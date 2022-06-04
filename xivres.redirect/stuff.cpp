#include "pch.h"
#include <xivres/path_spec.h>
#include <xivres/sqpack.reader.h>
#include <xivres/util.on_dtor.h>
#include <xivres/util.unicode.h>
#include <xivres/sqpack.generator.h>
#include <xivres/packed_stream.hotswap.h>
#include <xivres/packed_stream.standard.h>
#include <xivres/packed_stream.texture.h>
#include <xivres/packed_stream.model.h>
#include <xivres/installation.h>

class oplocking_file_stream : public xivres::default_base_stream {
	const std::filesystem::path m_path;
	mutable std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(CloseHandle)*> m_hFile;
	std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(CloseHandle)*> m_hOplockEvent;

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
				m_thOplockWaiter.detach();
			});
		}
	}

	bool try_open_again() const {
		if (m_hFile)
			return true;
		
		const auto h = CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return false;

		m_hFile = { h, &CloseHandle };
		fire_oplock();
		return true;
	}

public:
	oplocking_file_stream(std::filesystem::path path)
		: m_path(std::move(path))
		, m_hFile(throw_if_value(CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr), INVALID_HANDLE_VALUE), &CloseHandle)
		, m_hOplockEvent(throw_if_value(CreateEventW(nullptr, FALSE, FALSE, nullptr), HANDLE{}), &CloseHandle) {

		m_ovOplock.hEvent = m_hOplockEvent.get();
		fire_oplock();
	}
	oplocking_file_stream(oplocking_file_stream&&) = delete;
	oplocking_file_stream(const oplocking_file_stream&) = delete;
	oplocking_file_stream& operator=(oplocking_file_stream&&) = delete;
	oplocking_file_stream& operator=(const oplocking_file_stream&) = delete;
	~oplocking_file_stream() override {
		CancelIo(m_hFile.get());
		if (m_thOplockWaiter.joinable())
			m_thOplockWaiter.join();
	}

	[[nodiscard]] std::streamsize size() const override {
		if (!try_open_again())
			return 0;

		LARGE_INTEGER fs{};
		GetFileSizeEx(m_hFile.get(), &fs);
		return static_cast<std::streamsize>(fs.QuadPart);
	}

	std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override {
		if (!try_open_again())
			return 0;

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

void* DETOUR_find_existing_resource_handle(void* p1, uint32_t& categoryId, uint32_t& resourceType, uint32_t& resourceHash);

decltype(DETOUR_find_existing_resource_handle)* s_find_existing_resource_handle_original;
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
struct request_t {
	xivres::path_spec RequestedPathSpec;
	xivres::path_spec DetouredPathSpec;
	sqpack_id_t SqpackId;
	int ReadBegin;
	int ReadSize;
	int DatIndex;
	uint64_t SeekOffset;
	xivres::packed::file_header PackHeader;
	std::shared_ptr<xivres::stream> Stream;
};
static std::mutex s_mtx;
static std::optional<xivres::installation> s_installation;
static std::map<sqpack_id_t, xivres::sqpack::generator::sqpack_views> s_sqpackViews;
static std::map<sqpack_id_t, std::map<uint64_t, std::deque<std::shared_ptr<xivres::hotswap_packed_stream>>>> s_allocations;
static std::map<xivres::path_spec, std::weak_ptr<xivres::packed_stream>> s_availableReplacementStreams;
static std::map<HANDLE, xivres::stream*> s_sqpackStreams;
static std::map<HANDLE, uint64_t> s_virtualFilePointers;
static std::filesystem::path s_gameDir;

void* DETOUR_find_existing_resource_handle(void* p1, uint32_t& categoryId, uint32_t& resourceType, uint32_t& resourceHash) {
	const auto retAddr = static_cast<char**>(_AddressOfReturnAddress());
	auto& pszPath = retAddr[0x11];
	// OutputDebugStringA(std::format("cat=0x{:08x} res=0x{:08x} hash=0x{:08x} {}\n", categoryId, resourceType, resourceHash, pszPath).c_str());

	std::unique_lock lock(s_mtx, std::defer_lock);
	sqpack_id_t sqpkId(categoryId);

	try {
		if (!s_installation) {
			std::wstring self(PATHCCH_MAX_CCH, L'\0');
			self.resize(GetModuleFileNameW(GetModuleHandleW(nullptr), &self[0], PATHCCH_MAX_CCH));
			auto recursivePreventer = prevent_recursive_file_operation();
			s_installation.emplace(s_gameDir = std::filesystem::path(self).parent_path());
			recursivePreventer.clear();
		}

		if (!s_sqpackViews.contains(sqpkId)) {
			auto recursivePreventer = prevent_recursive_file_operation();
			auto& reader = s_installation->get_sqpack(sqpkId.packid());
			recursivePreventer.clear();

			xivres::sqpack::generator gen(sqpkId.exname(), sqpkId.name());
			gen.add_sqpack(reader, true, true);

			lock.lock();
			for (const auto [space, count] : std::initializer_list<std::pair<uint32_t, size_t>>{ {16 * 1048576, 256}, {128 * 1048576, 16}, {0x7FFFFFFF, 4} }) {
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
			lock.unlock();

			auto exported = gen.export_to_views(false);

			lock.lock();
			s_sqpackViews.emplace(sqpkId, std::move(exported));
			lock.unlock();
		}

		const auto newPath = s_gameDir / "sqraw" / pszPath;
		if (exists(newPath)) {
			auto ext = newPath.extension().wstring();
			for (auto& c : ext) {
				if (c < 128)
					c = std::tolower(c);
			}

			xivres::path_spec pathSpec(pszPath);
			lock.lock();
			std::shared_ptr<xivres::packed_stream> stream = s_availableReplacementStreams[pathSpec].lock();
			lock.unlock();
			if (!stream) {
				if (ext == L".tex" || ext == L".atex")
					stream = std::make_shared<xivres::passthrough_packed_stream<xivres::texture_passthrough_packer>>(pathSpec, std::make_shared<oplocking_file_stream>(newPath));
				else if (ext == L".mdl")
					stream = std::make_shared<xivres::passthrough_packed_stream<xivres::model_passthrough_packer>>(pathSpec, std::make_shared<oplocking_file_stream>(newPath));
				else
					stream = std::make_shared<xivres::passthrough_packed_stream<xivres::standard_passthrough_packer>>(pathSpec, std::make_shared<oplocking_file_stream>(newPath));

				lock.lock();
				s_availableReplacementStreams[pathSpec] = stream;
				lock.unlock();
			}

			const auto fileSize = static_cast<uint64_t>(stream->size());
			lock.lock();
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
				slot->swap_stream(std::move(stream));
				pszPath = const_cast<char*>(slot->path_spec().path().c_str());
				slots.pop_back();
				slots.emplace_front(std::move(slot));
				break;
			}
			lock.unlock();
		}
	} catch (const std::out_of_range&) {
		// pass
	}

	return s_find_existing_resource_handle_original(p1, categoryId, resourceType, resourceHash);
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

	const auto indexPath = std::filesystem::path(path).replace_extension(".index");
	const auto hFile = s_createfilew_original(indexPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return hFile;

	const auto categoryId = static_cast<uint32_t>(std::wcstoul(path.filename().wstring().substr(0, 2).c_str(), nullptr, 16));
	const auto expacId = static_cast<uint32_t>(std::wcstoul(path.filename().wstring().substr(2, 2).c_str(), nullptr, 16));
	const auto partId = static_cast<uint32_t>(std::wcstoul(path.filename().wstring().substr(4, 2).c_str(), nullptr, 16));
	const auto sqpkId = sqpack_id_t(categoryId, expacId, partId);

	std::unique_lock lock(s_mtx);
	const auto& views = s_sqpackViews.at(sqpkId);
	switch (sqpkType) {
		case -1:
			OutputDebugStringW(std::format(L"Opening {}.win32.index\n", xivres::util::unicode::convert<std::wstring>(sqpkId.name())).c_str());
			s_sqpackStreams[hFile] = views.Index1.get();
			s_virtualFilePointers[hFile] = 0;
			return hFile;

		case -2:
			OutputDebugStringW(std::format(L"Opening {}.win32.index2\n", xivres::util::unicode::convert<std::wstring>(sqpkId.name())).c_str());
			s_sqpackStreams[hFile] = views.Index2.get();
			s_virtualFilePointers[hFile] = 0;
			return hFile;

		default:
			if (sqpkType < 0 || sqpkType >= views.Data.size()) {
				CloseHandle(hFile);
				return INVALID_HANDLE_VALUE;
			}

			OutputDebugStringW(std::format(L"Opening {}.win32.dat{}\n", xivres::util::unicode::convert<std::wstring>(sqpkId.name()), sqpkType).c_str());
			s_sqpackStreams[hFile] = views.Data.at(sqpkType).get();
			s_virtualFilePointers[hFile] = 0;
			return hFile;
	}

	return hFile;
}

BOOL WINAPI DETOUR_SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod) {
	if (s_sqpackStreams.contains(hFile)) {
		std::lock_guard lock(s_mtx);
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
		std::unique_lock lock(s_mtx);
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
		std::unique_lock lock(s_mtx);
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

void do_stuff() {
	MH_CreateHook(find_existing_resource_handle_finder(), &DETOUR_find_existing_resource_handle, (void**)&s_find_existing_resource_handle_original);
	MH_CreateHook(&CreateFileW, &DETOUR_CreateFileW, (void**)&s_createfilew_original);
	MH_CreateHook(&SetFilePointerEx, &DETOUR_SetFilePointerEx, (void**)&s_setfilepointerex_original);
	MH_CreateHook(&ReadFile, &DETOUR_ReadFile, (void**)&s_readfile_original);
	MH_CreateHook(&CloseHandle, &DETOUR_CloseHandle, (void**)&s_closefile_original);
	MH_EnableHook(MH_ALL_HOOKS);
}