#ifndef _XIVRES_FONTGENERATOR_MERGEDFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_MERGEDFIXEDSIZEFONT_H_

#include "IFixedSizeFont.h"

namespace xivres::fontgen {
	enum class MergedFontVerticalAlignment {
		Top,
		Middle,
		Baseline,
		Bottom,
	};

	enum class MergedFontCodepointMode {
		AddNew,
		AddAll,
		Replace,
		Enum_Count_,
	};

	class MergedFixedSizeFont : public DefaultAbstractFixedSizeFont {
		struct InfoStruct {
			std::set<char32_t> Codepoints;
			std::map<char32_t, IFixedSizeFont*> UsedFonts;
			float Size{};
			int Ascent{};
			int LineHeight{};
			MergedFontVerticalAlignment Alignment = MergedFontVerticalAlignment::Baseline;
		};

		std::vector<std::shared_ptr<xivres::fontgen::IFixedSizeFont>> m_fonts;
		std::shared_ptr<const InfoStruct> m_info;
		mutable std::optional<std::map<std::pair<char32_t, char32_t>, int>> m_kerningPairs;

	public:
		MergedFixedSizeFont();
		MergedFixedSizeFont(MergedFixedSizeFont&&) noexcept;
		MergedFixedSizeFont(const MergedFixedSizeFont&);
		MergedFixedSizeFont& operator=(MergedFixedSizeFont&&) noexcept;
		MergedFixedSizeFont& operator=(const MergedFixedSizeFont&);

		MergedFixedSizeFont(std::vector<std::pair<std::shared_ptr<IFixedSizeFont>, MergedFontCodepointMode>> fonts, MergedFontVerticalAlignment verticalAlignment = MergedFontVerticalAlignment::Baseline);

		MergedFontVerticalAlignment GetComponentVerticalAlignment() const;

		std::string GetFamilyName() const override;

		std::string GetSubfamilyName() const override;

		float GetSize() const override;

		int GetAscent() const override;

		int GetLineHeight() const override;

		const std::set<char32_t>& GetAllCodepoints() const override;

		const void* GetBaseFontGlyphUniqid(char32_t c) const override;

		char32_t UniqidToGlyph(const void* pc) const override;

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override;

		const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const override;

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const override;

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override;

		const IFixedSizeFont* GetBaseFont(char32_t codepoint) const override;

	private:
		static int GetVerticalAdjustment(const InfoStruct& info, const xivres::fontgen::IFixedSizeFont& font);
	};
}

#endif
