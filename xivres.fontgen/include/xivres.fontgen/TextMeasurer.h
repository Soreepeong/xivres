#ifndef _XIVRES_FONTGENERATOR_TEXTMEASURER_H_
#define _XIVRES_FONTGENERATOR_TEXTMEASURER_H_

#include "IFixedSizeFont.h"

namespace xivres::fontgen {
	struct TextMeasureResult {
		struct CharacterInfo {
			char32_t Codepoint;
			char32_t Displayed;
			int X;
			int Y;
			GlyphMetrics Metrics;
		};

		GlyphMetrics Occupied;
		std::vector<CharacterInfo> Characters;

		void DrawTo(xivres::texture::memory_mipmap_stream& mipmapStream, const IFixedSizeFont& fontFace, int x, int y, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const;

		std::shared_ptr<xivres::texture::memory_mipmap_stream> CreateMipmap(const IFixedSizeFont& fontFace, util::RGBA8888 fgColor, util::RGBA8888 bgColor, int pad = 0) const;
	};

	struct TextMeasurer {
		const IFixedSizeFont& FontFace;
		int MaxWidth = (std::numeric_limits<int>::max)();
		int MaxHeight = (std::numeric_limits<int>::max)();
		bool UseKerning = true;
		std::optional<int> LineHeight = std::nullopt;
		const char32_t* FallbackCharacters = U"\u3013-?";
		std::vector<bool> IsCharacterWhiteSpace;
		std::vector<bool> IsCharacterWordBreakPoint;
		std::vector<bool> IsCharacterControlCharacter;

		TextMeasurer(const IFixedSizeFont& fontFace);

		TextMeasurer& WithUseKerning(bool use);

		TextMeasurer& WithMaxWidth(int width = (std::numeric_limits<int>::max)());

		TextMeasurer& WithMaxHeight(int height = (std::numeric_limits<int>::max)());

		TextMeasurer& WithLineHeight(std::optional<int> lineHeight = std::nullopt);

		TextMeasurer& WithFallbackCharacters(char32_t* fallbackCharacters);

		template <class TStringElem, class TStringTraits = std::char_traits<TStringElem>, class TStringAlloc = std::allocator<TStringElem>>
		TextMeasureResult Measure(const std::basic_string<TStringElem, TStringTraits, TStringAlloc>& pcszString) const {
			return Measure(&pcszString[0], pcszString.size());
		}

		template <class TStringElem, class TStringTraits = std::char_traits<TStringElem>>
		TextMeasureResult Measure(const std::basic_string_view<TStringElem, TStringTraits>& pcszString) const {
			return Measure(&pcszString[0], pcszString.size());
		}

		template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
		TextMeasureResult Measure(const T* pcszString, size_t nLength = (std::numeric_limits<size_t>::max)()) const {
			if (nLength == (std::numeric_limits<size_t>::max)())
				nLength = std::char_traits<T>::length(pcszString);

			TextMeasureResult res{};
			res.Characters.reserve(nLength);
			for (auto pc = pcszString, pc_ = pc + nLength; pc < pc_; pc++) {
				char32_t c = *pc;
				if (c == '\r') {
					if (pc + 1 < pc_ && *(pc + 1) == '\n')
						continue;
					c = '\n';
				}

				pc += util::unicode::decode(c, pc, pc_ - pc) - 1;
				res.Characters.emplace_back(c, c, 0, 0, GlyphMetrics{});
			}

			return Measure(res);
		}

	private:
		TextMeasureResult& Measure(TextMeasureResult& res) const;
	};
}

#endif
