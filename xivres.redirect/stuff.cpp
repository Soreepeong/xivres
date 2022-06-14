#include "pch.h"
#include <xivres/installation.h>
#include <xivres/packed_stream.hotswap.h>
#include <xivres/packed_stream.model.h>
#include <xivres/packed_stream.standard.h>
#include <xivres/packed_stream.texture.h>
#include <xivres/path_spec.h>
#include <xivres/sqpack.generator.h>
#include <xivres/sqpack.reader.h>
#include <xivres/textools.h>
#include <xivres/sound.h>
#include <xivres/util.on_dtor.h>
#include <xivres/util.thread_pool.h>
#include <xivres/util.unicode.h>
#include <xivres/xivstring.h>

#include "utils.h"

class oplocking_file_stream : public xivres::default_base_stream {
	const std::filesystem::path m_path;
	const bool m_bOpenAgain;
	mutable std::shared_ptr<std::remove_pointer_t<HANDLE>> m_hFile;
	std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&CloseHandle)> m_hOplockEvent;

	mutable std::streamsize m_size = 0;
	mutable std::mutex m_openMtx;
	mutable OVERLAPPED m_ovOplock{};
	mutable std::thread m_thOplockWaiter;

	mutable REQUEST_OPLOCK_INPUT_BUFFER m_inOplock = {
	  REQUEST_OPLOCK_CURRENT_VERSION,
	  sizeof(m_inOplock),
	  OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_HANDLE,
	  REQUEST_OPLOCK_INPUT_FLAG_REQUEST,
	};

	mutable REQUEST_OPLOCK_OUTPUT_BUFFER m_outOplock = {
	  REQUEST_OPLOCK_CURRENT_VERSION,
	  sizeof(m_outOplock),
	};

	template<typename T>
	static T throw_if_value(T val, T invalidVal) {
		if (val == invalidVal)
			throw std::runtime_error("Failure");
		return val;
	}

	void fire_oplock() const {
		DeviceIoControl(m_hFile.get(), FSCTL_REQUEST_OPLOCK,
			&m_inOplock, sizeof(m_inOplock),
			&m_outOplock, sizeof(m_outOplock),
			nullptr, &m_ovOplock);
		if (GetLastError() == ERROR_IO_PENDING) {
			if (m_thOplockWaiter.joinable())
				m_thOplockWaiter.join();

			m_thOplockWaiter = std::thread([this]() {
				DWORD dwBytes{};
				if (!GetOverlappedResult(m_hFile.get(), &m_ovOplock, &dwBytes, TRUE)) {
					if (GetLastError() == ERROR_CANCELLED)
						return;
				}

				m_hFile.reset();
			});
		}
	}

public:
	oplocking_file_stream(std::filesystem::path path, bool openAgain)
		: m_path(std::move(path))
		, m_bOpenAgain(openAgain)
		, m_hOplockEvent(throw_if_value(CreateEventW(nullptr, FALSE, FALSE, nullptr), HANDLE{}), &CloseHandle) {

		m_ovOplock.hEvent = m_hOplockEvent.get();

		open();
	}
	oplocking_file_stream(oplocking_file_stream&&) = delete;
	oplocking_file_stream(const oplocking_file_stream&) = delete;
	oplocking_file_stream& operator=(oplocking_file_stream&&) = delete;
	oplocking_file_stream& operator=(const oplocking_file_stream&) = delete;
	~oplocking_file_stream() override {
		CancelIoEx(m_hFile.get(), &m_ovOplock);
		if (m_thOplockWaiter.joinable())
			m_thOplockWaiter.join();
	}

	void open() const {
		if (m_hFile)
			return;

		const auto lock = std::lock_guard(m_openMtx);

		if (const auto h = CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr); h != INVALID_HANDLE_VALUE) {
			m_hFile = { h, &CloseHandle };

			LARGE_INTEGER fs{};
			GetFileSizeEx(m_hFile.get(), &fs);
			m_size = static_cast<std::streamsize>(fs.QuadPart);

			fire_oplock();
		}
	}

	void invalidate() {
		CancelIoEx(m_hFile.get(), &m_ovOplock);
		m_hFile.reset();
	}

	bool done() const {
		if (m_bOpenAgain)
			open();

		return !m_hFile;
	}

	[[nodiscard]] std::streamsize size() const override {
		if (m_bOpenAgain)
			open();

		return m_size;
	}

	std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override {
		if (m_bOpenAgain)
			open();

		if (done()) {
			memset(buf, 0, length);
			return length;
		}

		constexpr int64_t ChunkSize = 0x10000000L;
		if (length > ChunkSize) {
			size_t totalRead = 0;
			for (std::streamoff i = 0; i < length; i += ChunkSize) {
				const auto toRead = static_cast<DWORD>((std::min<int64_t>)(ChunkSize, length - i));
				const auto r = read(offset + i, static_cast<char*>(buf) + i, toRead);
				totalRead += r;
				if (r != toRead)
					break;
			}
			return static_cast<std::streamsize>(totalRead);

		} else {
			DWORD readLength = 0;
			OVERLAPPED ov{};
			std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(CloseHandle)*> hEvent(throw_if_value(CreateEventW(nullptr, FALSE, FALSE, nullptr), HANDLE{}), &CloseHandle);
			ov.hEvent = hEvent.get();
			ov.Offset = static_cast<DWORD>(offset);
			ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
			if (!ReadFile(m_hFile.get(), buf, static_cast<DWORD>(length), &readLength, &ov)) {
				const auto err = GetLastError();
				if (err != ERROR_HANDLE_EOF && err != ERROR_IO_PENDING)
					throw std::system_error(std::error_code(static_cast<int>(err), std::system_category()));
			}
			GetOverlappedResult(m_hFile.get(), &ov, &readLength, TRUE);
			return readLength;
		}
	}
};

class lazy_packing_oplocking_file_system : public xivres::packed_stream {
	const std::filesystem::path m_path;
	xivres::packed::type m_packedType = xivres::packed::type::placeholder;

	mutable std::shared_ptr<oplocking_file_stream> m_oplockingStream;
	mutable std::shared_ptr<xivres::packed_stream> m_packedStream;

public:
	lazy_packing_oplocking_file_system(xivres::path_spec pathSpec, std::filesystem::path path)
		: xivres::packed_stream(std::move(pathSpec))
		, m_path(std::move(path)) {

		std::wstring ext = m_path.extension().wstring();
		for (auto& c : ext)
			if (c < 128)
				c = std::tolower(c);

		if (ext == L".tex" || ext == L".atex")
			m_packedType = xivres::packed::type::texture;
		else if (ext == L".mdl")
			m_packedType = xivres::packed::type::model;
		else
			m_packedType = xivres::packed::type::standard;
	}

	xivres::packed::type get_packed_type() const override {
		return m_packedType;
	}

	std::streamsize size() const override {
		if (!open())
			return 0;

		return m_packedStream->size();
	}

