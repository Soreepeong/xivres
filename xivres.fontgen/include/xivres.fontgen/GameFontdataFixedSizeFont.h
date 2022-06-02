#ifndef _XIVRES_FONTGENERATOR_GAMEFONTDATAFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_GAMEFONTDATAFIXEDSIZEFONT_H_

#include <vector>

#include "xivres/installation.h"

#include "IFixedSizeFont.h"

namespace xivres::fontgen {
	enum class GameFontFamily {
		AXIS,
		Jupiter,
		JupiterN,
		MiedingerMid,
		Meidinger,
		TrumpGothic,
		ChnAXIS,
		KrnAXIS,
	};

	struct GameFontdataDefinition {
		const char* Path;
		const char* Name;
		const char* Family;
		float Size;
	};

	class GameFontdataFixedSizeFont : public DefaultAbstractFixedSizeFont {
		struct InfoStruct {
			std::shared_ptr<const fontdata::stream> Font;
			std::string FamilyName;
			std::string SubfamilyName;
			std::vector<std::shared_ptr<texture::memory_mipmap_stream>> Mipmaps;
			std::set<char32_t> Codepoints;
			std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
			std::vector<uint8_t> GammaTable;
		};

		std::shared_ptr<const InfoStruct> m_info;

	public:
		GameFontdataFixedSizeFont(std::shared_ptr<const fontdata::stream> strm, std::vector<std::shared_ptr<texture::memory_mipmap_stream>> mipmapStreams, std::string familyName, std::string subfamilyName);

		GameFontdataFixedSizeFont();
		GameFontdataFixedSizeFont(GameFontdataFixedSizeFont&&) noexcept;
		GameFontdataFixedSizeFont(const GameFontdataFixedSizeFont& r);
		GameFontdataFixedSizeFont& operator=(GameFontdataFixedSizeFont&&) noexcept;
		GameFontdataFixedSizeFont& operator=(const GameFontdataFixedSizeFont&);

		std::string GetFamilyName() const override;

		std::string GetSubfamilyName() const override;

		float GetSize() const override;

		int GetAscent() const override;

		int GetLineHeight() const override;

		const std::set<char32_t>& GetAllCodepoints() const override;

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override;

		const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const override;

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override;

		bool Draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const override;

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override;

		const IFixedSizeFont* GetBaseFont(char32_t codepoint) const override;

	private:
		GlyphMetrics GlyphMetricsFromEntry(const fontdata::glyph_entry* pEntry, int x = 0, int y = 0) const;
	};

	class GameFontdataSet {
		xivres::font_type m_gameFontType;
		std::vector<std::shared_ptr<xivres::fontgen::GameFontdataFixedSizeFont>> m_data;

	public:
		GameFontdataSet();
		GameFontdataSet(GameFontdataSet&&) noexcept;
		GameFontdataSet(const GameFontdataSet&);
		GameFontdataSet& operator=(GameFontdataSet&&) noexcept;
		GameFontdataSet& operator=(const GameFontdataSet&);

		GameFontdataSet(xivres::font_type gameFontType, std::vector<std::shared_ptr<xivres::fontgen::GameFontdataFixedSizeFont>> data);

		std::shared_ptr<xivres::fontgen::GameFontdataFixedSizeFont> operator[](size_t i) const;

		std::shared_ptr<xivres::fontgen::GameFontdataFixedSizeFont> GetFont(size_t i) const;

		std::shared_ptr<xivres::fontgen::GameFontdataFixedSizeFont> GetFont(GameFontFamily family, float size) const;

		size_t Count() const;

		operator bool() const;
	};

	std::span<const fontgen::GameFontdataDefinition> GetFontDefinition(font_type fontType = font_type::font);

	const char* GetFontTexFilenameFormat(font_type fontType = font_type::font);
}

#endif
