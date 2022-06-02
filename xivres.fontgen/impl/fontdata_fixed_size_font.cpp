#include "../include/xivres.fontgen/fontdata_fixed_size_font.h"

#include "xivres/texture.stream.h"
#include "xivres/util.bitmap_copy.h"

xivres::fontgen::glyph_metrics xivres::fontgen::fontdata_fixed_size_font::glyph_metrics_from_glyph_entry(const fontdata::glyph_entry* pEntry, int x, int y) const {
	glyph_metrics src{
		.X1 = x,
		.Y1 = y + pEntry->CurrentOffsetY,
		.X2 = src.X1 + pEntry->BoundingWidth,
		.Y2 = src.Y1 + pEntry->BoundingHeight,
		.AdvanceX = pEntry->BoundingWidth + pEntry->NextOffsetX,
	};
	return src;
}

const xivres::fontgen::fixed_size_font* xivres::fontgen::fontdata_fixed_size_font::get_base_font(char32_t codepoint) const {
	return this;
}

std::shared_ptr<xivres::fontgen::fixed_size_font> xivres::fontgen::fontdata_fixed_size_font::get_threadsafe_view() const {
	return std::make_shared<fontdata_fixed_size_font>(*this);
}

bool xivres::fontgen::fontdata_fixed_size_font::draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	const auto pEntry = m_info->Font->get_glyph(codepoint);
	if (!pEntry)
		return false;

	auto src = glyph_metrics{ *pEntry->TextureOffsetX, *pEntry->TextureOffsetY, *pEntry->TextureOffsetX + *pEntry->BoundingWidth, *pEntry->TextureOffsetY + *pEntry->BoundingHeight };
	auto dest = glyph_metrics_from_glyph_entry(pEntry, drawX, drawY);
	const auto& mipmapStream = *m_info->Mipmaps.at(pEntry->texture_file_index());
	const auto planeIndex = fontdata::glyph_entry::ChannelMap[pEntry->texture_plane_index()];
	src.adjust_to_intersection(dest, mipmapStream.Width, mipmapStream.Height, destWidth, destHeight);
	util::bitmap_copy::to_l8()
		.from(&mipmapStream.as_span<uint8_t>()[planeIndex], mipmapStream.Width, mipmapStream.Height, 4, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, 4, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.fore_opacity(fgOpacity)
		.back_color(bgColor)
		.back_opacity(bgOpacity)
		.gamma_table(m_info->GammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

bool xivres::fontgen::fontdata_fixed_size_font::draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const {
	const auto pEntry = m_info->Font->get_glyph(codepoint);
	if (!pEntry)
		return false;

	auto src = glyph_metrics{ *pEntry->TextureOffsetX, *pEntry->TextureOffsetY, *pEntry->TextureOffsetX + *pEntry->BoundingWidth, *pEntry->TextureOffsetY + *pEntry->BoundingHeight };
	auto dest = glyph_metrics_from_glyph_entry(pEntry, drawX, drawY);
	const auto& mipmapStream = *m_info->Mipmaps.at(pEntry->texture_file_index());
	const auto planeIndex = fontdata::glyph_entry::ChannelMap[pEntry->texture_plane_index()];
	src.adjust_to_intersection(dest, mipmapStream.Width, mipmapStream.Height, destWidth, destHeight);
	util::bitmap_copy::to_rgba8888()
		.from(&mipmapStream.as_span<uint8_t>()[planeIndex], mipmapStream.Width, mipmapStream.Height, 4, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.back_color(bgColor)
		.gamma_table(m_info->GammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

int xivres::fontgen::fontdata_fixed_size_font::get_adjusted_advance_width(char32_t left, char32_t right) const {
	glyph_metrics gm;
	if (!try_get_glyph_metrics(left, gm))
		return 0;

	return gm.AdvanceX + m_info->Font->get_kerning(left, right);
}

const std::map<std::pair<char32_t, char32_t>, int>& xivres::fontgen::fontdata_fixed_size_font::all_kerning_pairs() const {
	return m_info->KerningPairs;
}

bool xivres::fontgen::fontdata_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const {
	const auto p = m_info->Font->get_glyph(codepoint);
	if (!p)
		return false;

	gm = glyph_metrics_from_glyph_entry(p);
	return true;
}

const std::set<char32_t>& xivres::fontgen::fontdata_fixed_size_font::all_codepoints() const {
	return m_info->Codepoints;
}

int xivres::fontgen::fontdata_fixed_size_font::line_height() const {
	return m_info->Font->line_height();
}

int xivres::fontgen::fontdata_fixed_size_font::ascent() const {
	return m_info->Font->ascent();
}

float xivres::fontgen::fontdata_fixed_size_font::font_size() const {
	return m_info->Font->font_size();
}

std::string xivres::fontgen::fontdata_fixed_size_font::subfamily_name() const {
	return m_info->SubfamilyName;
}

std::string xivres::fontgen::fontdata_fixed_size_font::family_name() const {
	return m_info->FamilyName;
}

xivres::fontgen::fontdata_fixed_size_font::fontdata_fixed_size_font(std::shared_ptr<const fontdata::stream> strm, std::vector<std::shared_ptr<texture::memory_mipmap_stream>> mipmapStreams, std::string familyName, std::string subfamilyName) {
	for (const auto& mipmapStream : mipmapStreams) {
		if (mipmapStream->Type != texture::format::A8R8G8B8)
			throw std::invalid_argument("All mipmap streams must be in A8R8G8B8 format.");
	}

	auto info = std::make_shared<info_t>();
	info->Font = std::move(strm);
	info->FamilyName = std::move(familyName);
	info->SubfamilyName = std::move(subfamilyName);
	info->Mipmaps = std::move(mipmapStreams);
	info->GammaTable = util::bitmap_copy::create_gamma_table(1.f);

	for (const auto& entry : info->Font->get_glyphs())
		info->Codepoints.insert(info->Codepoints.end(), entry.codepoint());

	for (const auto& entry : info->Font->get_kernings())
		info->KerningPairs.emplace_hint(info->KerningPairs.end(), std::make_pair(entry.left(), entry.right()), entry.RightOffset);

	m_info = std::move(info);
}

xivres::fontgen::fontdata_fixed_size_font::fontdata_fixed_size_font() = default;

xivres::fontgen::fontdata_fixed_size_font::fontdata_fixed_size_font(fontdata_fixed_size_font&&) noexcept = default;

xivres::fontgen::fontdata_fixed_size_font::fontdata_fixed_size_font(const fontdata_fixed_size_font & r) = default;

xivres::fontgen::fontdata_fixed_size_font& xivres::fontgen::fontdata_fixed_size_font::operator=(fontdata_fixed_size_font&&) noexcept = default;

xivres::fontgen::fontdata_fixed_size_font& xivres::fontgen::fontdata_fixed_size_font::operator=(const fontdata_fixed_size_font&) = default;

xivres::fontgen::game_fontdata_set::operator bool() const {
	return !m_data.empty();
}

size_t xivres::fontgen::game_fontdata_set::count() const {
	return m_data.size();
}

std::shared_ptr<xivres::fontgen::fontdata_fixed_size_font> xivres::fontgen::game_fontdata_set::get_font(game_font_Family family, float size) const {
	std::vector<size_t> candidates;
	candidates.reserve(5);

	for (size_t i = 0; i < m_data.size(); i++) {
		const auto name = m_data[i]->family_name();
		if (family == game_font_Family::AXIS && name == "AXIS")
			candidates.push_back(i);
		else if (family == game_font_Family::Jupiter && name == "Jupiter")
			candidates.push_back(i);
		else if (family == game_font_Family::JupiterN && name == "JupiterN")
			candidates.push_back(i);
		else if (family == game_font_Family::Meidinger && name == "Meidinger")
			candidates.push_back(i);
		else if (family == game_font_Family::MiedingerMid && name == "MiedingerMid")
			candidates.push_back(i);
		else if (family == game_font_Family::TrumpGothic && name == "TrumpGothic")
			candidates.push_back(i);
		else if (family == game_font_Family::ChnAXIS && name == "ChnAXIS")
			candidates.push_back(i);
		else if (family == game_font_Family::KrnAXIS && name == "KrnAXIS")
			candidates.push_back(i);
	}

	if (candidates.empty())
		return {};

	std::sort(candidates.begin(), candidates.end(), [this, size](const auto& l, const auto& r) {
		return std::fabsf(size - m_data[l]->font_size()) < std::fabsf(size - m_data[r]->font_size());
	});

	return m_data[candidates[0]];
}

std::shared_ptr<xivres::fontgen::fontdata_fixed_size_font> xivres::fontgen::game_fontdata_set::get_font(size_t i) const {
	return m_data[i];
}

std::shared_ptr<xivres::fontgen::fontdata_fixed_size_font> xivres::fontgen::game_fontdata_set::operator[](size_t i) const {
	return m_data[i];
}

xivres::fontgen::game_fontdata_set::game_fontdata_set(xivres::font_type gameFontType, std::vector<std::shared_ptr<xivres::fontgen::fontdata_fixed_size_font>> data)
	: m_gameFontType(gameFontType)
	, m_data(std::move(data)) {

}

xivres::fontgen::game_fontdata_set::game_fontdata_set() = default;

xivres::fontgen::game_fontdata_set::game_fontdata_set(game_fontdata_set&&) noexcept = default;

xivres::fontgen::game_fontdata_set::game_fontdata_set(const game_fontdata_set&) = default;

xivres::fontgen::game_fontdata_set& xivres::fontgen::game_fontdata_set::operator=(game_fontdata_set&&) noexcept = default;

xivres::fontgen::game_fontdata_set& xivres::fontgen::game_fontdata_set::operator=(const game_fontdata_set&) = default;

std::span<const xivres::fontgen::game_fontdata_definition> xivres::fontgen::get_fontdata_definition(font_type fontType) {
	static const game_fontdata_definition fdtListFont[]{
		{"common/font/AXIS_96.fdt", "AXIS", "Regular", 9.6f},
		{"common/font/AXIS_12.fdt", "AXIS", "Regular", 12.f},
		{"common/font/AXIS_14.fdt", "AXIS", "Regular", 14.f},
		{"common/font/AXIS_18.fdt", "AXIS", "Regular", 18.f},
		{"common/font/AXIS_36.fdt", "AXIS", "Regular", 36.f},
		{"common/font/Jupiter_16.fdt", "Jupiter", "Regular", 16.f},
		{"common/font/Jupiter_20.fdt", "Jupiter", "Regular", 20.f},
		{"common/font/Jupiter_23.fdt", "Jupiter", "Regular", 23.f},
		{"common/font/Jupiter_45.fdt", "JupiterN", "Regular", 45.f},
		{"common/font/Jupiter_46.fdt", "Jupiter", "Regular", 46.f},
		{"common/font/Jupiter_90.fdt", "JupiterN", "Regular", 90.f},
		{"common/font/Meidinger_16.fdt", "Meidinger", "Regular", 16.f},
		{"common/font/Meidinger_20.fdt", "Meidinger", "Regular", 20.f},
		{"common/font/Meidinger_40.fdt", "Meidinger", "Regular", 40.f},
		{"common/font/MiedingerMid_10.fdt", "MiedingerMid", "Medium", 10.f},
		{"common/font/MiedingerMid_12.fdt", "MiedingerMid", "Medium", 12.f},
		{"common/font/MiedingerMid_14.fdt", "MiedingerMid", "Medium", 14.f},
		{"common/font/MiedingerMid_18.fdt", "MiedingerMid", "Medium", 18.f},
		{"common/font/MiedingerMid_36.fdt", "MiedingerMid", "Medium", 36.f},
		{"common/font/TrumpGothic_184.fdt", "TrumpGothic", "Regular", 18.4f},
		{"common/font/TrumpGothic_23.fdt", "TrumpGothic", "Regular", 23.f},
		{"common/font/TrumpGothic_34.fdt", "TrumpGothic", "Regular", 34.f},
		{"common/font/TrumpGothic_68.fdt", "TrumpGothic", "Regular", 68.f},
	};

	static const game_fontdata_definition fdtListFontLobby[]{
		{"common/font/AXIS_12_lobby.fdt", "AXIS", "Regular", 12.f},
		{"common/font/AXIS_14_lobby.fdt", "AXIS", "Regular", 14.f},
		{"common/font/AXIS_18_lobby.fdt", "AXIS", "Regular", 18.f},
		{"common/font/AXIS_36_lobby.fdt", "AXIS", "Regular", 36.f},
		{"common/font/Jupiter_16_lobby.fdt", "Jupiter", "Regular", 16.f},
		{"common/font/Jupiter_20_lobby.fdt", "Jupiter", "Regular", 20.f},
		{"common/font/Jupiter_23_lobby.fdt", "Jupiter", "Regular", 23.f},
		{"common/font/Jupiter_45_lobby.fdt", "JupiterN", "Regular", 45.f},
		{"common/font/Jupiter_46_lobby.fdt", "Jupiter", "Regular", 46.f},
		{"common/font/Jupiter_90_lobby.fdt", "JupiterN", "Regular", 90.f},
		{"common/font/Meidinger_16_lobby.fdt", "Meidinger", "Regular", 16.f},
		{"common/font/Meidinger_20_lobby.fdt", "Meidinger", "Regular", 20.f},
		{"common/font/Meidinger_40_lobby.fdt", "Meidinger", "Regular", 40.f},
		{"common/font/MiedingerMid_10_lobby.fdt", "MiedingerMid", "Medium", 10.f},
		{"common/font/MiedingerMid_12_lobby.fdt", "MiedingerMid", "Medium", 12.f},
		{"common/font/MiedingerMid_14_lobby.fdt", "MiedingerMid", "Medium", 14.f},
		{"common/font/MiedingerMid_18_lobby.fdt", "MiedingerMid", "Medium", 18.f},
		{"common/font/MiedingerMid_36_lobby.fdt", "MiedingerMid", "Medium", 36.f},
		{"common/font/TrumpGothic_184_lobby.fdt", "TrumpGothic", "Regular", 18.4f},
		{"common/font/TrumpGothic_23_lobby.fdt", "TrumpGothic", "Regular", 23.f},
		{"common/font/TrumpGothic_34_lobby.fdt", "TrumpGothic", "Regular", 34.f},
		{"common/font/TrumpGothic_68_lobby.fdt", "TrumpGothic", "Regular", 68.f},
	};

	static const game_fontdata_definition fdtListKrnAxis[]{
		{"common/font/KrnAXIS_120.fdt", "KrnAXIS", "Regular", 12.f},
		{"common/font/KrnAXIS_140.fdt", "KrnAXIS", "Regular", 14.f},
		{"common/font/KrnAXIS_180.fdt", "KrnAXIS", "Regular", 18.f},
		{"common/font/KrnAXIS_360.fdt", "KrnAXIS", "Regular", 36.f},
	};

	static const game_fontdata_definition fdtListChnAxis[]{
		{"common/font/ChnAXIS_120.fdt", "ChnAXIS", "Regular", 12.f},
		{"common/font/ChnAXIS_140.fdt", "ChnAXIS", "Regular", 14.f},
		{"common/font/ChnAXIS_180.fdt", "ChnAXIS", "Regular", 18.f},
		{"common/font/ChnAXIS_360.fdt", "ChnAXIS", "Regular", 36.f},
	};

	switch (fontType) {
		case xivres::font_type::font:
			return { fdtListFont };

		case xivres::font_type::font_lobby:
			return { fdtListFontLobby };

		case xivres::font_type::chn_axis:
			return { fdtListChnAxis };

		case xivres::font_type::krn_axis:
			return { fdtListKrnAxis };

		default:
			return {};
	}
}

const char* xivres::fontgen::get_font_tex_filename_format(font_type fontType /*= font_type::font*/) {
	switch (fontType) {
		case xivres::font_type::font: return "common/font/font{}.tex";
		case xivres::font_type::font_lobby: return "common/font/font_lobby{}.tex";
		case xivres::font_type::chn_axis: return "common/font/font_chn_{}.tex";
		case xivres::font_type::krn_axis: return "common/font/font_krn_{}.tex";
		default: return nullptr;
	}
}

xivres::fontgen::game_fontdata_set xivres::installation::get_fontdata_set(xivres::font_type gameFontType, std::span<const fontgen::game_fontdata_definition> gameFontdataDefinitions, const char* pcszTexturePathPattern) const {
	std::vector<std::shared_ptr<xivres::texture::memory_mipmap_stream>> textures;
	try {
		for (int i = 1; ; i++)
			textures.emplace_back(xivres::texture::memory_mipmap_stream::as_argb8888(*xivres::texture::stream(get_file(std::format(pcszTexturePathPattern, i))).mipmap_at(0, 0)));
	} catch (const std::out_of_range&) {
		// do nothing
	}

	std::vector<std::shared_ptr<xivres::fontgen::fontdata_fixed_size_font>> fonts;
	fonts.reserve(gameFontdataDefinitions.size());
	for (const auto& def : gameFontdataDefinitions) {
		fonts.emplace_back(std::make_shared<fontgen::fontdata_fixed_size_font>(
			std::make_shared<fontdata::stream>(*get_file(def.Path)),
			textures,
			def.Name,
			def.Family));
	}

	return { gameFontType, fonts };
}

xivres::fontgen::game_fontdata_set xivres::installation::get_fontdata_set(xivres::font_type fontType) const {
	return get_fontdata_set(fontType, xivres::fontgen::get_fontdata_definition(fontType), xivres::fontgen::get_font_tex_filename_format(fontType));
}