	std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override {
		if (!open())
			return 0;

		return m_packedStream->read(offset, buf, length);
	}

	void close() {
		m_packedStream.reset();
		m_oplockingStream.reset();
	}

private:
	bool open() const {
		if (m_oplockingStream && !m_oplockingStream->done() && m_packedStream)
			return true;

		if (!m_oplockingStream || m_oplockingStream->done()) {
			if (!exists(m_path))
				return false;

			m_oplockingStream = std::make_shared<oplocking_file_stream>(m_path, false);
			if (m_oplockingStream->done())
				return false;
		}

		if (m_oplockingStream && !m_oplockingStream->done()) {
			try {
				switch (m_packedType) {
					case xivres::packed::type::standard:
						m_packedStream = std::make_shared<xivres::passthrough_packed_stream<xivres::standard_passthrough_packer>>(path_spec(), m_oplockingStream);
						break;
					case xivres::packed::type::model:
						m_packedStream = std::make_shared<xivres::passthrough_packed_stream<xivres::model_passthrough_packer>>(path_spec(), m_oplockingStream);
						break;
					case xivres::packed::type::texture:
						m_packedStream = std::make_shared<xivres::passthrough_packed_stream<xivres::texture_passthrough_packer>>(path_spec(), m_oplockingStream);
						break;
				}
			} catch (const std::exception& e) {
				OutputDebugStringW(std::format(LR"(Error processing file "{}": {})" "\n", m_path.wstring(), xivres::util::unicode::convert<std::wstring>(e.what())).c_str());
			}
		}

		return true;
	}
};

void* DETOUR_find_existing_resource_handle(void* p1, uint32_t& categoryId, uint32_t& resourceType, uint32_t& resourceHash);
const char* DETOUR_resolve_string_indirection(const char* p);

decltype(DETOUR_find_existing_resource_handle)* s_find_existing_resource_handle_original;
decltype(DETOUR_resolve_string_indirection)* s_resolve_string_indirection_original;

union sqpack_id_t {
	uint32_t FullId;
	struct {
		uint32_t CategoryId : 16;
		uint32_t PartId : 8;
		uint32_t ExpacId : 8;
	};

	sqpack_id_t() : FullId(0xFFFFFFFF) {}
	sqpack_id_t(uint32_t fullId) : FullId(fullId) {}
	sqpack_id_t(uint32_t categoryId, uint32_t expacId, uint32_t partId) : CategoryId(categoryId), PartId(partId), ExpacId(expacId) {}

	static sqpack_id_t from_filename_int(uint32_t n) {
		return { n >> 16, (n >> 8) & 0xFF, n & 0xFF };
	}

	std::string exname() const {
		return ExpacId == 0 ? "ffxiv" : std::format("ex{}", ExpacId);
	}

	uint32_t packid() const {
		return (CategoryId << 16) | (ExpacId << 8) | PartId;
	}

	std::string name() const {
		return std::format("{:06x}", packid());
	}

	friend auto operator<=>(const sqpack_id_t& l, const sqpack_id_t& r) {
		return l.FullId <=> r.FullId;
	}
};

struct xivres_redirect_config_t {
	class additional_game_root_t {
		mutable std::unique_ptr<xivres::installation> m_pInstallation;
		std::string m_sInstallationPath;

	public:
		class override_resource_t {
			mutable std::vector<std::optional<srell::u8cregex>> m_pathRegexCompiled;

			std::set<uint32_t> m_types;
			std::vector<std::string> m_paths;

		public:
			friend void from_json(const nlohmann::json& j, override_resource_t& obj) {
				obj.m_types.clear();
				for (const auto& typeObj : j.at("types")) {
					uint32_t resourceType = 0;
					for (const auto c : typeObj.get<std::string>())
						resourceType = (resourceType << 8) | static_cast<uint8_t>(c);
					obj.m_types.insert(resourceType);
				}

				obj.m_paths.clear();
				obj.m_pathRegexCompiled.clear();
				for (const auto& pathObj : j.at("paths"))
					obj.m_paths.emplace_back(pathObj.get<std::string>());
				obj.m_pathRegexCompiled.resize(obj.m_paths.size());
			}

			const srell::u8cregex& regex(size_t pathIndex) const {
				auto& slot = m_pathRegexCompiled.at(pathIndex);
				if (!slot)
					slot.emplace(m_paths.at(pathIndex), srell::regex_constants::icase);
				return *slot;
			}

			bool matches_any(uint32_t resourceType, const std::string& s) const {
				if (!std::ranges::any_of(m_types, [&](const auto& resType) { return resType == resourceType; }))
					return false;

				for (size_t i = 0; i < m_pathRegexCompiled.size(); ++i) {
					if (srell::regex_search(s, regex(i)))
						return true;
				}
				return false;
			}
		};

		std::vector<override_resource_t> OverrideResources;

		const std::string& installation_path() const {
			return m_sInstallationPath;
		}

		void installation_path(std::string s) {
			m_sInstallationPath = std::move(s);
			m_pInstallation.reset();
		}

		const xivres::installation& get_installation() const {
			if (!m_pInstallation)
				m_pInstallation = std::make_unique<xivres::installation>(m_sInstallationPath);
			return *m_pInstallation;
		}

		friend void from_json(const nlohmann::json& j, additional_game_root_t& obj) {
			obj.installation_path(j.at("path").get<std::string>());
			obj.OverrideResources = j.at("overrideResources").get<std::vector<override_resource_t>>();
		}
	};

	class replacement_rule_t {
		mutable std::optional<srell::u8cregex> m_regexFrom;

	public:
		std::string From;
		std::string To;
		bool Stop = false;

		const srell::u8cregex& regex() const {
			return *m_regexFrom;
		}

		friend void from_json(const nlohmann::json& j, replacement_rule_t& obj) {
			obj.From = j.at("from").get<std::string>();
			obj.To = j.at("to").get<std::string>();
			obj.Stop = j.value("stop", true);
			obj.m_regexFrom.emplace(obj.From, srell::regex_constants::icase);
		}
	};

	class log_pattern_filter_t {
		mutable std::optional<srell::u8cregex> m_regex;

	public:
		std::string Pattern;
		bool Include = true;

		const srell::u8cregex& regex() const {
			return *m_regex;
		}

		friend void from_json(const nlohmann::json& j, log_pattern_filter_t& obj) {
			obj.Pattern = j.at("pattern").get<std::string>();
			obj.Include = j.value("include", true);
			obj.m_regex.emplace(obj.Pattern, srell::regex_constants::icase);
		}
	};

	bool LogReplacedPaths = false;
	std::vector<log_pattern_filter_t> LogPathFilters;
	std::vector<additional_game_root_t> AdditionalRoots;
	std::vector<replacement_rule_t> PathReplacements;

