#ifndef XIVRES_PATHSPEC_H_
#define XIVRES_PATHSPEC_H_

#include <cinttypes>
#include <filesystem>
#include <format>
#include <string>

#include "sqpack.h"
#include "util.unicode.h"

namespace xivres {
	struct path_spec {
		static constexpr uint32_t EmptyHashValue = 0xFFFFFFFFU;
		static constexpr uint8_t EmptyId = 0xFF;
		static constexpr uint32_t SlashHashValue = 0x862C2D2BU;

	private:
		bool m_empty = true;
		uint8_t m_categoryId = EmptyId;
		uint8_t m_expacId = EmptyId;
		uint8_t m_partId = EmptyId;
		uint32_t m_pathHash = EmptyHashValue;
		uint32_t m_nameHash = EmptyHashValue;
		uint32_t m_fullPathHash = EmptyHashValue;
		std::string m_text;

	public:
		path_spec() noexcept = default;
		path_spec& operator=(const path_spec& r) = default;
		path_spec(const path_spec& r) = default;
		~path_spec() = default;

		path_spec(path_spec&& r) noexcept { swap(*this, r); }
		path_spec& operator=(path_spec&& r) noexcept { swap(*this, r); return *this; }
		
		path_spec(uint32_t pathHash, uint32_t nameHash, uint32_t fullPathHash, uint8_t categoryId, uint8_t expacId, uint8_t partId)
			: m_empty(false)
			, m_categoryId(categoryId)
			, m_expacId(expacId)
			, m_partId(partId)
			, m_pathHash(pathHash)
			, m_nameHash(nameHash)
			, m_fullPathHash(fullPathHash) {}

		template<typename TElem>
		path_spec(const TElem* fullPath) : path_spec(util::unicode::convert<std::string>(fullPath)) {}

		template<typename TElem, typename TTrait = std::char_traits<TElem>>
		path_spec(std::basic_string_view<TElem, TTrait> fullPath) : path_spec(util::unicode::convert<std::string>(fullPath)) {}

		template<typename TElem, typename TTrait = std::char_traits<TElem>, typename TAlloc = std::allocator<TElem>>
		path_spec(const std::basic_string<TElem, TTrait, TAlloc>& fullPath) : path_spec(util::unicode::convert<std::string>(fullPath)) {}

		path_spec(std::string fullPath);

		path_spec(const std::filesystem::path& path) : path_spec(path.u8string()) {}

		friend void swap(path_spec& l, path_spec& r) noexcept {
			std::swap(l.m_empty, r.m_empty);
			std::swap(l.m_categoryId, r.m_categoryId);
			std::swap(l.m_expacId, r.m_expacId);
			std::swap(l.m_partId, r.m_partId);
			std::swap(l.m_pathHash, r.m_pathHash);
			std::swap(l.m_nameHash, r.m_nameHash);
			std::swap(l.m_fullPathHash, r.m_fullPathHash);
			std::swap(l.m_text, r.m_text);
		}

		void clear() noexcept {
			path_spec empty;
			swap(empty, *this);
		}

		void update_text_if_same(const path_spec& r) { if (!has_original() && r.has_original() && *this == r) m_text = r.m_text; }

		operator bool() const { return m_empty; }
		[[nodiscard]] bool empty() const { return m_empty; }

		[[nodiscard]] uint8_t category_id() const { return m_categoryId; }
		[[nodiscard]] uint8_t expac_id() const { return m_expacId; }
		[[nodiscard]] uint8_t part_id() const { return m_partId; }
		[[nodiscard]] uint32_t path_hash() const { return m_pathHash; }
		[[nodiscard]] uint32_t name_hash() const { return m_nameHash; }
		[[nodiscard]] uint32_t full_path_hash() const { return m_fullPathHash; }

		[[nodiscard]] bool has_original() const { return !m_text.empty(); }
		[[nodiscard]] const std::string& text() const { return m_text; }
		[[nodiscard]] path_spec textless() const { return !has_original() ? *this : path_spec(m_pathHash, m_nameHash, m_fullPathHash, m_categoryId, m_expacId, m_partId); }

		[[nodiscard]] uint32_t packid() const { return (m_categoryId << 16) | (m_expacId << 8) | m_partId; }
		[[nodiscard]] std::string packname() const { return std::format("{:0>6x}", packid()); }
		[[nodiscard]] std::string exname() const { return m_expacId == 0 ? "ffxiv" : std::format("ex{}", m_expacId); }

		bool operator==(const path_spec& r) const {
			if (m_empty && r.m_empty)
				return true;

			return m_categoryId == r.m_categoryId
				&& m_expacId == r.m_expacId
				&& m_partId == r.m_partId
				&& m_fullPathHash == r.m_fullPathHash
				&& m_nameHash == r.m_nameHash
				&& m_pathHash == r.m_pathHash
				&& (m_text.empty() || r.m_text.empty() || FullPathComparator::compare(*this, r) == 0);
		}

		bool operator!=(const path_spec& r) const {
			return !this->operator==(r);
		}

		struct AllHashComparator {
			static int compare(const path_spec& l, const path_spec& r) {
				if (l.m_empty && r.m_empty)
					return 0;
				if (l.m_empty && !r.m_empty)
					return -1;
				if (!l.m_empty && r.m_empty)
					return 1;
				if (l.m_fullPathHash < r.m_fullPathHash)
					return -1;
				if (l.m_fullPathHash > r.m_fullPathHash)
					return 1;
				if (l.m_pathHash < r.m_pathHash)
					return -1;
				if (l.m_pathHash > r.m_pathHash)
					return 1;
				if (l.m_nameHash < r.m_nameHash)
					return -1;
				if (l.m_nameHash > r.m_nameHash)
					return 1;
				return 0;
			}

