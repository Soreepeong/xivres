#ifndef _XIVRES_FONTGENERATOR_CODEPOINTLIMITINGFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_CODEPOINTLIMITINGFIXEDSIZEFONT_H_

#include "fixed_size_font.h"

namespace xivres::fontgen {
	struct wrap_modifiers {
		std::vector<std::pair<char32_t, char32_t>> Codepoints;
		std::map<char32_t, char32_t> CodepointReplacements;
		int LetterSpacing = 0;
		int HorizontalOffset = 0;
		int BaselineShift = 0;
	};

	class wrapping_fixed_size_font : public default_abstract_fixed_size_font {
		struct info_t {
			std::set<char32_t> Codepoints;
			std::map<char32_t, char32_t> MappedCodepoints;
			int LetterSpacing = 0;
			int HorizontalOffset = 0;
			int BaselineShift = 0;
		};

		std::shared_ptr<const fixed_size_font> m_font;
		std::shared_ptr<const info_t> m_info;

		mutable std::optional<std::map<std::pair<char32_t, char32_t>, int>> m_kerningPairs;

	public:
		wrapping_fixed_size_font(std::shared_ptr<const fixed_size_font> font, const wrap_modifiers& wrapModifiers);

		wrapping_fixed_size_font();
		wrapping_fixed_size_font(const wrapping_fixed_size_font& r);
		wrapping_fixed_size_font(wrapping_fixed_size_font&& r) noexcept;
		wrapping_fixed_size_font& operator=(const wrapping_fixed_size_font& r);
		wrapping_fixed_size_font& operator=(wrapping_fixed_size_font&& r) noexcept;

		std::string family_name() const override;

		std::string subfamily_name() const override;

		float font_size() const override;

		int ascent() const override;

		int line_height() const override;

		const std::set<char32_t>& all_codepoints() const override;

		bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const override;

		const void* get_base_font_glyph_uniqid(char32_t codepoint) const override;

		char32_t uniqid_to_glyph(const void* pc) const override;

		const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const override;

		bool draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const override;

		bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		std::shared_ptr<fixed_size_font> get_threadsafe_view() const override;

		const fixed_size_font* get_base_font(char32_t codepoint) const override;

	private:
		char32_t translate_codepoint(char32_t codepoint) const;
	};
}

#endif
