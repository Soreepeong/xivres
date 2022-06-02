#include "../include/xivres.fontgen/WrappingFixedSizeFont.h"

char32_t xivres::fontgen::WrappingFixedSizeFont::TranslateCodepoint(char32_t codepoint) const {
	if (const auto it = m_info->MappedCodepoints.find(codepoint); it != m_info->MappedCodepoints.end())
		return TranslateCodepoint(it->second);
	return codepoint;
}

const xivres::fontgen::IFixedSizeFont* xivres::fontgen::WrappingFixedSizeFont::GetBaseFont(char32_t codepoint) const {
	if (!m_info->Codepoints.contains(codepoint))
		return nullptr;

	return m_font->GetBaseFont(codepoint);
}

std::shared_ptr<xivres::fontgen::IFixedSizeFont> xivres::fontgen::WrappingFixedSizeFont::GetThreadSafeView() const {
	auto res = std::make_shared<WrappingFixedSizeFont>(*this);
	res->m_font = m_font->GetThreadSafeView();
	return res;
}

bool xivres::fontgen::WrappingFixedSizeFont::Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	codepoint = TranslateCodepoint(codepoint);

	GlyphMetrics gm;
	if (!m_font->GetGlyphMetrics(codepoint, gm))
		return false;

	const auto remainingOffset = gm.X1 + m_info->HorizontalOffset;
	if (remainingOffset >= 0)
		drawX += m_info->HorizontalOffset;
	else
		drawX -= gm.X1;
	drawY += m_info->BaselineShift;

	return m_font->Draw(codepoint, pBuf, stride, drawX, drawY, destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity);
}

bool xivres::fontgen::WrappingFixedSizeFont::Draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const {
	codepoint = TranslateCodepoint(codepoint);

	GlyphMetrics gm;
	if (!m_font->GetGlyphMetrics(codepoint, gm))
		return false;

	const auto remainingOffset = gm.X1 + m_info->HorizontalOffset;
	if (remainingOffset >= 0)
		drawX += m_info->HorizontalOffset;
	else
		drawX -= gm.X1;
	drawY += m_info->BaselineShift;

	return m_font->Draw(codepoint, pBuf, drawX, drawY, destWidth, destHeight, fgColor, bgColor);
}

const std::map<std::pair<char32_t, char32_t>, int>& xivres::fontgen::WrappingFixedSizeFont::GetAllKerningPairs() const {
	if (m_kerningPairs)
		return *m_kerningPairs;

	std::map<char32_t, int> NetHorizontalOffsets;
	std::map<char32_t, GlyphMetrics> AllGlyphMetrics;
	std::map<util::unicode::blocks::negative_lsb_group, std::map<char32_t, int>> negativeLsbChars;

	std::map<char32_t, std::set<char32_t>> reverseMappedCodepoints;

	for (const auto codepoint : m_info->Codepoints) {
		const auto mapped = TranslateCodepoint(codepoint);

		reverseMappedCodepoints[mapped].insert(codepoint);

		if (mapped < U' ')
			continue;

		GlyphMetrics gm;
		if (!m_font->GetGlyphMetrics(mapped, gm))
			continue;

		const auto remainingOffset = gm.X1 + m_info->HorizontalOffset;
		if (remainingOffset >= 0) {
			gm.AdvanceX = gm.AdvanceX + m_info->LetterSpacing;
			gm.Translate(m_info->HorizontalOffset, m_info->BaselineShift);

		} else {
			gm.AdvanceX = gm.AdvanceX + m_info->LetterSpacing - gm.X1;
			gm.Translate(-gm.X1, m_info->BaselineShift);

			do {
				const auto& block = util::unicode::blocks::block_for(mapped);
				if (block.NegativeLsbGroup == util::unicode::blocks::None)
					break;

				if (block.Purpose & util::unicode::blocks::UsedWithCombining)
					break;

				negativeLsbChars[block.NegativeLsbGroup][mapped] = remainingOffset;
			} while (false);
		}
	}

	m_kerningPairs.emplace(m_font->GetAllKerningPairs());
#pragma warning(push)
#pragma warning(disable: 26812)
	for (const auto& [group, chars] : negativeLsbChars) {
		for (const auto& [rightc, offset] : chars) {
			if (!reverseMappedCodepoints.contains(rightc))
				continue;

			for (const auto& block : util::unicode::blocks::all_blocks()) {
				if (block.NegativeLsbGroup != group && (group != util::unicode::blocks::Combining || !(block.Purpose & util::unicode::blocks::UsedWithCombining)))
					continue;
#pragma warning(pop)
				for (auto leftc = block.First; leftc <= block.Last; leftc++) {
					if (!reverseMappedCodepoints.contains(leftc))
						continue;

					for (const auto leftUnmapped : reverseMappedCodepoints.at(leftc))
						for (const auto rightUnmapped : reverseMappedCodepoints.at(rightc))
							(*m_kerningPairs)[std::make_pair(leftUnmapped, rightUnmapped)] += offset;
				}
			}
		}
	}

	for (auto it = m_kerningPairs->begin(); it != m_kerningPairs->end(); ) {
		if (m_info->Codepoints.contains(it->first.first) && m_info->Codepoints.contains(it->first.second) && it->second != 0)
			++it;
		else
			it = m_kerningPairs->erase(it);
	}

	return *m_kerningPairs;
}