			bool operator()(const path_spec& l, const path_spec& r) const {
				return compare(l, r) < 0;
			}
		};

		struct FullHashComparator {
			static int compare(const path_spec& l, const path_spec& r) {
				if (l.m_empty && r.m_empty)
					return 0;
				if (l.m_empty && !r.m_empty)
					return -1;
				if (!l.m_empty && r.m_empty)
					return 1;
				if (l.m_fullPathHash < r.m_fullPathHash)
					return -1;
				if (l.m_fullPathHash > r.m_fullPathHash)
					return 1;
				return 0;
			}

			bool operator()(const path_spec& l, const path_spec& r) const {
				return compare(l, r) < 0;
			}
		};

		struct PairHashComparator {
			static int compare(const path_spec& l, const path_spec& r) {
				if (l.m_empty && r.m_empty)
					return 0;
				if (l.m_empty && !r.m_empty)
					return -1;
				if (!l.m_empty && r.m_empty)
					return 1;
				if (l.m_pathHash < r.m_pathHash)
					return -1;
				if (l.m_pathHash > r.m_pathHash)
					return 1;
				if (l.m_nameHash < r.m_nameHash)
					return -1;
				if (l.m_nameHash > r.m_nameHash)
					return 1;
				return 0;
			}

			bool operator()(const path_spec& l, const path_spec& r) const {
				return compare(l, r) < 0;
			}
		};

		struct FullPathComparator {
			static int compare(const path_spec& l, const path_spec& r) {
				if (l.m_empty && r.m_empty)
					return 0;
				if (l.m_empty && !r.m_empty)
					return -1;
				if (!l.m_empty && r.m_empty)
					return 1;

				return util::unicode::strcmp(l.m_text.c_str(), r.m_text.c_str(), &util::unicode::lower, l.m_text.size() + 1, r.m_text.size() + 1);
			}

			bool operator()(const path_spec& l, const path_spec& r) const {
				return compare(l, r) < 0;
			}
		};

		struct AllComparator {
			static int compare(const path_spec& l, const path_spec& r) {
				if (const auto res = AllHashComparator::compare(l, r))
					return res;
				if (const auto res = FullPathComparator::compare(l, r))
					return res;
				return 0;
			}

			bool operator()(const path_spec& l, const path_spec& r) const {
				return compare(l, r) < 0;
			}
		};

		std::weak_ordering operator <=>(const path_spec& r) const {
			const auto c = AllComparator::compare(*this, r);
			if (c < 0)
				return std::weak_ordering::less;
			else if (c == 0)
				return std::weak_ordering::equivalent;
			else
				return std::weak_ordering::greater;
		}

		struct LocatorComparator {
			bool operator()(const sqpack::sqindex::pair_hash_locator& l, uint32_t r) const {
				return l.NameHash < r;
			}

			bool operator()(uint32_t l, const sqpack::sqindex::pair_hash_locator& r) const {
				return l < r.NameHash;
			}

			bool operator()(const sqpack::sqindex::path_hash_locator& l, uint32_t r) const {
				return l.PathHash < r;
			}

			bool operator()(uint32_t l, const sqpack::sqindex::path_hash_locator& r) const {
				return l < r.PathHash;
			}

			bool operator()(const sqpack::sqindex::full_hash_locator& l, uint32_t r) const {
				return l.FullPathHash < r;
			}

			bool operator()(uint32_t l, const sqpack::sqindex::full_hash_locator& r) const {
				return l < r.FullPathHash;
			}

			bool operator()(const sqpack::sqindex::pair_hash_with_text_locator& l, const char* rt) const {
				return util::unicode::strcmp(l.FullPath, rt, &util::unicode::lower, sizeof l.FullPath);
			}

			bool operator()(const char* lt, const sqpack::sqindex::pair_hash_with_text_locator& r) const {
				return util::unicode::strcmp(lt, r.FullPath, &util::unicode::lower, sizeof r.FullPath);
			}

			bool operator()(const sqpack::sqindex::full_hash_with_text_locator& l, const char* rt) const {
				return util::unicode::strcmp(l.FullPath, rt, &util::unicode::lower, sizeof l.FullPath);
			}

			bool operator()(const char* lt, const sqpack::sqindex::full_hash_with_text_locator& r) const {
				return util::unicode::strcmp(lt, r.FullPath, &util::unicode::lower, sizeof r.FullPath);
			}
		};

		static std::string required_prefix(uint32_t categoryId, uint32_t expacId, uint32_t partId);

		static std::string required_prefix(uint32_t packId) {
			return required_prefix(packId >> 16, (packId >> 8) & 0xFF, packId & 0xFF);
		}
	};
}

template<>
struct std::formatter<xivres::path_spec, char> : std::formatter<std::basic_string<char>, char> {
	template<class FormatContext>
	auto format(const xivres::path_spec& t, FormatContext& fc) {
		if (t.has_original()) {
			return std::formatter<std::basic_string<char>, char>::format(std::format(
				"{}({:08x}/{:08x}, {:08x})", t.text(), t.path_hash(), t.name_hash(), t.full_path_hash()), fc);
		} else {
			return std::formatter<std::basic_string<char>, char>::format(std::format(
				R"(???({:08x}/{:08x}, {:08x}))", t.path_hash(), t.name_hash(), t.full_path_hash()), fc);
		}
	}
};

#endif
