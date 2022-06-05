#include "pch.h"

void preload_stuff();
void do_stuff();

HMODULE g_hModule;

HRESULT WINAPI FORWARDER_DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter) {
	static const auto s_hDInput8 = LoadLibraryW(LR"(C:\Windows\System32\dinput8.dll)");
	static const auto s_pfnDirectInput8Create = reinterpret_cast<decltype(DirectInput8Create)*>(GetProcAddress(s_hDInput8, "DirectInput8Create"));
	do_stuff();
	return s_pfnDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

static void antidebug() {
	DWORD old;
	VirtualProtect(&IsDebuggerPresent, 4, PAGE_EXECUTE_READWRITE, &old);
	*reinterpret_cast<int*>(&IsDebuggerPresent) = 0xc3c03148;
	VirtualProtect(&IsDebuggerPresent, 4, old, &old);

	VirtualProtect(&OpenProcess, 4, PAGE_EXECUTE_READWRITE, &old);
	*reinterpret_cast<int*>(&OpenProcess) = 0xc3c03148;
	VirtualProtect(&OpenProcess, 4, old, &old);

	if (IsDebuggerPresent())
		__debugbreak();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
		case DLL_PROCESS_ATTACH:
			g_hModule = hModule;
			DisableThreadLibraryCalls(hModule);
			MH_Initialize();
			antidebug();
			std::thread([]() { preload_stuff(); }).detach();
			return TRUE;
		
		case DLL_PROCESS_DETACH:
			MH_Uninitialize();
			return TRUE;
	}

	return TRUE;
}
