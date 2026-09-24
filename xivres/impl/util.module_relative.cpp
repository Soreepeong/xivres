#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filesystem>
#include <utility>

#include "../include/xivres/util.module_relative.h"

std::string xivres::util::module_relative::to_string() const {
	HMODULE hModule{};
	if (!GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		static_cast<LPCWSTR>(Address),
		&hModule))
		return std::format("{:#x}", reinterpret_cast<uintptr_t>(Address));

	std::wstring path(MAX_PATH, L'\0');
	while (true) {
		const auto len = GetModuleFileNameW(hModule, path.data(), static_cast<DWORD>(path.size()));
		if (!len)
			return std::format("{:#x}", reinterpret_cast<uintptr_t>(Address));
		if (len < path.size()) {
			path.resize(len);
			break;
		}
		path.resize(path.size() * 2);
	}

	const auto base = reinterpret_cast<const char*>(hModule);
	const auto offset = static_cast<const char*>(Address) - base;
	const auto name = std::filesystem::path(path).filename().string();

	const auto& dosHeader = *reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE)
		return std::format("{}+{:#x}", name, offset);

	const auto& ntHeader = *reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dosHeader.e_lfanew);
	if (ntHeader.Signature != IMAGE_NT_SIGNATURE)
		return std::format("{}+{:#x}", name, offset);

	const auto sections = IMAGE_FIRST_SECTION(&ntHeader);
	for (size_t i = 0; i < ntHeader.FileHeader.NumberOfSections; ++i) {
		const auto& section = sections[i];
		if (std::cmp_less(offset, section.VirtualAddress) || std::cmp_greater_equal(offset, section.VirtualAddress + section.Misc.VirtualSize))
			continue;

		const auto raw = reinterpret_cast<const char*>(section.Name);
		return std::format("{}+{:#x}[{}]", name, offset, std::string_view(raw, strnlen(raw, IMAGE_SIZEOF_SHORT_NAME)));
	}

	return std::format("{}+{:#x}", name, offset);
}
