#include "../include/xivres.fontgen/wrapping_fixed_size_font.h"

char32_t xivres::fontgen::wrapping_fixed_size_font::translate_codepoint(char32_t codepoint) const {
	if (const auto it = m_info->MappedCodepoints.find(codepoint); it != m_info->MappedCodepoints.end())
		return translate_codepoint(it->second);
	return codepoint;
}

const xivres::fontgen::fixed_size_font* xivres::fontgen::wrapping_fixed_size_font::get_base_font(char32_t codepoint) const {
	if (!m_info->Codepoints.contains(codepoint))
		return nullptr;

	return m_font->get_base_font(codepoint);
}

std::shared_ptr<xivres::fontgen::fixed_size_font> xivres::fontgen::wrapping_fixed_size_font::get_threadsafe_view() const {
	auto res = std::make_shared<wrapping_fixed_size_font>(*this);
	res->m_font = m_font->get_threadsafe_view();
	return res;
}

bool xivres::fontgen::wrapping_fixed_size_font::draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	codepoint = translate_codepoint(codepoint);

	glyph_metrics gm;
	if (!m_font->try_get_glyph_metrics(codepoint, gm))
		return false;

	const auto remainingOffset = gm.X1 + m_info->HorizontalOffset;
	if (remainingOffset >= 0)
		drawX += m_info->HorizontalOffset;
	else
		drawX -= gm.X1;
	drawY += m_info->BaselineShift;

	return m_font->draw(codepoint, pBuf, stride, drawX, drawY, destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity);
}

bool xivres::fontgen::wrapping_fixed_size_font::draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const {
	codepoint = translate_codepoint(codepoint);

	glyph_metrics gm;
	if (!m_font->try_get_glyph_metrics(codepoint, gm))
		return false;

	const auto remainingOffset = gm.X1 + m_info->HorizontalOffset;
	if (remainingOffset >= 0)
		drawX += m_info->HorizontalOffset;
	else
		drawX -= gm.X1;
	drawY += m_info->BaselineShift;

	return m_font->draw(codepoint, pBuf, drawX, drawY, destWidth, destHeight, fgColor, bgColor);
}

const std::map<std::pair<char32_t, char32_t>, int>& xivres::fontgen::wrapping_fixed_size_font::all_kerning_pairs() const {
	if (m_kerningPairs)
		return *m_kerningPairs;

	std::map<char32_t, int> NetHorizontalOffsets;
	std::map<char32_t, glyph_metrics> AllGlyphMetrics;
	std::map<util::unicode::blocks::negative_lsb_group, std::map<char32_t, int>> negativeLsbChars;

	std::map<char32_t, std::set<char32_t>> reverseMappedCodepoints;

	for (const auto codepoint : m_info->Codepoints) {
		const auto mapped = translate_codepoint(codepoint);

		reverseMappedCodepoints[mapped].insert(codepoint);

		if (mapped < U' ')
			continue;

		glyph_metrics gm;
		if (!m_font->try_get_glyph_metrics(mapped, gm))
			continue;

		const auto remainingOffset = gm.X1 + m_info->HorizontalOffset;
		if (remainingOffset >= 0) {
			gm.AdvanceX = gm.AdvanceX + m_info->LetterSpacing;
			gm.translate(m_info->HorizontalOffset, m_info->BaselineShift);

		} else {
			gm.AdvanceX = gm.AdvanceX + m_info->LetterSpacing - gm.X1;
			gm.translate(-gm.X1, m_info->BaselineShift);

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

	m_kerningPairs.emplace(m_font->all_kerning_pairs());
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

char32_t xivres::fontgen::wrapping_fixed_size_font::uniqid_to_glyph(const void* pc) const {
	return m_font->uniqid_to_glyph(pc);
}

const void* xivres::fontgen::wrapping_fixed_size_font::get_base_font_glyph_uniqid(char32_t codepoint) const {
	codepoint = translate_codepoint(codepoint);

	if (!m_info->Codepoints.contains(codepoint))
		return nullptr;

	return m_font->get_base_font_glyph_uniqid(codepoint);
}

bool xivres::fontgen::wrapping_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const {
	codepoint = translate_codepoint(codepoint);

	if (!m_font->try_get_glyph_metrics(codepoint, gm))
		return false;

	const auto remainingOffset = gm.X1 + m_info->HorizontalOffset;
	if (remainingOffset >= 0) {
		gm.translate(m_info->HorizontalOffset, m_info->BaselineShift);
		gm.AdvanceX += m_info->LetterSpacing;
	} else {
		gm.translate(-gm.X1, m_info->BaselineShift);
		gm.AdvanceX += m_info->LetterSpacing - gm.X1;
	}

	return true;
}

const std::set<char32_t>& xivres::fontgen::wrapping_fixed_size_font::all_codepoints() const {
	return m_info->Codepoints;
}

int xivres::fontgen::wrapping_fixed_size_font::line_height() const {
	return m_font->line_height();
}

int xivres::fontgen::wrapping_fixed_size_font::ascent() const {
	return m_font->ascent();
}

float xivres::fontgen::wrapping_fixed_size_font::font_size() const {
	return m_font->font_size();
}

std::string xivres::fontgen::wrapping_fixed_size_font::subfamily_name() const {
	return m_font->subfamily_name();
}

std::string xivres::fontgen::wrapping_fixed_size_font::family_name() const {
	return m_font->family_name();
}

xivres::fontgen::wrapping_fixed_size_font::wrapping_fixed_size_font(std::shared_ptr<const fixed_size_font> font, const wrap_modifiers& wrapModifiers) : m_font(std::move(font)) {
	auto info = std::make_shared<info_t>();
	info->LetterSpacing = wrapModifiers.LetterSpacing;
	info->HorizontalOffset = wrapModifiers.HorizontalOffset;
	info->BaselineShift = wrapModifiers.BaselineShift;

	std::set<char32_t> codepoints;
	for (const auto& c : m_font->all_codepoints()) {
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

xivres::fontgen::wrapping_fixed_size_font::wrapping_fixed_size_font() = default;

xivres::fontgen::wrapping_fixed_size_font::wrapping_fixed_size_font(const wrapping_fixed_size_font & r) = default;

xivres::fontgen::wrapping_fixed_size_font::wrapping_fixed_size_font(wrapping_fixed_size_font && r) noexcept = default;

xivres::fontgen::wrapping_fixed_size_font& xivres::fontgen::wrapping_fixed_size_font::operator=(const wrapping_fixed_size_font & r) = default;

xivres::fontgen::wrapping_fixed_size_font& xivres::fontgen::wrapping_fixed_size_font::operator=(wrapping_fixed_size_font && r) noexcept = default;