	friend void from_json(const nlohmann::json& j, xivres_redirect_config_t& obj) {
		obj.LogReplacedPaths = j.value<bool>("logReplacedPaths", false);

		if (const auto it = j.find("logPathFilters"); it == j.end())
			obj.LogPathFilters.clear();
		else {
			if (!it->is_array())
				throw std::runtime_error("logPathFilters must be an array of objects");

			obj.LogPathFilters.clear();
			for (const auto& jobj : *it)
				obj.LogPathFilters.emplace_back(jobj.get<log_pattern_filter_t>());
		}

		if (const auto it = j.find("additionalRoots"); it == j.end())
			obj.AdditionalRoots.clear();
		else {
			if (!it->is_array())
				throw std::runtime_error("additionalRoots must be an array of objects");

			std::set<std::string> unvisited;
			for (const auto& prevItem : obj.AdditionalRoots)
				unvisited.emplace(prevItem.installation_path());

			std::map<std::string, size_t> pathOrder;
			for (size_t i = 0; i < it->size(); i++) {
				additional_game_root_t rootObj = it->at(i).get<additional_game_root_t>();

				auto root = std::filesystem::path(xivres::util::unicode::convert<std::wstring>(rootObj.installation_path()));
				if (!exists(root))
					continue;
				root = canonical(root);

				rootObj.installation_path(xivres::util::unicode::convert<std::string>(root.wstring()));
				pathOrder.emplace(rootObj.installation_path(), i);

				if (!unvisited.erase(rootObj.installation_path())) {
					auto& item = obj.AdditionalRoots.emplace_back(std::move(rootObj));
					item.get_installation().preload_all_sqpacks();
				} else {
					for (auto& info : obj.AdditionalRoots) {
						if (info.installation_path() != root)
							continue;

						info.OverrideResources = std::move(rootObj.OverrideResources);
					}
				}
			}
			for (auto it = obj.AdditionalRoots.begin(); it != obj.AdditionalRoots.end();) {
				if (pathOrder.contains(it->installation_path()))
					++it;
				else
					it = obj.AdditionalRoots.erase(it);
			}
			std::ranges::sort(obj.AdditionalRoots, [&pathOrder](const auto& l, const auto& r) { return pathOrder.at(l.installation_path()) < pathOrder.at(r.installation_path()); });
		}

		if (const auto it = j.find("pathReplacements"); it == j.end())
			obj.PathReplacements.clear();
		else {
			if (!it->is_array())
				throw std::runtime_error("pathReplacements must be an array of objects");

			obj.PathReplacements.clear();
			for (const auto& item : *it)
				obj.PathReplacements.emplace_back(item.get<replacement_rule_t>());
		}
	}
};

static std::shared_mutex s_handleMtx;
static std::shared_mutex s_modAccessMtx;
static std::map<sqpack_id_t, std::map<uint64_t, std::deque<std::shared_ptr<xivres::hotswap_packed_stream>>>> s_allocations;
static std::map<xivres::path_spec, std::shared_ptr<xivres::packed_stream>> s_availableReplacementStreams;
static std::map<HANDLE, xivres::stream*> s_sqpackStreams;
static std::map<HANDLE, uint64_t> s_virtualFilePointers;
static std::deque<std::string> s_fabricatedNameStorage;
static xivres_redirect_config_t s_config;

static std::filesystem::path get_game_dir() {
	static std::filesystem::path s_gameDir = []() {
		std::wstring self(PATHCCH_MAX_CCH, L'\0');
		self.resize(GetModuleFileNameW(GetModuleHandleW(nullptr), &self[0], PATHCCH_MAX_CCH));
		return std::filesystem::path(self).parent_path();
	}();
	return s_gameDir;
}

static const xivres::installation& get_installation() {
	static std::optional<xivres::installation> s_installation;
	if (!s_installation) {
		static std::mutex mtx;
		const auto lock = std::lock_guard(mtx);
		if (!s_installation)
			s_installation.emplace(get_game_dir());
	}
	return *s_installation;
}

[[nodiscard]] const xivres::sqpack::generator::sqpack_views* get_sqpack_view(sqpack_id_t sqpkId) {
	static std::map<sqpack_id_t, std::unique_ptr<xivres::sqpack::generator::sqpack_views>> s_sqpackViews;
	static std::map<sqpack_id_t, std::mutex> s_viewMtx;
	static bool s_sqpackViewsReady = false;

	if (!s_sqpackViewsReady) {
		static std::mutex s_sqpackMtx;
		const auto lock = std::lock_guard(s_sqpackMtx);
		if (!s_sqpackViewsReady) {
			for (const auto& id : get_installation().get_sqpack_ids()) {
				const auto sqpkId = sqpack_id_t::from_filename_int(id);
				s_sqpackViews.emplace(sqpkId, nullptr);
				static_cast<void>(s_viewMtx[sqpkId]);
			}
			s_sqpackViewsReady = true;
		}
	}

	if (!s_sqpackViews.contains(sqpkId))
		return nullptr;

	auto& ptr = s_sqpackViews.at(sqpkId);
	if (ptr)
		return ptr.get();

	const auto lock = std::lock_guard(s_viewMtx.at(sqpkId));
	if (ptr)
		return ptr.get();

	auto& reader = get_installation().get_sqpack(sqpkId.packid());

	xivres::sqpack::generator gen(sqpkId.exname(), sqpkId.name());
	gen.add_sqpack(reader, true, true);

	if (sqpkId.CategoryId == 7) {
		xivres::sound::reader scd(reader.at("sound/system/Sample_System.scd"));
		xivres::sound::writer blank;
		blank.set_table_1(scd.read_table_1());
		blank.set_table_2(scd.read_table_2());
		blank.set_table_4(scd.read_table_4());
		blank.set_table_5(scd.read_table_5());
		for (size_t i = 0; i < 256; ++i)
			blank.set_sound_item(i, xivres::sound::writer::sound_item::make_empty(std::chrono::milliseconds(100)));

		gen.add(std::make_shared<xivres::passthrough_packed_stream<xivres::standard_passthrough_packer>>("sound/empty256.scd", std::make_shared<xivres::memory_stream>(blank.Export())));
	}

	size_t counter = 0;
	for (const auto [space, count] : std::initializer_list<std::pair<uint32_t, size_t>>{ {16 * 1048576, 1024}, {128 * 1048576, 256}, {0x7FFFFFFF, 16} }) {
		auto& allocations = s_allocations[sqpkId][space];

		std::shared_ptr<xivres::hotswap_packed_stream> stream;
		const auto requiredPrefix = xivres::path_spec::required_prefix(sqpkId.packid());
		for (size_t i = 0; i < count; i++, counter++) {
			xivres::path_spec spec(std::format("{}x/{:x}.xrr", requiredPrefix, counter));
			if (!stream)
				stream = std::make_shared<xivres::hotswap_packed_stream>(spec, space);
			if (gen.add(stream, false).Added.empty()) {
				i--;
			} else {
				allocations.emplace_back(std::move(stream));
				stream = nullptr;
			}
		}
	}

	ptr = std::make_unique<xivres::sqpack::generator::sqpack_views>(gen.export_to_views(false));
	return ptr.get();
}

