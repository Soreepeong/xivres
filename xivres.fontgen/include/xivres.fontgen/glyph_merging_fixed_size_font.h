#ifndef XIVRES_FONTGENERATOR_GLYPHMERGINGFIXEDSIZEFONT_H_
#define XIVRES_FONTGENERATOR_GLYPHMERGINGFIXEDSIZEFONT_H_

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "fixed_size_font.h"

namespace xivres::fontgen {
	// Shapes that a text can be put in. Coordinates of shapes are in units of 1/1000 em, with y growing downwards, and
	// the baseline at y = 880.
	enum class glyph_merge_shape : uint8_t {
		None,
		AmPm,
		Ime,
		Box,

		// Digits are smaller than capitals, and numbers of two or more digits are smaller still, in a slightly smaller box.
		NumberBox,

		HollowBox,
		Hexagon,
		Rhombus,
		Bozja,
		Time,
		Custom,

		// The glyph of the codepoint in the font of the element, such as one drawn from an SVG file. The text area is
		// the custom one, or the middle of the ink of the glyph.
		Glyph,
	};

	enum class glyph_merge_text_mode : uint8_t {
		// The text is cut out of the shape.
		Subtract,

		// The text is cut out of the shape, and drawn where it is outside of the shape.
		Difference,
	};

	enum class glyph_merge_fit_mode : uint8_t {
		// Narrows the text down to 70% of its width first, and then shrinks it.
		CondenseThenShrink,
		Shrink,
		Overflow,
	};

	enum class glyph_merge_line_alignment : uint8_t {
		Left,
		Center,
		Right,
	};

	struct glyph_merge_mapping {
		// Each codepoint shows the text of the same index.
		std::u32string Codepoints;

		// Lines are separated by '\n'.
		std::vector<std::u32string> Texts;

		glyph_merge_shape Shape = glyph_merge_shape::Box;
		glyph_merge_text_mode TextMode = glyph_merge_text_mode::Subtract;

		// For glyph_merge_shape::Custom: SVG path data, in the coordinates of shapes, and the advance width.
		std::string CustomPath;
		float CustomAdvance = 1000.f;

		// For glyph_merge_shape::Custom: a whole SVG document in the coordinates of shapes, drawn instead of CustomPath if
		// not empty, so that its elements are filled each by itself, with their own fill rules, strokes, and colors.
		std::string CustomSvg;

		// For glyph_merge_shape::Custom and Glyph: the area that the text is fitted into, as x1, y1, x2, and y2 in the
		// coordinates of shapes; when empty, the middle of the bounding box of the shape.
		std::optional<std::array<float, 4>> CustomTextArea;
	};

	struct glyph_merge_params {
		// Size of the text in pixels; when empty, the text fills the height of the text area of the shape.
		std::optional<float> TextSize;
		glyph_merge_fit_mode FitMode = glyph_merge_fit_mode::CondenseThenShrink;
		float TextOffsetX = 0.f;
		float TextOffsetY = 0.f;
		float LetterSpacing = 0.f;
		float LineSpacing = 0.f;
		glyph_merge_line_alignment LineAlignment = glyph_merge_line_alignment::Center;
		std::vector<glyph_merge_mapping> Mappings;
	};

	// Draws texts, optionally in shapes, as the glyphs of codepoints; used for the boxed glyphs in the private use area
	// of the game fonts.
	class glyph_merging_fixed_size_font : public default_abstract_fixed_size_font {
	public:
		// Creates the font that draws the texts, at the given size, and horizontally scaled by condense on top of any other
		// transformation of the text. Renderers differ in how they apply transformation matrices, so composing them is
		// left to the caller.
		using text_font_factory = std::function<std::shared_ptr<fixed_size_font>(float size, float condense)>;

	private:
		struct info {
			std::shared_ptr<const fixed_size_font> BaseFont;
			glyph_merge_params Params;
			text_font_factory TextFontFactory;
			std::set<char32_t> Codepoints;
			std::map<char32_t, std::pair<size_t, size_t>> MappingIndices;
		};

		struct rendered_glyph {
			glyph_metrics Metrics;
			std::vector<uint8_t> Alpha;
		};

		std::shared_ptr<const info> m_info;
		mutable std::map<char32_t, rendered_glyph> m_glyphs;
		mutable std::map<std::pair<int, int>, std::shared_ptr<fixed_size_font>> m_textFonts;

	public:
		// baseFont is the font of the element, at the size of the element; it decides the size of the shapes, the
		// ascent, and the line height.
		glyph_merging_fixed_size_font(std::shared_ptr<const fixed_size_font> baseFont, glyph_merge_params params, text_font_factory textFontFactory);

		glyph_merging_fixed_size_font();
		glyph_merging_fixed_size_font(const glyph_merging_fixed_size_font& r);
		glyph_merging_fixed_size_font(glyph_merging_fixed_size_font&& r) noexcept;
		glyph_merging_fixed_size_font& operator=(const glyph_merging_fixed_size_font& r);
		glyph_merging_fixed_size_font& operator=(glyph_merging_fixed_size_font&& r) noexcept;
		~glyph_merging_fixed_size_font() override = default;

		[[nodiscard]] std::string family_name() const override;

		[[nodiscard]] std::string subfamily_name() const override;

		[[nodiscard]] float font_size() const override;

		[[nodiscard]] int ascent() const override;

		[[nodiscard]] int line_height() const override;

		[[nodiscard]] const std::set<char32_t>& all_codepoints() const override;

		[[nodiscard]] bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const override;

		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const override;

		bool draw(char32_t codepoint, util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::b8g8r8a8 fgColor, util::b8g8r8a8 bgColor) const override;

		bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		[[nodiscard]] std::shared_ptr<fixed_size_font> get_threadsafe_view() const override;

		[[nodiscard]] const fixed_size_font* get_base_font(char32_t codepoint) const override;

		[[nodiscard]] std::optional<float> get_baseline(uint32_t baselineTag) const override;

		// Returns the advance width of a built-in shape in units of 1/1000 em, or 0 for glyph_merge_shape::None and Custom.
		[[nodiscard]] static float get_shape_advance(glyph_merge_shape shape);

		// Returns the shape that the glyph of a codepoint is drawn in, as SVG path data in the coordinates of shapes, or
		// nothing if the glyph has no shape.
		[[nodiscard]] std::optional<std::string> get_shape_path_data(char32_t codepoint) const;

	private:
		[[nodiscard]] const rendered_glyph* get_rendered_glyph(char32_t codepoint) const;

		[[nodiscard]] rendered_glyph render(char32_t codepoint, const glyph_merge_mapping& mapping, const std::u32string& text) const;

		[[nodiscard]] const fixed_size_font& get_text_font(float size, float condense) const;
	};
}

#endif
