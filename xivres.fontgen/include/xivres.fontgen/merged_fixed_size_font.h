#ifndef XIVRES_FONTGENERATOR_MERGEDFIXEDSIZEFONT_H_
#define XIVRES_FONTGENERATOR_MERGEDFIXEDSIZEFONT_H_

#include "fixed_size_font.h"

namespace xivres::fontgen {
	enum class vertical_alignment {
		Top,
		Middle,
		Baseline,
		Bottom,
	};

	enum class codepoint_merge_mode {
		AddNew,
		AddAll,
		Replace,
		Enum_Count_,
	};

	class merged_fixed_size_font : public default_abstract_fixed_size_font {
		struct info_t {
			std::set<char32_t> Codepoints;
			std::map<char32_t, size_t> UsedFontIndices;
			float Size{};
			int Ascent{};
			int LineHeight{};
			vertical_alignment Alignment = vertical_alignment::Baseline;
		};

		std::vector<std::shared_ptr<fixed_size_font>> m_fonts;
		std::shared_ptr<const info_t> m_info;
		mutable std::optional<std::map<std::pair<char32_t, char32_t>, int>> m_kerningPairs;

	public:
		merged_fixed_size_font();
		merged_fixed_size_font(merged_fixed_size_font&&) noexcept;
		merged_fixed_size_font(const merged_fixed_size_font&);
		merged_fixed_size_font& operator=(merged_fixed_size_font&&) noexcept;
		merged_fixed_size_font& operator=(const merged_fixed_size_font&);
		~merged_fixed_size_font() override = default;

		merged_fixed_size_font(std::vector<std::pair<std::shared_ptr<fixed_size_font>, codepoint_merge_mode>> fonts, vertical_alignment verticalAlignment = vertical_alignment::Baseline);

		[[nodiscard]] std::string family_name() const override;

		[[nodiscard]] std::string subfamily_name() const override;

		[[nodiscard]] float font_size() const override;

		[[nodiscard]] int ascent() const override;

		[[nodiscard]] int line_height() const override;

		[[nodiscard]] const std::set<char32_t>& all_codepoints() const override;

		[[nodiscard]] const void* get_base_font_glyph_uniqid(char32_t c) const override;

		[[nodiscard]] char32_t uniqid_to_glyph(const void* pc) const override;

		[[nodiscard]] bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const override;

		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const override;

		bool draw(char32_t codepoint, util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::b8g8r8a8 fgColor, util::b8g8r8a8 bgColor) const override;

		bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		[[nodiscard]] std::shared_ptr<fixed_size_font> get_threadsafe_view() const override;

		[[nodiscard]] const fixed_size_font* get_base_font(char32_t codepoint) const override;

	private:
		[[nodiscard]] static int get_vertical_adjustment(const info_t& info, const fixed_size_font& font);
	};
}

#endif
