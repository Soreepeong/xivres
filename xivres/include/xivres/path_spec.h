#ifndef XIVRES_PATHSPEC_H_
#define XIVRES_PATHSPEC_H_

#include <cinttypes>
#include <format>
#include <string.h>

#include "sqpack.h"

namespace xivres {
	struct path_spec {
		static constexpr uint32_t EmptyHashValue = 0xFFFFFFFFU;
		static constexpr uint8_t EmptyId = 0xFF;
		static constexpr uint32_t SlashHashValue = 0x862C2D2BU;

	private:
		bool m_empty;
		uint8_t m_categoryId;
		uint8_t m_expacId;
		uint8_t m_partId;
		uint32_t m_pathHash;
		uint32_t m_nameHash;
		uint32_t m_fullPathHash;
		mutable std::string m_text;

	public:
		path_spec()
			: m_empty(true)
			, m_categoryId(EmptyId)
			, m_expacId(EmptyId)
			, m_partId(EmptyId)
			, m_pathHash(EmptyHashValue)
			, m_nameHash(EmptyHashValue)
			, m_fullPathHash(EmptyHashValue) {}

		path_spec(path_spec&& r) noexcept
			: m_empty(r.m_empty)
			, m_categoryId(r.m_categoryId)
			, m_expacId(r.m_expacId)
			, m_partId(r.m_partId)
			, m_pathHash(r.m_pathHash)
			, m_nameHash(r.m_nameHash)
			, m_fullPathHash(r.m_fullPathHash)
			, m_text(std::move(r.m_text)) {
			r.clear();
		}

		path_spec(const path_spec& r)
			: m_empty(r.m_empty)
			, m_categoryId(r.m_categoryId)
			, m_expacId(r.m_expacId)
			, m_partId(r.m_partId)
			, m_pathHash(r.m_pathHash)
			, m_nameHash(r.m_nameHash)
			, m_fullPathHash(r.m_fullPathHash)
			, m_text(r.m_text) {}

		path_spec(uint32_t pathHash, uint32_t nameHash, uint32_t fullPathHash, uint8_t categoryId, uint8_t expacId, uint8_t partId)
			: m_empty(false)
			, m_categoryId(categoryId)
			, m_expacId(expacId)
			, m_partId(partId)
			, m_pathHash(pathHash)
			, m_nameHash(nameHash)
			, m_fullPathHash(fullPathHash) {}

		path_spec(const char* fullPath) : path_spec(std::string(fullPath)) {}

