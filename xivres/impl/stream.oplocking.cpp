#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winioctl.h>

#include <mutex>
#include <system_error>
#include <thread>

#include "../include/xivres/stream.oplocking.h"

struct xivres::oplocking_file_stream::data {
	const std::filesystem::path Path;
	const bool ReopenOnChange;

	mutable std::shared_ptr<std::remove_pointer_t<HANDLE>> hFile;
	std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&CloseHandle)> hOplockEvent;

	mutable std::streamsize Size = 0;
	mutable std::mutex OpenMtx;
	mutable OVERLAPPED OvOplock{};
	mutable std::thread ThOplockWaiter;

	mutable REQUEST_OPLOCK_INPUT_BUFFER InOplock = {
		REQUEST_OPLOCK_CURRENT_VERSION,
		sizeof(InOplock),
		OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_HANDLE,
		REQUEST_OPLOCK_INPUT_FLAG_REQUEST,
	};

	mutable REQUEST_OPLOCK_OUTPUT_BUFFER OutOplock = {
		REQUEST_OPLOCK_CURRENT_VERSION,
		sizeof(OutOplock),
	};

	data(std::filesystem::path path, bool reopenOnChange)
		: Path(std::move(path))
		, ReopenOnChange(reopenOnChange)
		, hOplockEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr), &CloseHandle) {
		if (!hOplockEvent)
			throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()));

		OvOplock.hEvent = hOplockEvent.get();
	}

	void fire_oplock() const {
		const auto requested = DeviceIoControl(hFile.get(), FSCTL_REQUEST_OPLOCK,
			&InOplock, sizeof(InOplock),
			&OutOplock, sizeof(OutOplock),
			nullptr, &OvOplock);
		if (!requested && GetLastError() == ERROR_IO_PENDING) {
			if (ThOplockWaiter.joinable())
				ThOplockWaiter.join();

			ThOplockWaiter = std::thread([this]() {
				DWORD dwBytes{};
				if (!GetOverlappedResult(hFile.get(), &OvOplock, &dwBytes, TRUE)) {
					if (GetLastError() == ERROR_CANCELLED)
						return;
				}

				hFile.reset();
			});
		}
	}
};

xivres::oplocking_file_stream::oplocking_file_stream(std::filesystem::path path, bool reopenOnChange)
	: m_data(std::make_unique<data>(std::move(path), reopenOnChange)) {
	open();
}

xivres::oplocking_file_stream::~oplocking_file_stream() {
	CancelIoEx(m_data->hFile.get(), &m_data->OvOplock);
	if (m_data->ThOplockWaiter.joinable())
		m_data->ThOplockWaiter.join();
}

const std::filesystem::path& xivres::oplocking_file_stream::path() const {
	return m_data->Path;
}

void xivres::oplocking_file_stream::open() const {
	if (m_data->hFile)
		return;

	const auto lock = std::lock_guard(m_data->OpenMtx);

	if (const auto h = CreateFileW(m_data->Path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr); h != INVALID_HANDLE_VALUE) {
		m_data->hFile = {h, &CloseHandle};

		LARGE_INTEGER fs{};
		GetFileSizeEx(m_data->hFile.get(), &fs);
		m_data->Size = static_cast<std::streamsize>(fs.QuadPart);

		m_data->fire_oplock();
	}
}

void xivres::oplocking_file_stream::invalidate() {
	CancelIoEx(m_data->hFile.get(), &m_data->OvOplock);
	m_data->hFile.reset();
}

bool xivres::oplocking_file_stream::done() const {
	if (m_data->ReopenOnChange)
		open();

	return !m_data->hFile;
}

std::streamsize xivres::oplocking_file_stream::size() const {
	if (m_data->ReopenOnChange)
		open();

	return m_data->Size;
}

std::streamsize xivres::oplocking_file_stream::read(std::streamoff offset, void* buf, std::streamsize length) const {
	if (m_data->ReopenOnChange)
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
	}

	DWORD readLength = 0;
	OVERLAPPED ov{};
	const auto hEvent = std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(CloseHandle)*>(CreateEventW(nullptr, FALSE, FALSE, nullptr), &CloseHandle);
	if (!hEvent)
		throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()));

	ov.hEvent = hEvent.get();
	ov.Offset = static_cast<DWORD>(offset);
	ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
	if (!ReadFile(m_data->hFile.get(), buf, static_cast<DWORD>(length), &readLength, &ov)) {
		const auto err = GetLastError();
		if (err == ERROR_HANDLE_EOF)
			return 0;
		if (err != ERROR_IO_PENDING)
			throw std::system_error(std::error_code(static_cast<int>(err), std::system_category()));
	}

	// Discarding this leaves readLength at zero along with the reason, which upstream cannot
	// tell apart from a legitimately empty read.
	if (!GetOverlappedResult(m_data->hFile.get(), &ov, &readLength, TRUE)) {
		const auto err = GetLastError();
		if (err != ERROR_HANDLE_EOF)
			throw std::system_error(std::error_code(static_cast<int>(err), std::system_category()));
		return 0;
	}
	return readLength;
}
