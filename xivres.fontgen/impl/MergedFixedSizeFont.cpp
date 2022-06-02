#include "../include/xivres.fontgen/MergedFixedSizeFont.h"

int xivres::fontgen::MergedFixedSizeFont::GetVerticalAdjustment(const InfoStruct& info, const xivres::fontgen::IFixedSizeFont& font) {
	switch (info.Alignment) {
		case MergedFontVerticalAlignment::Top:
			return 0;
		case MergedFontVerticalAlignment::Middle:
			return 0 + (info.LineHeight - font.GetLineHeight()) / 2;
		case MergedFontVerticalAlignment::Baseline:
			return 0 + info.Ascent - font.GetAscent();
		case MergedFontVerticalAlignment::Bottom:
			return 0 + info.LineHeight - font.GetLineHeight();
		default:
			throw std::runtime_error("Invalid alignment value set");
	}
}

const xivres::fontgen::IFixedSizeFont* xivres::fontgen::MergedFixedSizeFont::GetBaseFont(char32_t codepoint) const {
	if (const auto it = m_info->UsedFonts.find(codepoint); it != m_info->UsedFonts.end())
		return it->second->GetBaseFont(codepoint);

	return nullptr;
}

std::shared_ptr<xivres::fontgen::IFixedSizeFont> xivres::fontgen::MergedFixedSizeFont::GetThreadSafeView() const {
	auto res = std::make_shared<MergedFixedSizeFont>(*this);
	res->m_fonts.clear();
	for (const auto& font : m_fonts)
		res->m_fonts.emplace_back(font->GetThreadSafeView());
	return res;
}

bool xivres::fontgen::MergedFixedSizeFont::Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	if (const auto it = m_info->UsedFonts.find(codepoint); it != m_info->UsedFonts.end())
		return it->second->Draw(codepoint, pBuf, stride, drawX, drawY + GetVerticalAdjustment(*m_info, *it->second), destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity);

	return false;
}

bool xivres::fontgen::MergedFixedSizeFont::Draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const {
	if (const auto it = m_info->UsedFonts.find(codepoint); it != m_info->UsedFonts.end())
		return it->second->Draw(codepoint, pBuf, drawX, drawY + GetVerticalAdjustment(*m_info, *it->second), destWidth, destHeight, fgColor, bgColor);

	return false;
}

const std::map<std::pair<char32_t, char32_t>, int>& xivres::fontgen::MergedFixedSizeFont::GetAllKerningPairs() const {
	if (m_kerningPairs)
		return *m_kerningPairs;

	std::map<IFixedSizeFont*, std::set<char32_t>> charsPerFonts;
	for (const auto& [c, f] : m_info->UsedFonts)
		charsPerFonts[f].insert(c);

	m_kerningPairs.emplace();
	for (const auto& [font, chars] : charsPerFonts) {
		for (const auto& kerningPair : font->GetAllKerningPairs()) {
			if (kerningPair.first.first < U' ' || kerningPair.first.second < U' ')
				continue;

			if (chars.contains(kerningPair.first.first) && chars.contains(kerningPair.first.second) && kerningPair.second)
				m_kerningPairs->emplace(kerningPair);
		}
	}

	return *m_kerningPairs;
}

bool xivres::fontgen::MergedFixedSizeFont::GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const {
	if (const auto it = m_info->UsedFonts.find(codepoint); it != m_info->UsedFonts.end()) {
		if (!it->second->GetGlyphMetrics(codepoint, gm))
			return false;

		gm.Translate(0, GetVerticalAdjustment(*m_info, *it->second));
		return true;
	}

	return false;
}

char32_t xivres::fontgen::MergedFixedSizeFont::UniqidToGlyph(const void* pc) const {
	for (const auto& font : m_info->UsedFonts) {
		if (const auto r = font.second->UniqidToGlyph(pc))
			return r;
	}
	return 0;
}

const void* xivres::fontgen::MergedFixedSizeFont::GetBaseFontGlyphUniqid(char32_t c) const {
	if (const auto it = m_info->UsedFonts.find(c); it != m_info->UsedFonts.end())
		return it->second->GetBaseFontGlyphUniqid(c);

	return nullptr;
}

const std::set<char32_t>& xivres::fontgen::MergedFixedSizeFont::GetAllCodepoints() const {
	return m_info->Codepoints;
}

int xivres::fontgen::MergedFixedSizeFont::GetLineHeight() const {
	return m_info->LineHeight;
}

int xivres::fontgen::MergedFixedSizeFont::GetAscent() const {
	return m_info->Ascent;
}

float xivres::fontgen::MergedFixedSizeFont::GetSize() const {
	return m_info->Size;
}

std::string xivres::fontgen::MergedFixedSizeFont::GetSubfamilyName() const {
	return {};
}

std::string xivres::fontgen::MergedFixedSizeFont::GetFamilyName() const {
	return "Merged";
}

xivres::fontgen::MergedFontVerticalAlignment xivres::fontgen::MergedFixedSizeFont::GetComponentVerticalAlignment() const {
	return m_info->Alignment;
}

xivres::fontgen::MergedFixedSizeFont::MergedFixedSizeFont(std::vector<std::pair<std::shared_ptr<IFixedSizeFont>, MergedFontCodepointMode>> fonts, MergedFontVerticalAlignment verticalAlignment) {
	auto info = std::make_shared<InfoStruct>();
	if (fonts.empty())
		fonts.emplace_back(std::make_shared<EmptyFixedSizeFont>(), MergedFontCodepointMode::AddAll);

	info->Alignment = verticalAlignment;
	info->Size = fonts.front().first->GetSize();
	info->Ascent = fonts.front().first->GetAscent();
	info->LineHeight = fonts.front().first->GetLineHeight();

	for (auto& [font, mergeMode] : fonts) {
		for (const auto c : font->GetAllCodepoints()) {
			switch (mergeMode) {
				case MergedFontCodepointMode::AddNew:
					if (info->UsedFonts.emplace(c, font.get()).second)
						info->Codepoints.insert(c);
					break;
				case MergedFontCodepointMode::Replace:
					if (const auto it = info->UsedFonts.find(c); it != info->UsedFonts.end())
						it->second = font.get();
					break;
				case MergedFontCodepointMode::AddAll:
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

xivres::fontgen::MergedFixedSizeFont::MergedFixedSizeFont() = default;

xivres::fontgen::MergedFixedSizeFont::MergedFixedSizeFont(MergedFixedSizeFont&&) noexcept = default;

xivres::fontgen::MergedFixedSizeFont::MergedFixedSizeFont(const MergedFixedSizeFont&) = default;

xivres::fontgen::MergedFixedSizeFont& xivres::fontgen::MergedFixedSizeFont::operator=(MergedFixedSizeFont&&) noexcept = default;

xivres::fontgen::MergedFixedSizeFont& xivres::fontgen::MergedFixedSizeFont::operator=(const MergedFixedSizeFont&) = default;
