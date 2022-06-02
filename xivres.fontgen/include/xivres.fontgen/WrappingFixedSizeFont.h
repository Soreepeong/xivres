#ifndef _XIVRES_FONTGENERATOR_CODEPOINTLIMITINGFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_CODEPOINTLIMITINGFIXEDSIZEFONT_H_

#include "IFixedSizeFont.h"

namespace xivres::fontgen {
	struct WrapModifiers {
		std::vector<std::pair<char32_t, char32_t>> Codepoints;
		std::map<char32_t, char32_t> CodepointReplacements;
		int LetterSpacing = 0;
		int HorizontalOffset = 0;
		int BaselineShift = 0;
	};

	class WrappingFixedSizeFont : public DefaultAbstractFixedSizeFont {
		struct InfoStruct {
			std::set<char32_t> Codepoints;
			std::map<char32_t, char32_t> MappedCodepoints;
			int LetterSpacing = 0;
			int HorizontalOffset = 0;
			int BaselineShift = 0;
		};

		std::shared_ptr<const IFixedSizeFont> m_font;
		std::shared_ptr<const InfoStruct> m_info;

		mutable std::optional<std::map<std::pair<char32_t, char32_t>, int>> m_kerningPairs;

	public:
		WrappingFixedSizeFont(std::shared_ptr<const IFixedSizeFont> font, const WrapModifiers& wrapModifiers);

		WrappingFixedSizeFont();
		WrappingFixedSizeFont(const WrappingFixedSizeFont& r);
		WrappingFixedSizeFont(WrappingFixedSizeFont&& r) noexcept;
		WrappingFixedSizeFont& operator=(const WrappingFixedSizeFont& r);
		WrappingFixedSizeFont& operator=(WrappingFixedSizeFont&& r) noexcept;

		std::string GetFamilyName() const override;

		std::string GetSubfamilyName() const override;

		float GetSize() const override;

		int GetAscent() const override;

		int GetLineHeight() const override;

		const std::set<char32_t>& GetAllCodepoints() const override;

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override;

		const void* GetBaseFontGlyphUniqid(char32_t codepoint) const override;

		char32_t UniqidToGlyph(const void* pc) const override;

		const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const override;

		bool Draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const override;

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override;

		const IFixedSizeFont* GetBaseFont(char32_t codepoint) const override;

	private:
		char32_t TranslateCodepoint(char32_t codepoint) const;
	};
}

#endif
