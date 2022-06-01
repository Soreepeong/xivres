#include "../include/xivres.fontgen/TextMeasurer.h"

std::shared_ptr<xivres::MemoryMipmapStream> xivres::fontgen::TextMeasureResult::CreateMipmap(const IFixedSizeFont& fontFace, RGBA8888 fgColor, RGBA8888 bgColor, int pad) const {
	auto res = std::make_shared<xivres::MemoryMipmapStream>(
		pad * 2 + Occupied.X2 - (std::min)(0, Occupied.X1),
		pad * 2 + Occupied.Y2 - (std::min)(0, Occupied.Y1),
		1,
		xivres::TextureFormat::A8R8G8B8);
	std::ranges::fill(res->View<RGBA8888>(), bgColor);
	DrawTo(
		*res,
		fontFace,
		pad - (std::min)(0, Occupied.X1),
		pad - (std::min)(0, Occupied.Y1),
		fgColor,
		bgColor);
	return res;
}

void xivres::fontgen::TextMeasureResult::DrawTo(xivres::MemoryMipmapStream& mipmapStream, const IFixedSizeFont& fontFace, int x, int y, RGBA8888 fgColor, RGBA8888 bgColor) const {
	const auto buf = mipmapStream.View<RGBA8888>();
	for (const auto& c : Characters) {
		if (c.Metrics.IsEffectivelyEmpty())
			continue;

		fontFace.Draw(
			c.Displayed,
			&buf[0],
			x + c.X, y + c.Y,
			mipmapStream.Width, mipmapStream.Height,
			fgColor,
			{});
	}
}

xivres::fontgen::TextMeasureResult& xivres::fontgen::TextMeasurer::Measure(TextMeasureResult& res) const {
	std::vector<size_t> lineBreakIndices;

	if (res.Characters.empty())
		return res;

	for (auto& curr : res.Characters) {
		if (curr.Codepoint < IsCharacterControlCharacter.size() && IsCharacterControlCharacter[curr.Codepoint])
			continue;

		if (!FontFace.GetGlyphMetrics(curr.Displayed, curr.Metrics)) {
			for (auto pfc = FallbackCharacters; (curr.Displayed = *pfc); pfc++) {
				if (FontFace.GetGlyphMetrics(curr.Displayed, curr.Metrics))
					break;
			}
		}
	}

	size_t lastBreakIndex = 0;
	for (size_t i = 1; i < res.Characters.size(); i++) {
		auto& prev = res.Characters[i - 1];
		auto& curr = res.Characters[i];

		if (prev.Codepoint == '\n') {
			lineBreakIndices.push_back(i);
			curr.X = 0;
			curr.Y = prev.Y + LineHeight.value_or(FontFace.GetLineHeight());
		} else {
			curr.X = prev.X + (UseKerning ? FontFace.GetAdjustedAdvanceX(prev.Displayed, curr.Displayed) : prev.Metrics.AdvanceX);
			curr.Y = prev.Y;
		}

		if (prev.Codepoint < IsCharacterWordBreakPoint.size() && IsCharacterWordBreakPoint[prev.Codepoint])
			lastBreakIndex = i;

		if (curr.Codepoint < IsCharacterWhiteSpace.size() && IsCharacterWhiteSpace[curr.Codepoint])
			continue;

		if (curr.X + curr.Metrics.X2 < MaxWidth)
			continue;

		if (!(prev.Codepoint < IsCharacterWhiteSpace.size() && IsCharacterWhiteSpace[prev.Codepoint]) && res.Characters[lastBreakIndex].X > 0)
			i = lastBreakIndex;
		else
			lastBreakIndex = i;
		res.Characters[i].X = 0;
		res.Characters[i].Y = res.Characters[i - 1].Y + LineHeight.value_or(FontFace.GetLineHeight());
		lineBreakIndices.push_back(i);
	}

	for (auto& elem : res.Characters) {
		elem.Metrics.Translate(elem.X, elem.Y);
		res.Occupied.ExpandToFit(elem.Metrics);
	}

	return res;
}

xivres::fontgen::TextMeasurer& xivres::fontgen::TextMeasurer::WithFallbackCharacters(char32_t* fallbackCharacters) {
	FallbackCharacters = fallbackCharacters;
	return *this;
}

xivres::fontgen::TextMeasurer& xivres::fontgen::TextMeasurer::WithLineHeight(std::optional<int> lineHeight) {
	LineHeight = lineHeight;
	return *this;
}

xivres::fontgen::TextMeasurer& xivres::fontgen::TextMeasurer::WithMaxHeight(int height) {
	MaxHeight = height;
	return *this;
}

xivres::fontgen::TextMeasurer& xivres::fontgen::TextMeasurer::WithMaxWidth(int width) {
	MaxWidth = width;
	return *this;
}

xivres::fontgen::TextMeasurer& xivres::fontgen::TextMeasurer::WithUseKerning(bool use) {
	UseKerning = use;
	return *this;
}

xivres::fontgen::TextMeasurer::TextMeasurer(const IFixedSizeFont& fontFace) : FontFace(fontFace) {
	IsCharacterWhiteSpace.resize(256);
	IsCharacterWhiteSpace[U' '] = true;
	IsCharacterWhiteSpace[U'\r'] = true;
	IsCharacterWhiteSpace[U'\n'] = true;
	IsCharacterWhiteSpace[U'\t'] = true;

	IsCharacterWordBreakPoint.resize(256);
	IsCharacterWordBreakPoint[U' '] = true;
	IsCharacterWordBreakPoint[U'\r'] = true;
	IsCharacterWordBreakPoint[U'\n'] = true;
	IsCharacterWordBreakPoint[U'\t'] = true;

	IsCharacterControlCharacter.resize(0x200d);
	IsCharacterControlCharacter[U'\r'] = true;
	IsCharacterControlCharacter[U'\n'] = true;
	IsCharacterControlCharacter[U'\t'] = true;
	IsCharacterControlCharacter[U'\x200c'] = true;  // Zero-width non joiner
}
