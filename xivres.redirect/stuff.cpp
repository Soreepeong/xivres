#include "pch.h"
#include <xivres/installation.h>
#include <xivres/packed_stream.hotswap.h>
#include <xivres/packed_stream.model.h>
#include <xivres/packed_stream.standard.h>
#include <xivres/packed_stream.texture.h>
#include <xivres/sqpack.generator.h>
#include <xivres/sqpack.reader.h>
#include <xivres/path_spec.h>
#include <xivres/util.on_dtor.h>
#include <xivres/util.thread_pool.h>
#include <xivres/util.unicode.h>
#include <xivres/xivstring.h>

#include "textools.h"

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
decltype(CreateFileW)* s_createfilew_original;
decltype(SetFilePointerEx)* s_setfilepointerex_original;
decltype(ReadFile)* s_readfile_original;
decltype(CloseHandle)* s_closefile_original;

static std::mutex s_recursivePreventionMutex;
static std::set<DWORD> s_currentThreadIds;
static xivres::util::wrap_value_with_context<bool> prevent_recursive_file_operation() {
	std::lock_guard lock(s_recursivePreventionMutex);
	if (!s_currentThreadIds.emplace(GetCurrentThreadId()).second)
		return { true, []() {} };

	return {
		false,
		[]() { std::lock_guard lock(s_recursivePreventionMutex); s_currentThreadIds.erase(GetCurrentThreadId()); }
	};
}

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

static std::shared_mutex s_handleMtx;
static std::shared_mutex s_modAccessMtx;
static std::map<sqpack_id_t, std::map<uint64_t, std::deque<std::shared_ptr<xivres::hotswap_packed_stream>>>> s_allocations;
static std::map<xivres::path_spec, std::shared_ptr<xivres::packed_stream>> s_availableReplacementStreams;
static std::map<HANDLE, xivres::stream*> s_sqpackStreams;
static std::map<HANDLE, uint64_t> s_virtualFilePointers;

