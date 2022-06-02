#include "../include/xivres.fontgen/merged_fixed_size_font.h"

int xivres::fontgen::merged_fixed_size_font::get_vertical_adjustment(const info_t& info, const xivres::fontgen::fixed_size_font& font) {
	switch (info.Alignment) {
		case vertical_alignment::Top:
			return 0;
		case vertical_alignment::Middle:
			return 0 + (info.LineHeight - font.line_height()) / 2;
		case vertical_alignment::Baseline:
			return 0 + info.Ascent - font.ascent();
		case vertical_alignment::Bottom:
			return 0 + info.LineHeight - font.line_height();
		default:
			throw std::runtime_error("Invalid alignment value set");
	}
}

const xivres::fontgen::fixed_size_font* xivres::fontgen::merged_fixed_size_font::get_base_font(char32_t codepoint) const {
	if (const auto it = m_info->UsedFonts.find(codepoint); it != m_info->UsedFonts.end())
		return it->second->get_base_font(codepoint);

	return nullptr;
}

std::shared_ptr<xivres::fontgen::fixed_size_font> xivres::fontgen::merged_fixed_size_font::get_threadsafe_view() const {
	auto res = std::make_shared<merged_fixed_size_font>(*this);
	res->m_fonts.clear();
	for (const auto& font : m_fonts)
		res->m_fonts.emplace_back(font->get_threadsafe_view());
	return res;
}

bool xivres::fontgen::merged_fixed_size_font::draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	if (const auto it = m_info->UsedFonts.find(codepoint); it != m_info->UsedFonts.end())
		return it->second->draw(codepoint, pBuf, stride, drawX, drawY + get_vertical_adjustment(*m_info, *it->second), destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity);

	return false;
}

bool xivres::fontgen::merged_fixed_size_font::draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const {
	if (const auto it = m_info->UsedFonts.find(codepoint); it != m_info->UsedFonts.end())
		return it->second->draw(codepoint, pBuf, drawX, drawY + get_vertical_adjustment(*m_info, *it->second), destWidth, destHeight, fgColor, bgColor);

	return false;
}

const std::map<std::pair<char32_t, char32_t>, int>& xivres::fontgen::merged_fixed_size_font::all_kerning_pairs() const {
	if (m_kerningPairs)
		return *m_kerningPairs;

	std::map<fixed_size_font*, std::set<char32_t>> charsPerFonts;
	for (const auto& [c, f] : m_info->UsedFonts)
		charsPerFonts[f].insert(c);

	m_kerningPairs.emplace();
	for (const auto& [font, chars] : charsPerFonts) {
		for (const auto& kerningPair : font->all_kerning_pairs()) {
			if (kerningPair.first.first < U' ' || kerningPair.first.second < U' ')
				continue;

			if (chars.contains(kerningPair.first.first) && chars.contains(kerningPair.first.second) && kerningPair.second)
				m_kerningPairs->emplace(kerningPair);
		}
	}

	return *m_kerningPairs;
}

bool xivres::fontgen::merged_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const {
	if (const auto it = m_info->UsedFonts.find(codepoint); it != m_info->UsedFonts.end()) {
		if (!it->second->try_get_glyph_metrics(codepoint, gm))
			return false;

		gm.translate(0, get_vertical_adjustment(*m_info, *it->second));
		return true;
	}

	return false;
}

char32_t xivres::fontgen::merged_fixed_size_font::uniqid_to_glyph(const void* pc) const {
	for (const auto& font : m_info->UsedFonts) {
		if (const auto r = font.second->uniqid_to_glyph(pc))
			return r;
	}
	return 0;
}

const void* xivres::fontgen::merged_fixed_size_font::get_base_font_glyph_uniqid(char32_t c) const {
	if (const auto it = m_info->UsedFonts.find(c); it != m_info->UsedFonts.end())
		return it->second->get_base_font_glyph_uniqid(c);

	return nullptr;
}

const std::set<char32_t>& xivres::fontgen::merged_fixed_size_font::all_codepoints() const {
	return m_info->Codepoints;
}

int xivres::fontgen::merged_fixed_size_font::line_height() const {
	return m_info->LineHeight;
}

int xivres::fontgen::merged_fixed_size_font::ascent() const {
	return m_info->Ascent;
}

float xivres::fontgen::merged_fixed_size_font::font_size() const {
	return m_info->Size;
}

std::string xivres::fontgen::merged_fixed_size_font::subfamily_name() const {
	return {};
}

std::string xivres::fontgen::merged_fixed_size_font::family_name() const {
	return "Merged";
}

xivres::fontgen::merged_fixed_size_font::merged_fixed_size_font(std::vector<std::pair<std::shared_ptr<fixed_size_font>, codepoint_merge_mode>> fonts, vertical_alignment verticalAlignment) {
	auto info = std::make_shared<info_t>();
	if (fonts.empty())
		fonts.emplace_back(std::make_shared<empty_fixed_size_font>(), codepoint_merge_mode::AddAll);

	info->Alignment = verticalAlignment;
	info->Size = fonts.front().first->font_size();
	info->Ascent = fonts.front().first->ascent();
	info->LineHeight = fonts.front().first->line_height();

	for (auto& [font, mergeMode] : fonts) {
		for (const auto c : font->all_codepoints()) {
			switch (mergeMode) {
				case codepoint_merge_mode::AddNew:
					if (info->UsedFonts.emplace(c, font.get()).second)
						info->Codepoints.insert(c);
					break;
				case codepoint_merge_mode::Replace:
					if (const auto it = info->UsedFonts.find(c); it != info->UsedFonts.end())
						it->second = font.get();
					break;
				case codepoint_merge_mode::AddAll:
					info->UsedFonts.insert_or_assign(c, font.get());
					info->Codepoints.insert(c);
					break;
				default:
					throw std::invalid_argument("Invalid MergedFontCodepointMode");
			}
		}

		m_fonts.emplace_back(std::move(font));
	}

	m_info = std::move(info);
}

xivres::fontgen::merged_fixed_size_font::merged_fixed_size_font() = default;

xivres::fontgen::merged_fixed_size_font::merged_fixed_size_font(merged_fixed_size_font&&) noexcept = default;

xivres::fontgen::merged_fixed_size_font::merged_fixed_size_font(const merged_fixed_size_font&) = default;

xivres::fontgen::merged_fixed_size_font& xivres::fontgen::merged_fixed_size_font::operator=(merged_fixed_size_font&&) noexcept = default;

xivres::fontgen::merged_fixed_size_font& xivres::fontgen::merged_fixed_size_font::operator=(const merged_fixed_size_font&) = default;
