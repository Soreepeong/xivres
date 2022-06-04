#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800

#include <Windows.h>
#include <filesystem>
#include <format>
#include <array>
#include <map>
#include <minhook.h>
#include <span>
#include <deque>
#include <set>
#include <intrin.h>
#include <dinput.h>
#include <PathCch.h>
#include <winioctl.h>
#include <stdio.h>

extern HMODULE g_hModule;