void* DETOUR_find_existing_resource_handle(void* p1, uint32_t& categoryId, uint32_t& resourceType, uint32_t& resourceHash) {
	const auto retAddr = static_cast<char**>(_AddressOfReturnAddress());
	auto& pszPath = retAddr[0x11];

	struct resource_load_details_t {
		void* Unknown_0x00;
		void* Unknown_0x08;
		uint32_t SegmentOffset;
		uint32_t SegmentLength;
	};
	uint32_t nSegmentOffset = 0, nSegmentLength = 0;
	if (auto& pLoadDetails = reinterpret_cast<resource_load_details_t**>(retAddr)[0x12]; pLoadDetails) {
		nSegmentOffset = pLoadDetails->SegmentOffset;
		nSegmentLength = pLoadDetails->SegmentLength;
	}

	sqpack_id_t sqpkId(categoryId);
	xivres::path_spec pathSpec(pszPath);
	std::shared_lock lock(s_modAccessMtx);
	std::shared_ptr<xivres::packed_stream> stream;

	for (const auto& filter : s_config.LogPathFilters) {
		if (srell::regex_search(pathSpec.path(), filter.regex())) {
			if (filter.Include) {
				OutputDebugStringW(xivres::util::unicode::convert<std::wstring>(std::format(
					"cat=0x{:08x} res=0x{:08x} hash=0x{:08x} {}\n", categoryId, resourceType, resourceHash, pszPath
				)).c_str());
			}
			break;
		}
	}

	try {
		const auto pViews = get_sqpack_view(sqpkId);
		if (!pViews)
			return s_find_existing_resource_handle_original(p1, categoryId, resourceType, resourceHash);

		std::string transformed = pszPath;
		auto changed = false;
		for (const auto& rule : s_config.PathReplacements) {
			auto n = srell::regex_replace(transformed, rule.regex(), rule.To);
			if (n != transformed) {
				transformed = std::move(n);
				if (rule.Stop)
					break;
			}
		}

		if (transformed != pszPath) {
			xivres::path_spec transformedPathSpec(transformed);
			auto exists = false;
			if (!exists) exists = s_availableReplacementStreams.contains(transformedPathSpec);
			if (!exists) exists = get_installation().get_sqpack(sqpkId.packid()).find_entry_index(transformedPathSpec) == (std::numeric_limits<size_t>::max)();
			for (const auto& additionalInstallation : s_config.AdditionalRoots) {
				if (!exists) exists = additionalInstallation.get_installation().get_sqpack(sqpkId.packid()).find_entry_index(transformedPathSpec) == (std::numeric_limits<size_t>::max)();
			}

			if (exists) {
				if (nSegmentLength) {
					const auto tstr = std::format("{}.{:x}.{:x}", transformed, nSegmentOffset, nSegmentLength);
					resourceHash = crc32_z(0, reinterpret_cast<const Bytef*>(tstr.c_str()), tstr.size());
				} else {
					resourceHash = crc32_z(0, reinterpret_cast<const Bytef*>(transformed.c_str()), transformed.size());
				}

				pathSpec = std::move(transformedPathSpec);
				s_fabricatedNameStorage.emplace_back(transformed.c_str());
				while (s_fabricatedNameStorage.size() > 1024)
					s_fabricatedNameStorage.pop_front();

				if (s_config.LogReplacedPaths)
					OutputDebugStringW(xivres::util::unicode::convert<std::wstring>(std::format(
						"Replaced path: {} => {}\n", pszPath, transformed
					)).c_str());

				pszPath = const_cast<char*>(s_fabricatedNameStorage.back().c_str());
			}
		}

		if (!stream) {
			if (const auto it = s_availableReplacementStreams.find(pathSpec); it != s_availableReplacementStreams.end())
				stream = it->second;
		}

		if (!stream) {
			const auto originalExists = get_installation().get_sqpack(sqpkId.packid()).find_entry_index(pathSpec) != (std::numeric_limits<size_t>::max)();
			for (const auto& additionalInstallation : s_config.AdditionalRoots) {
				if (originalExists) {
					auto use = false;

					for (const auto& cond : additionalInstallation.OverrideResources) {
						if (cond.matches_any(resourceType, pathSpec.path())) {
							use = true;
							break;
						}
					}

					if (!use)
						continue;
				}

				const auto& sqpk = additionalInstallation.get_installation().get_sqpack(sqpkId.packid());
				const auto index = sqpk.find_entry_index(pathSpec);
				if (index == (std::numeric_limits<size_t>::max)())
					continue;

				stream = sqpk.packed_at(sqpk.Entries[index]);
				break;
			}
		}

		if (stream) {
			const auto fileSize = static_cast<uint64_t>(stream->size());
			for (auto& [space, slots] : s_allocations.at(sqpkId)) {
				if (space < fileSize)
					continue;

				auto exists = false;
				for (auto it = slots.begin(); it != slots.end(); ++it) {
					if (auto base = (*it)->base_stream(); base && base->path_spec() == pathSpec) {
						exists = true;
						auto slot = std::move(*it);
						pszPath = const_cast<char*>(slot->path_spec().path().c_str());
						slots.erase(it);
						slots.emplace_front(std::move(slot));
						break;
					}
				}
				if (exists)
					break;

				std::shared_ptr<xivres::hotswap_packed_stream> slot = std::move(slots.back());
				slot->swap_stream(stream);
				pszPath = const_cast<char*>(slot->path_spec().path().c_str());
				slots.pop_back();
				slots.emplace_front(std::move(slot));
				break;
			}
		}
	} catch (const std::out_of_range&) {
		// pass
	}

	return s_find_existing_resource_handle_original(p1, categoryId, resourceType, resourceHash);
}

const char* DETOUR_resolve_string_indirection(const char* p) {
	static const auto SeStringTester = [](const char* ptr) {
		try {
			void(xivres::xivstring(ptr).parsed());
			return true;
		} catch (...) {
			return false;
		}
	};

	__try {
		return s_resolve_string_indirection_original(p);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		// pass
	}
	__try {
		auto p2 = s_resolve_string_indirection_original(p - 0x0e);
		if (SeStringTester(p2))
			return p2;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		// pass
	}

	if (SeStringTester(p + 0x02))
		return p + 0x02;

	return "<error>";
}

