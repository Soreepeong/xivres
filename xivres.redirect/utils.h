#pragma once

namespace utils {
	class loaded_module {
		HMODULE m_hModule;
	public:
		loaded_module() : m_hModule(nullptr) {}
		loaded_module(const void* hModule) : m_hModule(reinterpret_cast<HMODULE>(const_cast<void*>(hModule))) {}
		loaded_module(void* hModule) : m_hModule(reinterpret_cast<HMODULE>(hModule)) {}
		loaded_module(size_t hModule) : m_hModule(reinterpret_cast<HMODULE>(hModule)) {}

		std::filesystem::path path() const;

		bool is_current_process() const { return m_hModule == GetModuleHandleW(nullptr); }
		bool owns_address(const void* pAddress) const;

		operator HMODULE() const {
			return m_hModule;
		}

		size_t address_int() const { return reinterpret_cast<size_t>(m_hModule); }
		size_t image_size() const { return is_pe64() ? nt_header64().OptionalHeader.SizeOfImage : nt_header32().OptionalHeader.SizeOfImage; }
		char* address(size_t offset = 0) const { return reinterpret_cast<char*>(m_hModule) + offset; }
		template<typename T> T* address_as(size_t offset) const { return reinterpret_cast<T*>(address(offset)); }
		template<typename T> std::span<T> span_as(size_t offset, size_t count) const { return std::span<T>(reinterpret_cast<T*>(address(offset)), count); }
		template<typename T> T& ref_as(size_t offset) const { return *reinterpret_cast<T*>(address(offset)); }

		IMAGE_DOS_HEADER& dos_header() const { return ref_as<IMAGE_DOS_HEADER>(0); }
		IMAGE_NT_HEADERS32& nt_header32() const { return ref_as<IMAGE_NT_HEADERS32>(dos_header().e_lfanew); }
		IMAGE_NT_HEADERS64& nt_header64() const { return ref_as<IMAGE_NT_HEADERS64>(dos_header().e_lfanew); }
		bool is_pe64() const { return nt_header32().OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC; }

		std::span<IMAGE_DATA_DIRECTORY> data_directories() const { return is_pe64() ? nt_header64().OptionalHeader.DataDirectory : nt_header32().OptionalHeader.DataDirectory; }
		IMAGE_DATA_DIRECTORY& data_directory(size_t index) const { return data_directories()[index]; }

		std::span<IMAGE_SECTION_HEADER> section_headers() const;
		IMAGE_SECTION_HEADER& section_header(const char* pcszSectionName) const;
		std::span<char> section(size_t index) const;
		std::span<char> section(const char* pcszSectionName) const;

		template<typename TFn> TFn* get_exported_function(const char* pcszFunctionName) {
			const auto pAddress = GetProcAddress(m_hModule, pcszFunctionName);
			if (!pAddress)
				throw std::out_of_range(std::format("Exported function \"{}\" not found.", pcszFunctionName));
			return reinterpret_cast<TFn*>(pAddress);
		}

		bool find_imported_function_pointer(const char* pcszDllName, const char* pcszFunctionName, uint32_t hintOrOrdinal, void*& ppFunctionAddress) const;
		void* get_imported_function_pointer(const char* pcszDllName, const char* pcszFunctionName, uint32_t hintOrOrdinal) const;
		template<typename TFn> TFn** get_imported_function_pointer(const char* pcszDllName, const char* pcszFunctionName, uint32_t hintOrOrdinal) { return reinterpret_cast<TFn**>(get_imported_function_pointer(pcszDllName, pcszFunctionName, hintOrOrdinal)); }

		static loaded_module current_process();
		static std::vector<loaded_module> all_modules();
	};

	class signature_finder {
		std::vector<std::span<const char>> m_ranges;
		std::vector<srell::regex> m_patterns;

	public:
		signature_finder& look_in(const void* pFirst, size_t length);
		signature_finder& look_in(const loaded_module& m, const char* sectionName);

		template<typename T>
		signature_finder& look_in(std::span<T> s) {
			return look_in(s.data(), s.size());
		}

		signature_finder& look_for(std::string_view pattern, std::string_view mask, char cExactMatch = 'x', char cWildcard = '.');
		signature_finder& look_for(std::string_view pattern, char wildcardMask);
		signature_finder& look_for(std::string_view pattern);
		signature_finder& look_for_hex(std::string_view pattern);

		template<size_t len>
		signature_finder& look_for(char pattern[len]) {
			static_assert(len == 5);
		}

		struct result {
			std::span<const char> Match;
			size_t PatternIndex;
			size_t MatchIndex;
			size_t CaptureIndex;
		};

		std::vector<result> find(size_t minCount, size_t maxCount, bool bErrorOnMoreThanMaximum) const;

		std::span<const char> find_one() const;
	};

	class memory_tenderizer {
		std::span<char> m_data;
		std::vector<MEMORY_BASIC_INFORMATION> m_regions;

	public:
		memory_tenderizer(const void* pAddress, size_t length, DWORD dwNewProtect);

		template<typename T, typename = std::enable_if_t<std::is_trivial_v<T>&& std::is_standard_layout_v<T>>>
		memory_tenderizer(const T& object, DWORD dwNewProtect) : memory_tenderizer(&object, sizeof T, dwNewProtect) {}

		template<typename T>
		memory_tenderizer(std::span<const T> s, DWORD dwNewProtect) : memory_tenderizer(&s[0], s.size(), dwNewProtect) {}

		template<typename T>
		memory_tenderizer(std::span<T> s, DWORD dwNewProtect) : memory_tenderizer(&s[0], s.size(), dwNewProtect) {}

		~memory_tenderizer();
	};
}