char32_t xivres::fontgen::WrappingFixedSizeFont::UniqidToGlyph(const void* pc) const {
	return m_font->UniqidToGlyph(pc);
}

const void* xivres::fontgen::WrappingFixedSizeFont::GetBaseFontGlyphUniqid(char32_t codepoint) const {
	codepoint = TranslateCodepoint(codepoint);

	if (!m_info->Codepoints.contains(codepoint))
		return nullptr;

	return m_font->GetBaseFontGlyphUniqid(codepoint);
}

bool xivres::fontgen::WrappingFixedSizeFont::GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const {
	codepoint = TranslateCodepoint(codepoint);

	if (!m_font->GetGlyphMetrics(codepoint, gm))
		return false;

	const auto remainingOffset = gm.X1 + m_info->HorizontalOffset;
	if (remainingOffset >= 0) {
		gm.Translate(m_info->HorizontalOffset, m_info->BaselineShift);
		gm.AdvanceX += m_info->LetterSpacing;
	} else {
		gm.Translate(-gm.X1, m_info->BaselineShift);
		gm.AdvanceX += m_info->LetterSpacing - gm.X1;
	}

	return true;
}

const std::set<char32_t>& xivres::fontgen::WrappingFixedSizeFont::GetAllCodepoints() const {
	return m_info->Codepoints;
}

int xivres::fontgen::WrappingFixedSizeFont::GetLineHeight() const {
	return m_font->GetLineHeight();
}

int xivres::fontgen::WrappingFixedSizeFont::GetAscent() const {
	return m_font->GetAscent();
}

float xivres::fontgen::WrappingFixedSizeFont::GetSize() const {
	return m_font->GetSize();
}

std::string xivres::fontgen::WrappingFixedSizeFont::GetSubfamilyName() const {
	return m_font->GetSubfamilyName();
}

std::string xivres::fontgen::WrappingFixedSizeFont::GetFamilyName() const {
	return m_font->GetFamilyName();
}

xivres::fontgen::WrappingFixedSizeFont::WrappingFixedSizeFont(std::shared_ptr<const IFixedSizeFont> font, const WrapModifiers& wrapModifiers) : m_font(std::move(font)) {
	auto info = std::make_shared<InfoStruct>();
	info->LetterSpacing = wrapModifiers.LetterSpacing;
	info->HorizontalOffset = wrapModifiers.HorizontalOffset;
	info->BaselineShift = wrapModifiers.BaselineShift;

	std::set<char32_t> codepoints;
	for (const auto& c : m_font->GetAllCodepoints()) {
		auto found = false;
		for (const auto& [c1, c2] : wrapModifiers.Codepoints) {
			if (c1 <= c && c <= c2) {
				found = true;
				break;
			}
		}
		if (found) {
			info->Codepoints.insert(c);
			if (const auto it = wrapModifiers.CodepointReplacements.find(c); it != wrapModifiers.CodepointReplacements.end())
				info->MappedCodepoints[c] = it->second;
		}
	}

	m_info = std::move(info);
}

xivres::fontgen::WrappingFixedSizeFont::WrappingFixedSizeFont() = default;

xivres::fontgen::WrappingFixedSizeFont::WrappingFixedSizeFont(const WrappingFixedSizeFont & r) = default;

xivres::fontgen::WrappingFixedSizeFont::WrappingFixedSizeFont(WrappingFixedSizeFont && r) noexcept = default;

xivres::fontgen::WrappingFixedSizeFont& xivres::fontgen::WrappingFixedSizeFont::operator=(const WrappingFixedSizeFont & r) = default;

xivres::fontgen::WrappingFixedSizeFont& xivres::fontgen::WrappingFixedSizeFont::operator=(WrappingFixedSizeFont && r) noexcept = default;