HANDLE WINAPI DETOUR_CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
	if (dwDesiredAccess != GENERIC_READ || dwCreationDisposition != OPEN_EXISTING)
		return CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

	const auto path = std::filesystem::path(lpFileName);
	if (!exists(path.parent_path().parent_path().parent_path() / "ffxiv_dx11.exe"))
		return CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

	if (path.parent_path().parent_path().filename() != L"sqpack")
		return CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

	int sqpkType;
	if (path.extension() == L".index")
		sqpkType = -1;
	else if (path.extension() == L".index2")
		sqpkType = -2;
	else if (path.extension().wstring().starts_with(L".dat"))
		sqpkType = wcstol(path.extension().wstring().substr(4).c_str(), nullptr, 10);
	else
		return CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

	const auto categoryId = static_cast<uint32_t>(std::wcstoul(path.filename().wstring().substr(0, 2).c_str(), nullptr, 16));
	const auto expacId = static_cast<uint32_t>(std::wcstoul(path.filename().wstring().substr(2, 2).c_str(), nullptr, 16));
	const auto partId = static_cast<uint32_t>(std::wcstoul(path.filename().wstring().substr(4, 2).c_str(), nullptr, 16));
	const auto sqpkId = sqpack_id_t(categoryId, expacId, partId);

	const auto pViews = get_sqpack_view(sqpkId);
	if (!pViews)
		return CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

	const auto indexPath = std::filesystem::path(path).replace_extension(".index");
	const auto hFile = CreateFileW(indexPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return hFile;

	std::lock_guard lock(s_handleMtx);
	switch (sqpkType) {
		case -1:
			OutputDebugStringW(std::format(L"Opening {}.win32.index\n", xivres::util::unicode::convert<std::wstring>(sqpkId.name())).c_str());
			s_sqpackStreams[hFile] = pViews->Index1.get();
			s_virtualFilePointers[hFile] = 0;
			return hFile;

		case -2:
			OutputDebugStringW(std::format(L"Opening {}.win32.index2\n", xivres::util::unicode::convert<std::wstring>(sqpkId.name())).c_str());
			s_sqpackStreams[hFile] = pViews->Index2.get();
			s_virtualFilePointers[hFile] = 0;
			return hFile;

		default:
			if (sqpkType < 0 || sqpkType >= pViews->Data.size()) {
				CloseHandle(hFile);
				return INVALID_HANDLE_VALUE;
			}

			OutputDebugStringW(std::format(L"Opening {}.win32.dat{}\n", xivres::util::unicode::convert<std::wstring>(sqpkId.name()), sqpkType).c_str());
			s_sqpackStreams[hFile] = pViews->Data.at(sqpkType).get();
			s_virtualFilePointers[hFile] = 0;
			return hFile;
	}

	return hFile;
}

BOOL WINAPI DETOUR_SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod) {
	if (s_sqpackStreams.contains(hFile)) {
		std::shared_lock lock(s_handleMtx);
		if (const auto it = s_virtualFilePointers.find(hFile); it != s_virtualFilePointers.end()) {
			auto& ptr = it->second;
			switch (dwMoveMethod) {
				case FILE_BEGIN:
					ptr = liDistanceToMove.QuadPart;
					break;
				case FILE_CURRENT:
					ptr += liDistanceToMove.QuadPart;
					break;
				case FILE_END:
					ptr = s_sqpackStreams.at(hFile)->size() + liDistanceToMove.QuadPart;
					break;
				default:
					SetLastError(ERROR_INVALID_PARAMETER);
					return FALSE;
			}

			if (lpNewFilePointer)
				lpNewFilePointer->QuadPart = ptr;

			return TRUE;
		}
	}

	return SetFilePointerEx(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);
}