		path_spec(std::string fullPath) : path_spec() {
			std::vector<std::span<char>> parts;
			size_t previousOffset = 0, offset;
			while ((offset = fullPath.find_first_of("/\\", previousOffset)) != std::string::npos) {
				auto part = std::span(fullPath).subspan(previousOffset, offset - previousOffset);
				previousOffset = offset + 1;

				if (part.empty() || (part.size() == 1 && part[0] == '.'))
					void();
				else if (part.size() == 2 && part[0] == '.' && part[1] == '.') {
					if (!parts.empty())
						parts.pop_back();
				} else {
					parts.push_back(part);
				}
			}

			if (auto part = std::span(fullPath).subspan(previousOffset); part.empty() || (part.size() == 1 && part[0] == '.'))
				void();
			else if (part.size() == 2 && part[0] == '.' && part[1] == '.') {
				if (!parts.empty())
					parts.pop_back();
			} else {
				parts.push_back(part);
			}

			if (parts.empty())
				return;

			m_empty = false;
			m_text.reserve(std::accumulate(parts.begin(), parts.end(), SIZE_MAX, [](size_t curr, const std::string_view& view) { return curr + view.size() + 1; }));

			m_pathHash = m_nameHash = 0;
			for (size_t i = 0; i < parts.size(); i++) {
				if (i > 0) {
					m_text += "/";
					if (i == 1)
						m_pathHash = m_nameHash;
					else
						m_pathHash = crc32_combine(crc32_combine(m_pathHash, ~SlashHashValue, 1), m_nameHash, static_cast<long>(parts[i - 1].size()));
				}
				m_text += parts[i];
				for (auto& p : parts[i]) {
					if ('A' <= p && p <= 'Z')
						p += 'a' - 'A';
				}
				m_nameHash = crc32_z(0, reinterpret_cast<const uint8_t*>(parts[i].data()), parts[i].size());
			}

			m_fullPathHash = crc32_combine(crc32_combine(m_pathHash, ~SlashHashValue, 1), m_nameHash, parts.empty() ? 0 : static_cast<long>(parts.back().size()));

			m_fullPathHash = ~m_fullPathHash;
			m_pathHash = ~m_pathHash;
			m_nameHash = ~m_nameHash;

			if (!parts.empty()) {
				std::vector<std::string_view> views;
				views.reserve(parts.size());
				for (const auto& part : parts)
					views.emplace_back(part);

				m_expacId = m_partId = 0;

				if (views[0] == "common") {
					m_categoryId = 0x00;

				} else if (views[0] == "bgcommon") {
					m_categoryId = 0x01;

				} else if (views[0] == "bg") {
					m_categoryId = 0x02;
					m_expacId = views.size() >= 2 && views[1].starts_with("ex") ? static_cast<uint8_t>(std::strtol(&views[1][2], nullptr, 10)) : 0;
					m_partId = views.size() >= 3 && m_expacId > 0 ? static_cast<uint8_t>(std::strtol(&views[2][0], nullptr, 10)) : 0;

				} else if (views[0] == "cut") {
					m_categoryId = 0x03;
					m_expacId = views.size() >= 2 && views[1].starts_with("ex") ? static_cast<uint8_t>(std::strtol(&views[1][2], nullptr, 10)) : 0;

				} else if (views[0] == "chara") {
					m_categoryId = 0x04;

				} else if (views[0] == "shader") {
					m_categoryId = 0x05;

				} else if (views[0] == "ui") {
					m_categoryId = 0x06;

				} else if (views[0] == "sound") {
					m_categoryId = 0x07;

				} else if (views[0] == "vfx") {
					m_categoryId = 0x08;

				} else if (views[0] == "exd") {
					m_categoryId = 0x0a;

				} else if (views[0] == "game_script") {
					m_categoryId = 0x0b;

				} else if (views[0] == "music") {
					m_categoryId = 0x0c;
					m_expacId = views.size() >= 2 && views[1].starts_with("ex") ? static_cast<uint8_t>(std::strtol(&views[1][2], nullptr, 10)) : 0;

				} else
					m_categoryId = 0x00;
			}
		}

		path_spec& operator=(path_spec&& r) noexcept {
			m_empty = r.m_empty;
			m_categoryId = r.m_categoryId;
			m_expacId = r.m_expacId;
			m_partId = r.m_partId;
			m_fullPathHash = r.m_fullPathHash;
			m_pathHash = r.m_pathHash;
			m_nameHash = r.m_nameHash;
			m_text = std::move(r.m_text);
			r.clear();
			return *this;
		}

		path_spec& operator=(const path_spec& r) {
			m_empty = r.m_empty;
			m_categoryId = r.m_categoryId;
			m_expacId = r.m_expacId;
			m_partId = r.m_partId;
			m_fullPathHash = r.m_fullPathHash;
			m_pathHash = r.m_pathHash;
			m_nameHash = r.m_nameHash;
			m_text = r.m_text;
			return *this;
		}

		void clear() noexcept {
			m_text.clear();
			m_fullPathHash = m_pathHash = m_nameHash = EmptyHashValue;
		}

		void update_text_if_same(const path_spec& r) const {
			if (!has_original() && r.has_original() && *this == r)
				this->m_text = r.m_text;
		}

		[[nodiscard]] bool has_original() const {
			return !m_text.empty();
		}

		[[nodiscard]] bool empty() const {
			return m_empty;
		}

		[[nodiscard]] uint8_t category_id() const {
			return m_categoryId;
		}

		[[nodiscard]] uint8_t expac_id() const {
			return m_expacId;
		}

		[[nodiscard]] uint8_t part_id() const {
			return m_partId;
		}

		[[nodiscard]] uint32_t path_hash() const {
			return m_pathHash;
		}

		[[nodiscard]] uint32_t name_hash() const {
			return m_nameHash;
		}

