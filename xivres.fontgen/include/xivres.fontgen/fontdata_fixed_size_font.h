#ifndef _XIVRES_FONTGENERATOR_GAMEFONTDATAFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_GAMEFONTDATAFIXEDSIZEFONT_H_

#include <vector>

#include "xivres/installation.h"

#include "fixed_size_font.h"

namespace xivres::fontgen {
	enum class game_font_Family {
		AXIS,
		Jupiter,
		JupiterN,
		MiedingerMid,
		Meidinger,
		TrumpGothic,
		ChnAXIS,
		KrnAXIS,
	};

	struct game_fontdata_definition {
		const char* Path;
		const char* Name;
		const char* Family;
		float Size;
	};

	class fontdata_fixed_size_font : public default_abstract_fixed_size_font {
		struct info_t {
			std::shared_ptr<const fontdata::stream> Font;
			std::string FamilyName;
			std::string SubfamilyName;
			std::vector<std::shared_ptr<texture::memory_mipmap_stream>> Mipmaps;
			std::set<char32_t> Codepoints;
			std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
			std::vector<uint8_t> GammaTable;
		};

		std::shared_ptr<const info_t> m_info;

	public:
		fontdata_fixed_size_font(std::shared_ptr<const fontdata::stream> strm, std::vector<std::shared_ptr<texture::memory_mipmap_stream>> mipmapStreams, std::string familyName, std::string subfamilyName);

		fontdata_fixed_size_font();
		fontdata_fixed_size_font(fontdata_fixed_size_font&&) noexcept;
		fontdata_fixed_size_font(const fontdata_fixed_size_font& r);
		fontdata_fixed_size_font& operator=(fontdata_fixed_size_font&&) noexcept;
		fontdata_fixed_size_font& operator=(const fontdata_fixed_size_font&);

		std::string family_name() const override;

		std::string subfamily_name() const override;

		float font_size() const override;

		int ascent() const override;

		int line_height() const override;

		const std::set<char32_t>& all_codepoints() const override;

		bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const override;

		const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const override;

		int get_adjusted_advance_width(char32_t left, char32_t right) const override;

		bool draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const override;

		bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		std::shared_ptr<fixed_size_font> get_threadsafe_view() const override;

		const fixed_size_font* get_base_font(char32_t codepoint) const override;

	private:
		glyph_metrics glyph_metrics_from_glyph_entry(const fontdata::glyph_entry* pEntry, int x = 0, int y = 0) const;
	};

	class game_fontdata_set {
		xivres::font_type m_gameFontType;
		std::vector<std::shared_ptr<xivres::fontgen::fontdata_fixed_size_font>> m_data;

	public:
		game_fontdata_set();
		game_fontdata_set(game_fontdata_set&&) noexcept;
		game_fontdata_set(const game_fontdata_set&);
		game_fontdata_set& operator=(game_fontdata_set&&) noexcept;
		game_fontdata_set& operator=(const game_fontdata_set&);

		game_fontdata_set(xivres::font_type gameFontType, std::vector<std::shared_ptr<xivres::fontgen::fontdata_fixed_size_font>> data);

		std::shared_ptr<xivres::fontgen::fontdata_fixed_size_font> operator[](size_t i) const;

		std::shared_ptr<xivres::fontgen::fontdata_fixed_size_font> get_font(size_t i) const;

		std::shared_ptr<xivres::fontgen::fontdata_fixed_size_font> get_font(game_font_Family family, float size) const;

		size_t count() const;

		operator bool() const;
	};

	std::span<const fontgen::game_fontdata_definition> get_fontdata_definition(font_type fontType = font_type::font);

	const char* get_font_tex_filename_format(font_type fontType = font_type::font);
}

#endif