BOOL WINAPI DETOUR_ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) {
	if (s_sqpackStreams.contains(hFile)) {
		std::shared_lock lock(s_handleMtx);
		if (const auto it = s_sqpackStreams.find(hFile); it != s_sqpackStreams.end()) {
			auto ptr = s_virtualFilePointers[hFile];
			const auto& stream = *it->second;
			lock.unlock();
			const auto read = stream.read(ptr, lpBuffer, nNumberOfBytesToRead);
			if (lpNumberOfBytesRead)
				*lpNumberOfBytesRead = static_cast<DWORD>(read);
			lock.lock();
			s_virtualFilePointers[hFile] = ptr + read;
			return TRUE;
		}
	}
	return ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

BOOL WINAPI DETOUR_CloseHandle(HANDLE hObject) {
	if (s_sqpackStreams.contains(hObject)) {
		std::lock_guard lock(s_handleMtx);
		s_sqpackStreams.erase(hObject);
		s_virtualFilePointers.erase(hObject);
	}

	return CloseHandle(hObject);
}

static std::span<char> get_clean_exe_span() {
	static std::vector<char> buf = []() {
		std::shared_ptr<void> hFile(CreateFileW((get_game_dir() / "ffxiv_dx11.exe").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr), &CloseHandle);
		std::vector<char> buf(GetFileSize(hFile.get(), nullptr));
		if (DWORD rd; !ReadFile(hFile.get(), &buf[0], static_cast<DWORD>(buf.size()), &rd, nullptr) || rd != buf.size())
			throw std::runtime_error(std::format("Failed to read file: {}", GetLastError()));
		return buf;
	}();
	return { buf };
}

static std::pair<std::span<const char>, ptrdiff_t> get_clean_text_section() {
	const auto buf = get_clean_exe_span();
	const auto& dosHeader = *reinterpret_cast<IMAGE_DOS_HEADER*>(&buf[0]);
	const auto& ntHeader64 = *reinterpret_cast<IMAGE_NT_HEADERS64*>(&buf[dosHeader.e_lfanew]);
	const auto sectionHeaders = std::span(IMAGE_FIRST_SECTION(&ntHeader64), ntHeader64.FileHeader.NumberOfSections);
	const auto realBase = reinterpret_cast<char*>(GetModuleHandleW(nullptr));
	const auto imageBase = reinterpret_cast<char*>(ntHeader64.OptionalHeader.ImageBase);
	const auto& rva2sec = [&](size_t rva) -> IMAGE_SECTION_HEADER& {
		for (auto& sectionHeader2 : sectionHeaders) {
			if (sectionHeader2.VirtualAddress <= rva && rva < sectionHeader2.VirtualAddress + sectionHeader2.Misc.VirtualSize)
				return sectionHeader2;
		}
		throw std::runtime_error("rva");
	};
	const auto& va2rva = [&](void* va) {
		return reinterpret_cast<char*>(va) - imageBase;
	};
	const auto& va2sec = [&](void* va) -> IMAGE_SECTION_HEADER& {
		return rva2sec(reinterpret_cast<char*>(va) - imageBase);
	};
	for (const auto& sectionHeader : sectionHeaders) {
		const auto section = std::span(&buf[sectionHeader.PointerToRawData], sectionHeader.SizeOfRawData);
		if (strncmp(reinterpret_cast<const char*>(sectionHeader.Name), ".text", IMAGE_SIZEOF_SHORT_NAME) != 0)
			continue;

		return { section, sectionHeader.VirtualAddress - sectionHeader.PointerToRawData };
	}

	throw std::runtime_error(".text section not found?");
}

static void* find_existing_resource_handle_finder() {
	const auto [section, delta] = get_clean_text_section();
	const auto res = utils::signature_finder()
		.look_in(section)
		.look_for_hex("48 8b 4f ?? 4c 8b cd 4d 8b c4 49 8b d5 e8")
		.find_one();
	auto p = reinterpret_cast<char*>(GetModuleHandleW(nullptr)) + delta +
		(res.data() + res.size() - get_clean_exe_span().data());
	return p + *reinterpret_cast<int*>(p) + 4;
}

static std::vector<void*> find_rsv_indirection_resolvers() {
	const auto [section, delta] = get_clean_text_section();
	std::vector<void*> res;
	for (const auto& match : utils::signature_finder()
		.look_in(section)
		.look_for_hex("8b 01 25 ff ff ff 00 48 03 c1")
		.find(1, 100, false)) {
		res.push_back(reinterpret_cast<char*>(GetModuleHandleW(nullptr)) + delta + (match.Match.data() - get_clean_exe_span().data()));
	}
	return res;
}

void update_raw_dirs(const std::filesystem::path& rawDir) {
	std::vector<std::filesystem::path> directoriesToCheckForMapping;
	if (!exists(rawDir))
		return;

	for (const auto& innerRoot : std::filesystem::directory_iterator(rawDir)) {
		if (!innerRoot.is_directory())
			continue;

		if (exists(std::filesystem::path(innerRoot).parent_path() / "disable"))
			continue;

		const auto innerRootPathLength = innerRoot.path().wstring().size();
		directoriesToCheckForMapping.clear();
		for (const auto& file : std::filesystem::recursive_directory_iterator(innerRoot, std::filesystem::directory_options::follow_directory_symlink | std::filesystem::directory_options::skip_permission_denied)) {
			if (file.is_directory()) {
				directoriesToCheckForMapping.emplace_back(file.path());
				continue;
			}

			const auto pathSpec = xivres::path_spec(file.path().wstring().substr(innerRootPathLength + 1));
			auto& stream = s_availableReplacementStreams[pathSpec];
			stream = std::make_shared<lazy_packing_oplocking_file_system>(pathSpec, file.path());
		}
		for (const auto& mapBasePath : directoriesToCheckForMapping) {
			const auto mapDefinitionPath = mapBasePath / "mapping.json";
			if (!exists(mapDefinitionPath))
				continue;

			try {
				nlohmann::json json;
				std::ifstream(mapDefinitionPath) >> json;
				if (!json.is_object())
					throw std::runtime_error("Expected object");
				for (const auto& [k, v] : json.items()) {
					const auto targetDir = mapBasePath / xivres::util::unicode::convert<std::wstring>(v.get<std::string>());
					if (!exists(targetDir))
						continue;
					const auto targetDirPathLength = targetDir.wstring().size();

					const auto sourceDir = (mapBasePath / xivres::util::unicode::convert<std::wstring>(k)).wstring().substr(innerRootPathLength + 1) + L"/";
					for (const auto& file : std::filesystem::recursive_directory_iterator(targetDir, std::filesystem::directory_options::follow_directory_symlink | std::filesystem::directory_options::skip_permission_denied)) {
						if (file.is_directory())
							continue;

						const auto pathSpec = xivres::path_spec(sourceDir + file.path().wstring().substr(targetDirPathLength + 1));
						auto& stream = s_availableReplacementStreams[pathSpec];
						stream = std::make_shared<lazy_packing_oplocking_file_system>(pathSpec, file.path());
					}
				}
			} catch (const std::exception& e) {
				OutputDebugStringW(std::format(LR"(Error processing mapping definition file "{}": {})" "\n", mapDefinitionPath.wstring(), xivres::util::unicode::convert<std::wstring>(e.what())).c_str());
			}
		}
	}
}

void update_ttmp_files(const std::filesystem::path& ttmpDir) {
	std::vector<std::filesystem::path> directoriesToCheckForMapping;
	if (!exists(ttmpDir))
		return;

	std::map<xivres::path_spec, xivres::image_change_data::file> imc;
	std::map<std::pair<xivres::textools::metafile::item_types, uint32_t>, xivres::equipment_deformer_parameter_file> eqdp;
	std::optional<xivres::equipment_parameter_file> eqp;
	std::optional<xivres::gimmmick_parameter_file> gmp;
	std::map<xivres::textools::metafile::est_types, xivres::ex_skeleton_table_file> est;

	std::vector<std::filesystem::path> paths;
	for (const auto& path : std::filesystem::recursive_directory_iterator(ttmpDir, std::filesystem::directory_options::follow_directory_symlink | std::filesystem::directory_options::skip_permission_denied)) {
		if (path.is_directory())
			continue;
		if (path.path().extension() != L".mpl")
			continue;
		paths.emplace_back(path.path());
	}
	std::ranges::sort(paths);

	for (const auto& path : paths) {
		const auto ttmpdPath = std::filesystem::path(path).replace_filename("TTMPD.mpd");
		if (!exists(ttmpdPath))
			continue;

		auto disable = false;
		for (auto p = path, parent = p.parent_path(); !disable && p != parent; p = parent, parent = p.parent_path())
			disable = exists(parent / "disable");
		if (disable)
			continue;

		try {
			std::vector<nlohmann::json> json;
			try {
				std::ifstream in(path, std::ios::binary);
				while (true) {
					json.emplace_back();
					in >> json.back();
				}
			} catch (const std::exception& e) {
				if (json.size() == 1)
					throw e;
				json.pop_back();
			}

			xivres::textools::mod_pack_json ttmpl;
			if (json.size() == 1 && (json[0].find("SimpleModsList") != json[0].end() || json[0].find("ModPackPages") != json[0].end()))
				ttmpl = json[0].get<xivres::textools::mod_pack_json>();
			else {
				for (const auto& j : json)
					ttmpl.SimpleModsList.emplace_back(j.get<xivres::textools::mods_json>());
			}

			const auto choicesFile = std::filesystem::path(path).replace_filename("choices.json");
			nlohmann::json choices;
			if (exists(choicesFile))
				std::ifstream(choicesFile, std::ios::binary) >> choices;

			auto choicesFixed = false;

			if (!choices.is_array()) {
				choices = nlohmann::json::array();
				choicesFixed = true;
			}

			for (; choices.size() > ttmpl.ModPackPages.size(); choicesFixed = true)
				choices.erase(choices.size() - 1);
			for (; choices.size() < ttmpl.ModPackPages.size(); choicesFixed = true)
				choices.emplace_back(nlohmann::json::array());
			for (size_t pageObjectIndex = 0; pageObjectIndex < ttmpl.ModPackPages.size(); ++pageObjectIndex) {
				const auto& modGroups = ttmpl.ModPackPages[pageObjectIndex].ModGroups;
				if (modGroups.empty())
					continue;

				auto& pageChoices = choices.at(pageObjectIndex);
				if (!pageChoices.is_array()) {
					pageChoices = nlohmann::json::array();
					choicesFixed = true;
				}

				for (; pageChoices.size() > modGroups.size(); choicesFixed = true)
					pageChoices.erase(pageChoices.size() - 1);

				for (size_t modGroupIndex = 0; modGroupIndex < modGroups.size(); ++modGroupIndex) {
					const auto& modGroup = modGroups[modGroupIndex];
					if (modGroups.empty())
						continue;

					while (pageChoices.size() <= modGroupIndex) {
						choicesFixed = true;
						pageChoices.emplace_back(modGroup.SelectionType == "Multi" ? nlohmann::json::array() : nlohmann::json::array({ 0 }));
					}

					auto& modGroupChoice = pageChoices.at(modGroupIndex);
					if (!modGroupChoice.is_array()) {
						modGroupChoice = nlohmann::json::array({ modGroupChoice });
						choicesFixed = true;
					}

					for (auto& e : modGroupChoice) {
						if (!e.is_number_unsigned()) {
							e = 0;
							choicesFixed = true;
						} else if (e.get<size_t>() >= modGroup.OptionList.size()) {
							e = modGroup.OptionList.size() - 1;
							choicesFixed = true;
						}
					}
					modGroupChoice = modGroupChoice.get<std::set<size_t>>();
				}
			}

			if (choicesFixed)
				std::ofstream(std::filesystem::path(choicesFile).replace_filename("choices.fixed.json"), std::ios::binary) << choices.dump(1, '\t');

			std::shared_ptr<xivres::stream> ttmpd = std::make_shared<oplocking_file_stream>(ttmpdPath, true);
			ttmpl.for_each([&](const xivres::textools::mods_json& entry) {
				if (!entry.is_textools_metadata()) {
					const auto pathSpec = xivres::path_spec(entry.FullPath);
					auto& stream = s_availableReplacementStreams[pathSpec];
					stream = std::make_shared<xivres::stream_as_packed_stream>(pathSpec, ttmpd->substream(static_cast<std::streamoff>(entry.ModOffset), static_cast<std::streamsize>(entry.ModSize)));
					return;
				}

				if (!ttmpd)
					ttmpd = std::make_shared<xivres::file_stream>(ttmpdPath);

				const auto metadata = xivres::textools::metafile(entry.FullPath, xivres::unpacked_stream(std::make_shared<xivres::stream_as_packed_stream>(entry.FullPath, ttmpd->substream(entry.ModOffset, entry.ModSize))));
				metadata.apply_image_change_data_edits([&]() -> xivres::image_change_data::file& {
					const auto& imcPath = metadata.TargetImcPath;
					if (const auto it = imc.find(imcPath); it == imc.end())
						return imc[imcPath] = xivres::image_change_data::file(*get_installation().get_file(metadata.SourceImcPath));
					else
						return it->second;
				});

				metadata.apply_equipment_deformer_parameter_edits([&](auto type, auto race) -> xivres::equipment_deformer_parameter_file& {
					const auto key = std::make_pair(type, race);
					if (const auto it = eqdp.find(key); it == eqdp.end()) {
						auto& res = eqdp[key] = xivres::equipment_deformer_parameter_file(*get_installation().get_file(xivres::textools::metafile::equipment_deformer_parameter_path(type, race)));
						res.expand_or_collapse(true);
						return res;
					} else
						return it->second;
				});

				if (metadata.has_equipment_parameter_edits()) {
					if (!eqp) {
						eqp.emplace(*get_installation().get_file(xivres::textools::metafile::EqpPath));
						*eqp = eqp->expand_or_collapse(true);
					}
					metadata.apply_equipment_parameter_edits(*eqp);
				}

				if (metadata.has_gimmick_parameter_edits()) {
					if (!eqp) {
						gmp.emplace(*get_installation().get_file(xivres::textools::metafile::GmpPath));
						*gmp = gmp->expand_or_collapse(true);
					}
					metadata.apply_gimmick_parameter_edits(*gmp);
				}

				if (const auto it = est.find(metadata.EstType); it == est.end()) {
					if (const auto estPath = xivres::textools::metafile::ex_skeleton_table_path(metadata.EstType))
						metadata.apply_ex_skeleton_table_edits(est[metadata.EstType] = xivres::ex_skeleton_table_file(*get_installation().get_file(estPath)));
				} else
					metadata.apply_ex_skeleton_table_edits(it->second);
			}, choices);

		} catch (const std::exception& e) {
			OutputDebugStringW(std::format(LR"(Error processing ttmpl "{}": {})" "\n", path.wstring(), xivres::util::unicode::convert<std::wstring>(e.what())).c_str());
		}
	}

	for (const auto& [imcPath, file] : imc) {
		const auto pathSpec = xivres::path_spec(imcPath);
		s_availableReplacementStreams[pathSpec] = std::make_shared<xivres::passthrough_packed_stream<xivres::standard_passthrough_packer>>(pathSpec, std::make_shared<xivres::memory_stream>(file.data()));
	}
	for (const auto& [spec, file] : eqdp) {
		auto& [type, race] = spec;
		const auto pathSpec = xivres::path_spec(xivres::textools::metafile::equipment_deformer_parameter_path(type, race));
		s_availableReplacementStreams[pathSpec] = std::make_shared<xivres::passthrough_packed_stream<xivres::standard_passthrough_packer>>(pathSpec, std::make_shared<xivres::memory_stream>(file.data()));
	}
	if (eqp) {
		const auto pathSpec = xivres::path_spec(xivres::textools::metafile::EqpPath);
		s_availableReplacementStreams[pathSpec] = std::make_shared<xivres::passthrough_packed_stream<xivres::standard_passthrough_packer>>(pathSpec, std::make_shared<xivres::memory_stream>(eqp->data_bytes()));
	}
	if (gmp) {
		const auto pathSpec = xivres::path_spec(xivres::textools::metafile::GmpPath);
		s_availableReplacementStreams[pathSpec] = std::make_shared<xivres::passthrough_packed_stream<xivres::standard_passthrough_packer>>(pathSpec, std::make_shared<xivres::memory_stream>(gmp->data_bytes()));
	}
	for (const auto& [estType, file] : est) {
		const auto pathSpec = xivres::textools::metafile::ex_skeleton_table_path(estType);
		s_availableReplacementStreams[pathSpec] = std::make_shared<xivres::passthrough_packed_stream<xivres::standard_passthrough_packer>>(pathSpec, std::make_shared<xivres::memory_stream>(file.data()));
	}
}

void continuous_update_mod_dirs(HANDLE hReady) {
	std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&CloseHandle)> hChangeNotification(
		CreateFile(get_game_dir().c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr), &CloseHandle);

	const auto ttmpDir = get_game_dir() / "ttmp";
	const auto rawDir = get_game_dir() / "sqraw";
	const auto configPath = get_game_dir() / "xivres.redirect.config.json";

	std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&CloseHandle)> hOplockEvent(CreateEventW(nullptr, TRUE, TRUE, nullptr), &CloseHandle);
	std::vector<char> buf(65536);
	OVERLAPPED ov{};
	ov.hEvent = hOplockEvent.get();

	const auto applyDelay = 200ULL;
	for (uint64_t applyChangesAt = 0;;) {
		if (WaitForSingleObject(ov.hEvent, 0) == WAIT_OBJECT_0) {
			ResetEvent(ov.hEvent);
			ReadDirectoryChangesW(hChangeNotification.get(), &buf[0], 65534, TRUE,
				FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
				nullptr, &ov, nullptr);
		}

		bool changed = false;
		if (applyChangesAt == (std::numeric_limits<uint64_t>::max)())
			WaitForSingleObject(ov.hEvent, INFINITE);
		else if (const auto now = GetTickCount64(); applyChangesAt > now)
			WaitForSingleObject(ov.hEvent, static_cast<DWORD>((std::min)(applyChangesAt - now, 60000ULL)));

		if (DWORD read; GetOverlappedResult(hChangeNotification.get(), &ov, &read, FALSE)) {
			auto fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(&buf[0]);
			for (;; fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<char*>(fni) + fni->NextEntryOffset)) {
				std::filesystem::path relativePath(std::wstring_view(fni->FileName, fni->FileNameLength / 2));
				auto fullPath = get_game_dir() / relativePath;

				if (_wcsnicmp(relativePath.c_str(), L"ttmp\\", 5) == 0) {
					auto name = relativePath.filename().wstring();
					for (auto& c : name)
						if (c < 128)
							c = std::tolower(c);
					if (name == L"ttmpd.mpd" || name == L"ttmpl.mpl" || name == L"choices.json" || name == L"disable") {
						applyChangesAt = GetTickCount64() + applyDelay;
						OutputDebugStringW(std::format(L"FS Change [{}]: {}\n", fni->Action, relativePath.c_str()).c_str());
					}
				} else if (_wcsnicmp(relativePath.c_str(), L"sqraw\\", 6) == 0
					|| relativePath == "xivres.redirect.config.json") {
					applyChangesAt = GetTickCount64() + applyDelay;
					OutputDebugStringW(std::format(L"FS Change [{}]: {}\n", fni->Action, relativePath.c_str()).c_str());
				}

				if (fni->NextEntryOffset == 0)
					break;
			}
		}

		if (applyChangesAt > GetTickCount64())
			continue;

		applyChangesAt = (std::numeric_limits<uint64_t>::max)();

		const auto lock = std::lock_guard(s_modAccessMtx);
		s_availableReplacementStreams.clear();
		// if (s_emptyScd) s_availableReplacementStreams.emplace(s_emptyScd->path_spec(), s_emptyScd);

		try {
			nlohmann::json json;
			std::ifstream(configPath) >> json;
			from_json(json, s_config);
		} catch (const std::exception& e) {
			OutputDebugStringW(std::format(LR"(Error processing additional root rules file: {})" "\n", xivres::util::unicode::convert<std::wstring>(e.what())).c_str());
		}

		try {
			update_raw_dirs(rawDir);
		} catch (const std::exception& e) {
			OutputDebugStringW(std::format(LR"(Error processing raw directory: {})" "\n", xivres::util::unicode::convert<std::wstring>(e.what())).c_str());
		}

		try {
			update_ttmp_files(ttmpDir);
		} catch (const std::exception& e) {
			OutputDebugStringW(std::format(LR"(Error processing ttmp directory: {})" "\n", xivres::util::unicode::convert<std::wstring>(e.what())).c_str());
		}

		if (hReady) {
			SetEvent(hReady);
			hReady = nullptr;
		}
	}
}