		[[nodiscard]] uint32_t full_path_hash() const {
			return m_fullPathHash;
		}

		[[nodiscard]] const std::string& path() const {
			return m_text;
		}

		[[nodiscard]] std::string exname() const {
			if (m_expacId == 0)
				return "ffxiv";
			else
				return std::format("ex{}", m_expacId);
		}

		[[nodiscard]] uint32_t packid() const {
			return (m_categoryId << 16) | (m_expacId << 8) | m_partId;
		}

		[[nodiscard]] std::string packname() const {
			return std::format("{:0>6x}", packid());
		}

		bool operator==(const path_spec& r) const {
			if (m_empty && r.m_empty)
				return true;

			return m_categoryId == r.m_categoryId
				&& m_expacId == r.m_expacId
				&& m_partId == r.m_partId
				&& m_fullPathHash == r.m_fullPathHash
				&& m_nameHash == r.m_nameHash
				&& m_pathHash == r.m_pathHash
				&& (m_text.empty() || r.m_text.empty() || FullPathComparator::Compare(*this, r) == 0);
		}

		bool operator!=(const path_spec& r) const {
			return !this->operator==(r);
		}

		struct AllComparator {
			static int Compare(const path_spec& l, const path_spec& r) {
				if (l.m_empty && r.m_empty)
					return 0;
				if (l.m_empty && !r.m_empty)
					return -1;
				if (!l.m_empty && r.m_empty)
					return 1;
				if (!l.m_text.empty() && !r.m_text.empty())
					return l.m_text < r.m_text ? -1 : (l.m_text == r.m_text ? 0 : 1);
				if (l.m_text.empty() && !r.m_text.empty())
					return -1;
				if (!l.m_text.empty() && r.m_text.empty())
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
				return Compare(l, r) < 0;
			}
		};

		bool operator <(const path_spec& r) const {
			return AllComparator().Compare(*this, r) < 0;
		}

		struct AllHashComparator {
			static int Compare(const path_spec& l, const path_spec& r) {
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
				return Compare(l, r) < 0;
			}
		};

		struct FullHashComparator {
			static int Compare(const path_spec& l, const path_spec& r) {
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
				return Compare(l, r) < 0;
			}
		};

		struct PairHashComparator {
			static int Compare(const path_spec& l, const path_spec& r) {
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
				return Compare(l, r) < 0;
			}
		};

		struct FullPathComparator {
			static int Compare(const path_spec& l, const path_spec& r) {
				if (l.m_empty && r.m_empty)
					return 0;
				if (l.m_empty && !r.m_empty)
					return -1;
				if (!l.m_empty && r.m_empty)
					return 1;
				for (size_t i = 0; i < l.m_text.size() && i < r.m_text.size(); i++) {
					const auto x = std::tolower(l.m_text[i]);
					const auto y = std::tolower(r.m_text[i]);
					if (x < y)
						return -1;
					if (x > y)
						return 1;
				}
				if (l.m_text.size() < r.m_text.size())
					return -1;
				if (l.m_text.size() > r.m_text.size())
					return 1;
				return 0;
			}

			bool operator()(const path_spec& l, const path_spec& r) const {
				return Compare(l, r) < 0;
			}
		};

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
				return _strcmpi(l.FullPath, rt);
			}

			bool operator()(const char* lt, const sqpack::sqindex::pair_hash_with_text_locator& r) const {
				return _strcmpi(lt, r.FullPath);
			}

			bool operator()(const sqpack::sqindex::full_hash_with_text_locator& l, const char* rt) const {
				return _strcmpi(l.FullPath, rt);
			}

			bool operator()(const char* lt, const sqpack::sqindex::full_hash_with_text_locator& r) const {
				return _strcmpi(lt, r.FullPath);
			}
		};
	};
}

template<>
struct std::formatter<xivres::path_spec, char> : std::formatter<std::basic_string<char>, char> {
	template<class FormatContext>
	auto format(const xivres::path_spec& t, FormatContext& fc) {
		return std::formatter<std::basic_string<char>, char>::format(std::format("{}({:08x}/{:08x}, {:08x})", t.path(), t.path_hash(), t.name_hash(), t.full_path_hash()), fc);
	}
};

#endif
