#include "../include/xivres.fontgen/fixed_size_font.h"

void xivres::fontgen::glyph_metrics::adjust_to_intersection(glyph_metrics& r, MetricType srcWidth, MetricType srcHeight, MetricType destWidth, MetricType destHeight) {
	if (X1 < 0) {
		r.X1 -= X1;
		X1 = 0;
	}
	if (r.X1 < 0) {
		X1 -= r.X1;
		r.X1 = 0;
	}
	if (Y1 < 0) {
		r.Y1 -= Y1;
		Y1 = 0;
	}
	if (r.Y1 < 0) {
		Y1 -= r.Y1;
		r.Y1 = 0;
	}
	if (X2 >= srcWidth) {
		r.X2 -= X2 - srcWidth;
		X2 = srcWidth;
	}
	if (r.X2 >= destWidth) {
		X2 -= r.X2 - destWidth;
		r.X2 = destWidth;
	}
	if (Y2 >= srcHeight) {
		r.Y2 -= Y2 - srcHeight;
		Y2 = srcHeight;
	}
	if (r.Y2 >= destHeight) {
		Y2 -= r.Y2 - destHeight;
		r.Y2 = destHeight;
	}

	if (X1 >= X2 || r.X1 >= r.X2 || Y1 >= Y2 || r.Y1 >= r.Y2)
		*this = r = {};
}

xivres::fontgen::glyph_metrics& xivres::fontgen::glyph_metrics::translate(MetricType x, MetricType y) {
	X1 += x;
	X2 += x;
	Y1 += y;
	Y2 += y;
	return *this;
}

xivres::fontgen::glyph_metrics& xivres::fontgen::glyph_metrics::expand_to_fit(const glyph_metrics& r) {
	const auto prevLeft = X1;
	X1 = (std::min)(X1, r.X1);
	Y1 = (std::min)(Y1, r.Y1);
	X2 = (std::max)(X2, r.X2);
	Y2 = (std::max)(Y2, r.Y2);
	if (prevLeft + AdvanceX > r.X1 + r.AdvanceX)
		AdvanceX = prevLeft + AdvanceX - X1;
	else
		AdvanceX = r.X1 + AdvanceX - X1;
	return *this;
}

const void* xivres::fontgen::default_abstract_fixed_size_font::get_base_font_glyph_uniqid(char32_t c) const {
	const auto& codepoints = all_codepoints();
	const auto it = codepoints.find(c);
	return it == codepoints.end() ? nullptr : &*it;
}

char32_t xivres::fontgen::default_abstract_fixed_size_font::uniqid_to_glyph(const void* pc) const {
	const auto c = *reinterpret_cast<const char32_t*>(pc);
	return all_codepoints().contains(c) ? c : 0;
}

int xivres::fontgen::default_abstract_fixed_size_font::get_adjusted_advance_width(char32_t left, char32_t right) const {
	glyph_metrics gm;
	if (!try_get_glyph_metrics(left, gm))
		return 0;

	const auto& kerningPairs = all_kerning_pairs();
	if (const auto it = kerningPairs.find(std::make_pair(left, right)); it != kerningPairs.end())
		return gm.AdvanceX + it->second;

	return gm.AdvanceX;
}

const xivres::fontgen::fixed_size_font* xivres::fontgen::empty_fixed_size_font::get_base_font(char32_t codepoint) const {
	return this;
}

std::shared_ptr<xivres::fontgen::fixed_size_font> xivres::fontgen::empty_fixed_size_font::get_threadsafe_view() const {
	return std::make_shared<empty_fixed_size_font>(*this);
}

bool xivres::fontgen::empty_fixed_size_font::draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	return false;
}

bool xivres::fontgen::empty_fixed_size_font::draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const {
	return false;
}

int xivres::fontgen::empty_fixed_size_font::get_adjusted_advance_width(char32_t left, char32_t right) const {
	return 0;
}

const std::map<std::pair<char32_t, char32_t>, int>& xivres::fontgen::empty_fixed_size_font::all_kerning_pairs() const {
	static const std::map<std::pair<char32_t, char32_t>, int> s_empty;
	return s_empty;
}

char32_t xivres::fontgen::empty_fixed_size_font::uniqid_to_glyph(const void* pc) const {
	return 0;
}

const void* xivres::fontgen::empty_fixed_size_font::get_base_font_glyph_uniqid(char32_t c) const {
	return nullptr;
}

bool xivres::fontgen::empty_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const {
	gm.clear();
	return false;
}

const std::set<char32_t>& xivres::fontgen::empty_fixed_size_font::all_codepoints() const {
	static const std::set<char32_t> s_empty;
	return s_empty;
}

int xivres::fontgen::empty_fixed_size_font::line_height() const {
	return m_fontDef.LineHeight;
}

int xivres::fontgen::empty_fixed_size_font::ascent() const {
	return m_fontDef.Ascent;
}

float xivres::fontgen::empty_fixed_size_font::font_size() const {
	return m_size;
}

std::string xivres::fontgen::empty_fixed_size_font::subfamily_name() const {
	return "Regular";
}

std::string xivres::fontgen::empty_fixed_size_font::family_name() const {
	return "Empty";
}

xivres::fontgen::empty_fixed_size_font::empty_fixed_size_font(float size, create_struct fontDef)
	: m_size(size)
	, m_fontDef(fontDef) {
}

xivres::fontgen::empty_fixed_size_font::empty_fixed_size_font() = default;

xivres::fontgen::empty_fixed_size_font::empty_fixed_size_font(empty_fixed_size_font&&) noexcept = default;

xivres::fontgen::empty_fixed_size_font::empty_fixed_size_font(const empty_fixed_size_font & r) = default;

xivres::fontgen::empty_fixed_size_font& xivres::fontgen::empty_fixed_size_font::operator=(empty_fixed_size_font&&) noexcept = default;

xivres::fontgen::empty_fixed_size_font& xivres::fontgen::empty_fixed_size_font::operator=(const empty_fixed_size_font&) = default;

void xivres::fontgen::font_render_transformation_matrix::SetIdentity() {
	M11 = M22 = 1.f;
	M12 = M21 = 0.f;
}
