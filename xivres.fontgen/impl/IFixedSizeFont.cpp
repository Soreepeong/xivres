#include "../include/xivres.fontgen/IFixedSizeFont.h"

void xivres::fontgen::GlyphMetrics::AdjustToIntersection(GlyphMetrics& r, MetricType srcWidth, MetricType srcHeight, MetricType destWidth, MetricType destHeight) {
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

xivres::fontgen::GlyphMetrics& xivres::fontgen::GlyphMetrics::Translate(MetricType x, MetricType y) {
	X1 += x;
	X2 += x;
	Y1 += y;
	Y2 += y;
	return *this;
}

xivres::fontgen::GlyphMetrics& xivres::fontgen::GlyphMetrics::ExpandToFit(const GlyphMetrics& r) {
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

const void* xivres::fontgen::DefaultAbstractFixedSizeFont::GetBaseFontGlyphUniqid(char32_t c) const {
	const auto& codepoints = GetAllCodepoints();
	const auto it = codepoints.find(c);
	return it == codepoints.end() ? nullptr : &*it;
}

char32_t xivres::fontgen::DefaultAbstractFixedSizeFont::UniqidToGlyph(const void* pc) const {
	const auto c = *reinterpret_cast<const char32_t*>(pc);
	return GetAllCodepoints().contains(c) ? c : 0;
}

int xivres::fontgen::DefaultAbstractFixedSizeFont::GetAdjustedAdvanceX(char32_t left, char32_t right) const {
	GlyphMetrics gm;
	if (!GetGlyphMetrics(left, gm))
		return 0;

	const auto& kerningPairs = GetAllKerningPairs();
	if (const auto it = kerningPairs.find(std::make_pair(left, right)); it != kerningPairs.end())
		return gm.AdvanceX + it->second;

	return gm.AdvanceX;
}

const xivres::fontgen::IFixedSizeFont* xivres::fontgen::EmptyFixedSizeFont::GetBaseFont(char32_t codepoint) const {
	return this;
}

std::shared_ptr<xivres::fontgen::IFixedSizeFont> xivres::fontgen::EmptyFixedSizeFont::GetThreadSafeView() const {
	return std::make_shared<EmptyFixedSizeFont>(*this);
}

bool xivres::fontgen::EmptyFixedSizeFont::Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	return false;
}

bool xivres::fontgen::EmptyFixedSizeFont::Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const {
	return false;
}

int xivres::fontgen::EmptyFixedSizeFont::GetAdjustedAdvanceX(char32_t left, char32_t right) const {
	return 0;
}

const std::map<std::pair<char32_t, char32_t>, int>& xivres::fontgen::EmptyFixedSizeFont::GetAllKerningPairs() const {
	static const std::map<std::pair<char32_t, char32_t>, int> s_empty;
	return s_empty;
}

char32_t xivres::fontgen::EmptyFixedSizeFont::UniqidToGlyph(const void* pc) const {
	return 0;
}

const void* xivres::fontgen::EmptyFixedSizeFont::GetBaseFontGlyphUniqid(char32_t c) const {
	return nullptr;
}

bool xivres::fontgen::EmptyFixedSizeFont::GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const {
	gm.Clear();
	return false;
}

const std::set<char32_t>& xivres::fontgen::EmptyFixedSizeFont::GetAllCodepoints() const {
	static const std::set<char32_t> s_empty;
	return s_empty;
}

int xivres::fontgen::EmptyFixedSizeFont::GetLineHeight() const {
	return m_fontDef.LineHeight;
}

int xivres::fontgen::EmptyFixedSizeFont::GetAscent() const {
	return m_fontDef.Ascent;
}

float xivres::fontgen::EmptyFixedSizeFont::GetSize() const {
	return m_size;
}

std::string xivres::fontgen::EmptyFixedSizeFont::GetSubfamilyName() const {
	return "Regular";
}

std::string xivres::fontgen::EmptyFixedSizeFont::GetFamilyName() const {
	return "Empty";
}

xivres::fontgen::EmptyFixedSizeFont::EmptyFixedSizeFont(float size, CreateStruct fontDef)
	: m_size(size)
	, m_fontDef(fontDef) {
}

xivres::fontgen::EmptyFixedSizeFont::EmptyFixedSizeFont() = default;

xivres::fontgen::EmptyFixedSizeFont::EmptyFixedSizeFont(EmptyFixedSizeFont&&) noexcept = default;

xivres::fontgen::EmptyFixedSizeFont::EmptyFixedSizeFont(const EmptyFixedSizeFont & r) = default;

xivres::fontgen::EmptyFixedSizeFont& xivres::fontgen::EmptyFixedSizeFont::operator=(EmptyFixedSizeFont&&) noexcept = default;

xivres::fontgen::EmptyFixedSizeFont& xivres::fontgen::EmptyFixedSizeFont::operator=(const EmptyFixedSizeFont&) = default;