struct replacement_rule_t {
	srell::u8cregex From;
	std::string To;
	bool Stop = false;
};
static std::vector<replacement_rule_t> s_loadPathRegex;

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
		if (!s_installation) {
			auto recursivePreventer = prevent_recursive_file_operation();
			s_installation.emplace(get_game_dir());
		}
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

	auto recursivePreventer = prevent_recursive_file_operation();
	auto& reader = get_installation().get_sqpack(sqpkId.packid());
	recursivePreventer.clear();

	xivres::sqpack::generator gen(sqpkId.exname(), sqpkId.name());
	gen.add_sqpack(reader, true, true);

	for (const auto [space, count] : std::initializer_list<std::pair<uint32_t, size_t>>{ {16 * 1048576, 1024}, {128 * 1048576, 256}, {0x7FFFFFFF, 16} }) {
		auto& allocations = s_allocations[sqpkId][space];

		std::shared_ptr<xivres::hotswap_packed_stream> stream;
		for (size_t i = 0, counter = 0; i < count; i++, counter++) {
			xivres::path_spec spec(std::format("xivres-redirect/{:08}b-{:08x}.bin", space, counter));
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
	// OutputDebugStringA(std::format("cat=0x{:08x} res=0x{:08x} hash=0x{:08x} {}\n", categoryId, resourceType, resourceHash, pszPath).c_str());

	std::shared_lock lock(s_modAccessMtx, std::defer_lock);
	sqpack_id_t sqpkId(categoryId);

	try {
		if (!get_sqpack_view(sqpkId))
			return s_find_existing_resource_handle_original(p1, categoryId, resourceType, resourceHash);
		
		std::string transformed = pszPath;
		auto changed = false;
		for (const auto& rule : s_loadPathRegex) {
			auto n = srell::regex_replace(transformed, rule.From, rule.To);
			if (n != transformed) {
				transformed = std::move(n);
				if (rule.Stop)
					break;
			}
		}
		xivres::path_spec pathSpec(transformed);
		if (transformed != pszPath) {
			if (!s_availableReplacementStreams.contains(pathSpec) && get_installation().get_sqpack(sqpkId.packid()).find_entry_index(pathSpec) == (std::numeric_limits<size_t>::max)())
				pathSpec = pszPath;
			else
				strncpy_s(pszPath, MAX_PATH, transformed.c_str(), MAX_PATH);
		}

		lock.lock();
		if (const auto it = s_availableReplacementStreams.find(pathSpec); it != s_availableReplacementStreams.end()) {
			const auto& stream = it->second;
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
		return s_createfilew_original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

	const auto recursivePreventer = prevent_recursive_file_operation();
	if (*recursivePreventer)
		return s_createfilew_original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

	const auto path = std::filesystem::path(lpFileName);
	if (!exists(path.parent_path().parent_path().parent_path() / "ffxiv_dx11.exe"))
		return s_createfilew_original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

	if (path.parent_path().parent_path().filename() != L"sqpack")
		return s_createfilew_original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

	int sqpkType;
	if (path.extension() == L".index")
		sqpkType = -1;
	else if (path.extension() == L".index2")
		sqpkType = -2;
	else if (path.extension().wstring().starts_with(L".dat"))
		sqpkType = wcstol(path.extension().wstring().substr(4).c_str(), nullptr, 10);
	else
		return s_createfilew_original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

	const auto categoryId = static_cast<uint32_t>(std::wcstoul(path.filename().wstring().substr(0, 2).c_str(), nullptr, 16));
	const auto expacId = static_cast<uint32_t>(std::wcstoul(path.filename().wstring().substr(2, 2).c_str(), nullptr, 16));
	const auto partId = static_cast<uint32_t>(std::wcstoul(path.filename().wstring().substr(4, 2).c_str(), nullptr, 16));
	const auto sqpkId = sqpack_id_t(categoryId, expacId, partId);

	const auto pViews = get_sqpack_view(sqpkId);
	if (!pViews)
		return s_createfilew_original(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

	const auto indexPath = std::filesystem::path(path).replace_extension(".index");
	const auto hFile = s_createfilew_original(indexPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
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

	return s_setfilepointerex_original(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);
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
	return s_readfile_original(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

BOOL WINAPI DETOUR_CloseHandle(HANDLE hObject) {
	if (s_sqpackStreams.contains(hObject)) {
		std::lock_guard lock(s_handleMtx);
		s_sqpackStreams.erase(hObject);
		s_virtualFilePointers.erase(hObject);
	}

	return s_closefile_original(hObject);
}

void* find_existing_resource_handle_finder() {
	const auto pc = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
	auto& dosHeader = *reinterpret_cast<IMAGE_DOS_HEADER*>(pc);
	auto& ntHeader = *reinterpret_cast<IMAGE_NT_HEADERS64*>(pc + dosHeader.e_lfanew);
	const auto range = std::span(pc, ntHeader.OptionalHeader.SizeOfImage);
	const std::array<uint8_t, 3> lookfor1{ { 0x48, 0x8b, 0x4f } };
	const std::array<uint8_t, 10> lookfor2{ { 0x4c, 0x8b, 0xcd, 0x4d, 0x8b, 0xc4, 0x49, 0x8b, 0xd5, 0xe8 } };
	auto it = range.begin();
	while (it != range.end()) {
		it = std::search(it, range.end(), lookfor1.begin(), lookfor1.end());
		if (0 == memcmp(&*it + lookfor1.size() + 1, lookfor2.data(), lookfor2.size())) {
			it += lookfor1.size() + 1 + lookfor2.size();
			it += *reinterpret_cast<int*>(&*it);
			it += 4;
			return &*it;
		}
		it++;
	}
	// 48 8b 4f ?? 4c 8b cd 4d 8b c4 49 8b d5 e8
	ExitProcess(-100);
	return 0;
}

std::vector<void*> find_rsv_indirection_resolvers() {
	const auto pc = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
	auto& dosHeader = *reinterpret_cast<IMAGE_DOS_HEADER*>(pc);
	auto& ntHeader = *reinterpret_cast<IMAGE_NT_HEADERS64*>(pc + dosHeader.e_lfanew);
	const auto range = std::span(pc, ntHeader.OptionalHeader.SizeOfImage);
	const std::array<uint8_t, 10> lookfor{ { 0x8b, 0x01, 0x25, 0xff, 0xff, 0xff, 0x00, 0x48, 0x03, 0xc1} };
	std::vector<void*> res;
	auto it = range.begin();
	while (it != range.end()) {
		it = std::search(it, range.end(), lookfor.begin(), lookfor.end());
		if (it == range.end())
			break;
		res.emplace_back(&*it);
		it++;
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
	std::map<std::pair<textools::metafile::item_type_t, uint32_t>, xivres::equipment_deformer_parameter_file> eqdp;
	std::optional<xivres::equipment_parameter_file> eqp;
	std::optional<xivres::gimmmick_parameter_file> gmp;
	std::map<textools::metafile::est_type_t, xivres::ex_skeleton_table_file> est;

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
			
			textools::ttmpl_t ttmpl;
			if (json.size() == 1 && (json[0].find("SimpleModsList") != json[0].end() || json[0].find("ModPackPages") != json[0].end()))
				ttmpl = json[0].get<textools::ttmpl_t>();
			else {
				for (const auto& j : json)
					ttmpl.SimpleModsList.emplace_back(j.get<textools::mod_entry_t>());
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
			ttmpl.for_each([&](const textools::mod_entry_t& entry) {
				if (!entry.is_textools_metadata()) {
					const auto pathSpec = xivres::path_spec(entry.FullPath);
					auto& stream = s_availableReplacementStreams[pathSpec];
					stream = std::make_shared<xivres::stream_as_packed_stream>(pathSpec, ttmpd->substream(static_cast<std::streamoff>(entry.ModOffset), static_cast<std::streamsize>(entry.ModSize)));
					return;
				}

				if (!ttmpd)
					ttmpd = std::make_shared<xivres::file_stream>(ttmpdPath);

				const auto metadata = textools::metafile(entry.FullPath, xivres::unpacked_stream(std::make_shared<xivres::stream_as_packed_stream>(entry.FullPath, ttmpd->substream(entry.ModOffset, entry.ModSize))));
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
						auto& res = eqdp[key] = xivres::equipment_deformer_parameter_file(*get_installation().get_file(textools::metafile::equipment_deformer_parameter_path(type, race)));
						res.expand_or_collapse(true);
						return res;
					} else
						return it->second;
				});

				if (metadata.has_equipment_parameter_edits()) {
					if (!eqp) {
						eqp.emplace(*get_installation().get_file(textools::metafile::EqpPath));
						*eqp = eqp->expand_or_collapse(true);
					}
					metadata.apply_equipment_parameter_edits(*eqp);
				}

				if (metadata.has_gimmick_parameter_edits()) {
					if (!eqp) {
						gmp.emplace(*get_installation().get_file(textools::metafile::GmpPath));
						*gmp = gmp->expand_or_collapse(true);
					}
					metadata.apply_gimmick_parameter_edits(*gmp);
				}

				if (const auto it = est.find(metadata.EstType); it == est.end()) {
					if (const auto estPath = textools::metafile::ex_skeleton_table_path(metadata.EstType))
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
		const auto pathSpec = xivres::path_spec(textools::metafile::equipment_deformer_parameter_path(type, race));
		s_availableReplacementStreams[pathSpec] = std::make_shared<xivres::passthrough_packed_stream<xivres::standard_passthrough_packer>>(pathSpec, std::make_shared<xivres::memory_stream>(file.data()));
	}
	if (eqp) {
		const auto pathSpec = xivres::path_spec(textools::metafile::EqpPath);
		s_availableReplacementStreams[pathSpec] = std::make_shared<xivres::passthrough_packed_stream<xivres::standard_passthrough_packer>>(pathSpec, std::make_shared<xivres::memory_stream>(eqp->data_bytes()));
	}
	if (gmp) {
		const auto pathSpec = xivres::path_spec(textools::metafile::GmpPath);
		s_availableReplacementStreams[pathSpec] = std::make_shared<xivres::passthrough_packed_stream<xivres::standard_passthrough_packer>>(pathSpec, std::make_shared<xivres::memory_stream>(gmp->data_bytes()));
	}
	for (const auto& [estType, file] : est) {
		const auto pathSpec = textools::metafile::ex_skeleton_table_path(estType);
		s_availableReplacementStreams[pathSpec] = std::make_shared<xivres::passthrough_packed_stream<xivres::standard_passthrough_packer>>(pathSpec, std::make_shared<xivres::memory_stream>(file.data()));
	}
}

void update_path_replacements(const std::filesystem::path& ruleFile) {
	s_loadPathRegex.clear();

	if (!exists(ruleFile))
		return;

	nlohmann::json rules;
	std::ifstream(ruleFile, std::ios::binary) >> rules;
	if (!rules.is_array())
		throw std::runtime_error("root must be an array");

	for (const auto& item : rules) {
		if (!item.is_array() || item.size() < 2)
			continue;
		auto& rule = s_loadPathRegex.emplace_back();
		rule.From = item[0].get<std::string>();
		rule.To = item[1].get<std::string>();
		rule.Stop = item.size() < 3 || item[2].get<bool>();
	}
}

void continuous_update_mod_dirs() {
	std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&CloseHandle)> hChangeNotification(
		CreateFile(get_game_dir().c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr), &CloseHandle);

	const auto ttmpDir = get_game_dir() / "ttmp";
	const auto rawDir = get_game_dir() / "sqraw";
	const auto pathReplacementRuleFile = get_game_dir() / "path_replacements.json";

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
				} else if (_wcsnicmp(relativePath.c_str(), L"sqraw\\", 6) == 0) {
					applyChangesAt = GetTickCount64() + applyDelay;
					OutputDebugStringW(std::format(L"FS Change [{}]: {}\n", fni->Action, relativePath.c_str()).c_str());
				} else if (relativePath == "path_replacements.json") {
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

		try {
			update_path_replacements(pathReplacementRuleFile);
		} catch (const std::exception& e) {
			OutputDebugStringW(std::format(LR"(Error processing path replacement rules file: {})" "\n", xivres::util::unicode::convert<std::wstring>(e.what())).c_str());
		}
	}
}

void preload_stuff() {
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
}

void do_stuff() {
	MH_CreateHook(find_existing_resource_handle_finder(), &DETOUR_find_existing_resource_handle, (void**)&s_find_existing_resource_handle_original);
	for (const auto p : find_rsv_indirection_resolvers())
		MH_CreateHook(p, &DETOUR_resolve_string_indirection, (void**)&s_resolve_string_indirection_original);
	MH_CreateHook(&CreateFileW, &DETOUR_CreateFileW, (void**)&s_createfilew_original);
	MH_CreateHook(&SetFilePointerEx, &DETOUR_SetFilePointerEx, (void**)&s_setfilepointerex_original);
	MH_CreateHook(&ReadFile, &DETOUR_ReadFile, (void**)&s_readfile_original);
	MH_CreateHook(&CloseHandle, &DETOUR_CloseHandle, (void**)&s_closefile_original);
	MH_EnableHook(MH_ALL_HOOKS);

	std::thread([]() {continuous_update_mod_dirs(); }).detach();
}