void preload_stuff() {
	std::thread([]() {
		xivres::util::thread_pool pool;
		try {
			auto packIds = get_installation().get_sqpack_ids();
			std::ranges::sort(packIds, [](const auto l, const auto r) {
				auto partIdL = l & 0xFF;
				auto partIdR = r & 0xFF;
				if (partIdL != partIdR)
					return partIdL < partIdR;

				auto expacIdL = (l >> 8) & 0xFF;
				auto expacIdR = (r >> 8) & 0xFF;
				if (expacIdL != expacIdR)
					return expacIdL < expacIdR;

				auto categoryIdL = l >> 16;
				auto categoryIdR = r >> 16;
				if (categoryIdL != categoryIdR)
					return categoryIdL < categoryIdR;
				return false;
			});
			for (const auto id : packIds) {
				pool.Submit([id]() {
					static_cast<void>(get_sqpack_view(sqpack_id_t::from_filename_int(id)));
				});
			}
			pool.SubmitDoneAndWait();
		} catch (const std::exception&) {
			pool.PropagateInnerErrorIfErrorOccurred();
		}
	}).detach();
}

void do_stuff() {
	MH_CreateHook(find_existing_resource_handle_finder(), &DETOUR_find_existing_resource_handle, (void**)&s_find_existing_resource_handle_original);
	for (const auto p : find_rsv_indirection_resolvers())
		MH_CreateHook(p, &DETOUR_resolve_string_indirection, (void**)&s_resolve_string_indirection_original);

	if (void* pfn; utils::loaded_module::current_process().find_imported_function_pointer("kernel32.dll", "CreateFileW", 0, pfn)) {
		utils::memory_tenderizer m(pfn, sizeof(void*), PAGE_READWRITE);
		*reinterpret_cast<void**>(pfn) = &DETOUR_CreateFileW;
	}

	if (void* pfn; utils::loaded_module::current_process().find_imported_function_pointer("kernel32.dll", "SetFilePointerEx", 0, pfn)) {
		utils::memory_tenderizer m(pfn, sizeof(void*), PAGE_READWRITE);
		*reinterpret_cast<void**>(pfn) = &DETOUR_SetFilePointerEx;
	}

	if (void* pfn; utils::loaded_module::current_process().find_imported_function_pointer("kernel32.dll", "ReadFile", 0, pfn)) {
		utils::memory_tenderizer m(pfn, sizeof(void*), PAGE_READWRITE);
		*reinterpret_cast<void**>(pfn) = &DETOUR_ReadFile;
	}

	if (void* pfn; utils::loaded_module::current_process().find_imported_function_pointer("kernel32.dll", "CloseHandle", 0, pfn)) {
		utils::memory_tenderizer m(pfn, sizeof(void*), PAGE_READWRITE);
		*reinterpret_cast<void**>(pfn) = &DETOUR_CloseHandle;
	}

	MH_EnableHook(MH_ALL_HOOKS);

	std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&CloseHandle)> hReadyEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr), &CloseHandle);
	std::thread([&hReadyEvent]() {continuous_update_mod_dirs(hReadyEvent.get()); }).detach();
	WaitForSingleObject(hReadyEvent.get(), INFINITE);
}