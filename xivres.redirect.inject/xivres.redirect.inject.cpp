#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <Windows.h>
#include <Psapi.h>
#include <string>
#include <PathCch.h>
#include <filesystem>

int main() {
	std::wstring mypath;
	DWORD mypathlen = PATHCCH_MAX_CCH;
	mypath.resize(mypathlen);
	QueryFullProcessImageNameW(GetCurrentProcess(), 0, &mypath[0], &mypathlen);
	mypath.resize(mypathlen);
	mypath = (std::filesystem::path(mypath).parent_path() / "xivres.redirect.dll").wstring();


	for (HWND hwnd = nullptr; nullptr != (hwnd = FindWindowExW(nullptr, nullptr, L"FFXIVGAME", nullptr));) {
		DWORD pid{};
		GetWindowThreadProcessId(hwnd, &pid);
		if (!pid)
			continue;

		const auto hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
		if (!hProcess)
			continue;

		const auto param = VirtualAllocEx(hProcess, nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		size_t wr{};
		WriteProcessMemory(hProcess, param, &mypath[0], 2 * mypath.size() + 2, &wr);
		WaitForSingleObject(CreateRemoteThread(hProcess, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(&LoadLibraryW), param, 0, nullptr), INFINITE);
		VirtualFreeEx(hProcess, param, 0, MEM_RELEASE);
		CloseHandle(hProcess);
	}

	return 0;
}
