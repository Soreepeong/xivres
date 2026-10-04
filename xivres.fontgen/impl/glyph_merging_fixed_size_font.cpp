#include "../include/xivres.fontgen/glyph_merging_fixed_size_font.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <stdexcept>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include "../include/xivres.fontgen/image_fixed_size_font.h"
#include "xivres/util.bitmap_copy.h"

namespace {
	using xivres::fontgen::glyph_merge_shape;

	// Shapes are in units of 1/1000 em, with y growing downwards, and the baseline at y = 880.
	constexpr float ShapeUnitsPerEm = 1000.f;
	constexpr float ShapeBaselineY = 880.f;

	// Height of the capitals of texts drawn without a shape, when no text size is given; that of the level indicators of
	// the Lodestone web font.
	constexpr float ShapelessCapHeight = 630.f;

	// Distance between the baselines of the lines of a text, relative to the height of the capitals.
	constexpr float StackedLineAdvance = 1.11f;

	struct point {
		float x;
		float y;

		point operator+(const point& r) const { return {x + r.x, y + r.y}; }
		point operator-(const point& r) const { return {x - r.x, y - r.y}; }
		point operator*(float f) const { return {x * f, y * f}; }
	};

	// Points with FreeType outline tags; each contour starts with an on-curve point and closes by itself.
	struct path {
		std::vector<point> Points;
		std::vector<char> Tags;
		std::vector<size_t> ContourEnds;

		void move_to(point p) {
			close();
			Points.push_back(p);
			Tags.push_back(FT_CURVE_TAG_ON);
		}

		void line_to(point p) {
			Points.push_back(p);
			Tags.push_back(FT_CURVE_TAG_ON);
		}

		void quad_to(point c, point p) {
			Points.push_back(c);
			Tags.push_back(FT_CURVE_TAG_CONIC);
			line_to(p);
		}

		void cubic_to(point c1, point c2, point p) {
			Points.push_back(c1);
			Tags.push_back(FT_CURVE_TAG_CUBIC);
			Points.push_back(c2);
			Tags.push_back(FT_CURVE_TAG_CUBIC);
			line_to(p);
		}

		void close() {
			const auto begin = ContourEnds.empty() ? 0 : ContourEnds.back() + 1;
			if (Points.size() <= begin)
				return;

			// The contour closes by itself; drop an explicit closing point.
			if (Points.size() - begin > 1 && Tags.back() == FT_CURVE_TAG_ON
				&& std::abs(Points.back().x - Points[begin].x) < 0.001f && std::abs(Points.back().y - Points[begin].y) < 0.001f) {
				Points.pop_back();
				Tags.pop_back();
			}
			ContourEnds.push_back(Points.size() - 1);
		}

		void append(const path& r) {
			const auto offset = Points.size();
			Points.insert(Points.end(), r.Points.begin(), r.Points.end());
			Tags.insert(Tags.end(), r.Tags.begin(), r.Tags.end());
			for (const auto e : r.ContourEnds)
				ContourEnds.push_back(e + offset);
		}
	};

	// Adds a polygon with corners rounded by up to the given radius; corners are approximated with cubic curves.
	void add_rounded_polygon(path& p, const std::vector<point>& vertices, float radius) {
		constexpr auto Kappa = 0.5522847f;
		const auto n = vertices.size();
		std::vector<point> starts(n), ends(n);
		for (size_t i = 0; i < n; i++) {
			const auto& prev = vertices[(i + n - 1) % n];
			const auto& cur = vertices[i];
			const auto& next = vertices[(i + 1) % n];
			const auto toPrev = prev - cur;
			const auto toNext = next - cur;
			const auto lenPrev = std::hypot(toPrev.x, toPrev.y);
			const auto lenNext = std::hypot(toNext.x, toNext.y);
			const auto r = (std::min)({radius, lenPrev / 2, lenNext / 2});
			starts[i] = cur + toPrev * (r / lenPrev);
			ends[i] = cur + toNext * (r / lenNext);
		}

		p.move_to(ends[0]);
		for (size_t j = 1; j <= n; j++) {
			const auto i = j % n;
			p.line_to(starts[i]);
			p.cubic_to(starts[i] + (vertices[i] - starts[i]) * Kappa, ends[i] + (vertices[i] - ends[i]) * Kappa, ends[i]);
		}
		p.close();
	}

	std::vector<point> rect_vertices(float x1, float y1, float x2, float y2, bool reverse = false) {
		if (reverse)
			return {{x1, y1}, {x1, y2}, {x2, y2}, {x2, y1}};
		return {{x1, y1}, {x2, y1}, {x2, y2}, {x1, y2}};
	}

	struct shape_definition {
		float Advance = 0.f;
		path Path;

		// Area that the text is fitted into and centered in.
		float AreaX1 = 0.f, AreaY1 = 0.f, AreaX2 = 0.f, AreaY2 = 0.f;

		// Whether the text is drawn on the shape, instead of being combined by the text mode.
		bool DrawsText = false;
	};

	// Shapes other than the hollow box follow the glyphs of the Lodestone web font (FFXIV_Lodestone_SSF), whose units are
	// the same as these. The text areas span the capitals of the texts in those glyphs, from the top to the baseline.
	// characterCount is of the text that is put in the shape, which some shapes are sized by.
	shape_definition make_shape(glyph_merge_shape shape, size_t characterCount = 1) {
		shape_definition s;
		switch (shape) {
			case glyph_merge_shape::Box:
				s.Advance = 1000;
				add_rounded_polygon(s.Path, rect_vertices(71, 71, 929, 926), 110);
				s.AreaX1 = 165, s.AreaY1 = 159, s.AreaX2 = 835, s.AreaY2 = 850;
				break;

			case glyph_merge_shape::NumberBox:
				s.Advance = 1000;
				if (characterCount <= 1) {
					add_rounded_polygon(s.Path, rect_vertices(71, 71, 929, 926), 110);
					s.AreaX1 = 132, s.AreaY1 = 192, s.AreaX2 = 879, s.AreaY2 = 816;
				} else {
					add_rounded_polygon(s.Path, rect_vertices(75, 75, 926, 922), 110);
					s.AreaX1 = 154, s.AreaY1 = 199, s.AreaX2 = 827, s.AreaY2 = 793;
				}
				break;

			case glyph_merge_shape::Ime:
				s.Advance = 1000;
				add_rounded_polygon(s.Path, rect_vertices(28, 28, 972, 969), 121);
				s.AreaX1 = 105, s.AreaY1 = 111, s.AreaX2 = 895, s.AreaY2 = 880;
				break;

			case glyph_merge_shape::HollowBox:
				s.Advance = 1000;
				add_rounded_polygon(s.Path, rect_vertices(45, 45, 955, 955), 145);
				add_rounded_polygon(s.Path, rect_vertices(95, 95, 905, 905, true), 95);
				s.AreaX1 = 180, s.AreaY1 = 210, s.AreaX2 = 820, s.AreaY2 = 790;
				s.DrawsText = true;
				break;

			case glyph_merge_shape::AmPm:
				s.Advance = 750;
				add_rounded_polygon(s.Path, rect_vertices(41, 61, 699, 916), 110);
				s.AreaX1 = 107, s.AreaY1 = 136, s.AreaX2 = 626, s.AreaY2 = 841;
				break;

			case glyph_merge_shape::Hexagon:
				s.Advance = 1250;
				add_rounded_polygon(s.Path, {{287, 0}, {972, 0}, {1257, 499}, {972, 998}, {287, 998}, {1, 499}}, 130);
				s.AreaX1 = 263, s.AreaY1 = 196, s.AreaX2 = 983, s.AreaY2 = 812;
				break;

			case glyph_merge_shape::Rhombus:
				s.Advance = 1000;
				add_rounded_polygon(s.Path, {{500, -94}, {1094, 499}, {500, 1092}, {-94, 499}}, 236);
				s.AreaX1 = 185, s.AreaY1 = 178, s.AreaX2 = 815, s.AreaY2 = 822;
				break;

			case glyph_merge_shape::Bozja:
				s.Advance = 1750;
				add_rounded_polygon(s.Path, rect_vertices(168, 50, 1582, 948), 200);
				s.AreaX1 = 330, s.AreaY1 = 163, s.AreaX2 = 1420, s.AreaY2 = 837;
				break;

			case glyph_merge_shape::Time:
				s.Advance = 2000;
				add_rounded_polygon(s.Path, rect_vertices(63, 31, 1938, 967), 234);
				s.AreaX1 = 220, s.AreaY1 = 156, s.AreaX2 = 1780, s.AreaY2 = 852;
				break;

			case glyph_merge_shape::None:
			case glyph_merge_shape::Custom:
			case glyph_merge_shape::Glyph:
				break;
		}
		return s;
	}

	// Parses SVG path data: M, L, H, V, C, S, Q, T, A, and Z, absolute and relative.
	path parse_svg_path(std::string_view d) {
		path p;
		size_t i = 0;
		const auto skipSeparators = [&] {
			while (i < d.size() && (std::isspace(static_cast<unsigned char>(d[i])) || d[i] == ','))
				i++;
		};
		const auto nextIsNumber = [&] {
			skipSeparators();
			return i < d.size() && (std::isdigit(static_cast<unsigned char>(d[i])) || d[i] == '-' || d[i] == '+' || d[i] == '.');
		};
		const auto readNumber = [&]() -> float {
			skipSeparators();
			const auto begin = i;
			if (i < d.size() && (d[i] == '-' || d[i] == '+'))
				i++;
			auto seenDot = false, seenExponent = false;
			while (i < d.size()) {
				const auto c = d[i];
				if (std::isdigit(static_cast<unsigned char>(c)))
					i++;
				else if (c == '.' && !seenDot && !seenExponent)
					seenDot = true, i++;
				else if ((c == 'e' || c == 'E') && !seenExponent) {
					seenExponent = true, i++;
					if (i < d.size() && (d[i] == '-' || d[i] == '+'))
						i++;
				} else
					break;
			}
			if (begin == i)
				throw std::invalid_argument("Invalid number in path data");
			return std::stof(std::string(d.substr(begin, i - begin)));
		};
		const auto readFlag = [&]() -> bool {
			skipSeparators();
			if (i >= d.size() || (d[i] != '0' && d[i] != '1'))
				throw std::invalid_argument("Invalid flag in path data");
			return d[i++] == '1';
		};

		point cur{0, 0}, start{0, 0}, lastControl{0, 0};
		char lastCommand = 0;
		char command = 0;
		while (true) {
			skipSeparators();
			if (i >= d.size())
				break;
			if (std::isalpha(static_cast<unsigned char>(d[i])))
				command = d[i++];
			else if (!command)
				throw std::invalid_argument("Path data must begin with a command");

			const auto relative = std::islower(static_cast<unsigned char>(command)) != 0;
			const auto base = relative ? cur : point{0, 0};
			const auto upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
			switch (upper) {
				case 'M':
					cur = base + point{readNumber(), readNumber()};
					start = cur;
					p.move_to(cur);
					// Subsequent pairs are lines.
					command = relative ? 'l' : 'L';
					break;
				case 'L':
					cur = base + point{readNumber(), readNumber()};
					p.line_to(cur);
					break;
				case 'H':
					cur = {(relative ? cur.x : 0) + readNumber(), cur.y};
					p.line_to(cur);
					break;
				case 'V':
					cur = {cur.x, (relative ? cur.y : 0) + readNumber()};
					p.line_to(cur);
					break;
				case 'C': {
					const auto c1 = base + point{readNumber(), readNumber()};
					const auto c2 = base + point{readNumber(), readNumber()};
					cur = base + point{readNumber(), readNumber()};
					p.cubic_to(c1, c2, cur);
					lastControl = c2;
					break;
				}
				case 'S': {
					const auto upperLast = static_cast<char>(std::toupper(static_cast<unsigned char>(lastCommand)));
					const auto c1 = upperLast == 'C' || upperLast == 'S' ? cur * 2 - lastControl : cur;
					const auto c2 = base + point{readNumber(), readNumber()};
					cur = base + point{readNumber(), readNumber()};
					p.cubic_to(c1, c2, cur);
					lastControl = c2;
					break;
				}
				case 'Q': {
					const auto c = base + point{readNumber(), readNumber()};
					cur = base + point{readNumber(), readNumber()};
					p.quad_to(c, cur);
					lastControl = c;
					break;
				}
				case 'T': {
					const auto upperLast = static_cast<char>(std::toupper(static_cast<unsigned char>(lastCommand)));
					const auto c = upperLast == 'Q' || upperLast == 'T' ? cur * 2 - lastControl : cur;
					cur = base + point{readNumber(), readNumber()};
					p.quad_to(c, cur);
					lastControl = c;
					break;
				}
				case 'A': {
					auto rx = std::abs(readNumber());
					auto ry = std::abs(readNumber());
					const auto angle = readNumber() * std::numbers::pi_v<float> / 180.f;
					const auto largeArc = readFlag();
					const auto sweep = readFlag();
					const auto end = base + point{readNumber(), readNumber()};
					if (rx == 0 || ry == 0) {
						p.line_to(end);
						cur = end;
						break;
					}

					// Endpoint to center parameterization; https://www.w3.org/TR/SVG11/implnote.html#ArcImplementationNotes
					const auto cosA = std::cos(angle), sinA = std::sin(angle);
					const auto dx = (cur.x - end.x) / 2, dy = (cur.y - end.y) / 2;
					const auto x1p = cosA * dx + sinA * dy, y1p = -sinA * dx + cosA * dy;
					if (const auto lambda = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry); lambda > 1) {
						rx *= std::sqrt(lambda);
						ry *= std::sqrt(lambda);
					}
					const auto num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
					const auto den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
					auto coef = std::sqrt((std::max)(0.f, num / den));
					if (largeArc == sweep)
						coef = -coef;
					const auto cxp = coef * rx * y1p / ry, cyp = -coef * ry * x1p / rx;
					const auto cx = cosA * cxp - sinA * cyp + (cur.x + end.x) / 2;
					const auto cy = sinA * cxp + cosA * cyp + (cur.y + end.y) / 2;
					const auto vectorAngle = [](float ux, float uy, float vx, float vy) {
						return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
					};
					const auto theta1 = vectorAngle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
					auto delta = vectorAngle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
					if (!sweep && delta > 0)
						delta -= 2 * std::numbers::pi_v<float>;
					else if (sweep && delta < 0)
						delta += 2 * std::numbers::pi_v<float>;

					// Split into pieces of at most 90 degrees, each approximated with a cubic curve.
					const auto pieces = static_cast<int>(std::ceil(std::abs(delta) / (std::numbers::pi_v<float> / 2) - 0.001f));
					const auto step = delta / static_cast<float>((std::max)(1, pieces));
					const auto k = 4.f / 3.f * std::tan(step / 4);
					const auto at = [&](float t) {
						return point{cx + rx * std::cos(t) * cosA - ry * std::sin(t) * sinA, cy + rx * std::cos(t) * sinA + ry * std::sin(t) * cosA};
					};
					const auto derivative = [&](float t) {
						return point{-rx * std::sin(t) * cosA - ry * std::cos(t) * sinA, -rx * std::sin(t) * sinA + ry * std::cos(t) * cosA};
					};
					for (int j = 0; j < (std::max)(1, pieces); j++) {
						const auto t1 = theta1 + step * static_cast<float>(j);
						const auto t2 = t1 + step;
						const auto p1 = at(t1), p2 = j + 1 == pieces ? end : at(t2);
						p.cubic_to(p1 + derivative(t1) * k, p2 - derivative(t2) * k, p2);
					}
					cur = end;
					break;
				}
				case 'Z':
					p.close();
					cur = start;
					break;
				default:
					throw std::invalid_argument("Unsupported command in path data");
			}
			lastCommand = command;
			if (upper == 'Z' && !nextIsNumber())
				command = 0;
		}
		p.close();
		return p;
	}

	// Coverage of a path drawn into a buffer whose top left pixel is at (originX, originY) in pixels.
	void draw_path(FT_Library library, const path& p, float scale, float baselineY, int originX, int originY, int width, int height, std::vector<uint8_t>& buffer) {
		if (p.Points.empty() || width <= 0 || height <= 0)
			return;

		std::vector<FT_Vector> points(p.Points.size());
		for (size_t i = 0; i < points.size(); i++) {
			const auto x = p.Points[i].x * scale - static_cast<float>(originX);
			const auto y = baselineY - (ShapeBaselineY - p.Points[i].y) * scale - static_cast<float>(originY);
			points[i].x = static_cast<FT_Pos>(std::lround(x * 64.f));
			points[i].y = static_cast<FT_Pos>(std::lround((static_cast<float>(height) - y) * 64.f));
		}
		std::vector<char> tags(p.Tags);
		std::vector<short> contours(p.ContourEnds.size());
		std::ranges::transform(p.ContourEnds, contours.begin(), [](size_t e) { return static_cast<short>(e); });

		FT_Outline outline{};
		outline.n_points = static_cast<short>(points.size());
		outline.n_contours = static_cast<short>(contours.size());
		outline.points = points.data();
		outline.tags = reinterpret_cast<unsigned char*>(tags.data());
		outline.contours = reinterpret_cast<decltype(outline.contours)>(contours.data());
		outline.flags = FT_OUTLINE_NONE;

		FT_Bitmap bitmap{};
		bitmap.rows = height;
		bitmap.width = width;
		bitmap.pitch = width;
		bitmap.buffer = buffer.data();
		bitmap.num_grays = 256;
		bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
		FT_Outline_Get_Bitmap(library, &outline, &bitmap);
	}

	void get_path_bounds(const path& p, float scale, float baselineY, float& x1, float& y1, float& x2, float& y2) {
		x1 = y1 = (std::numeric_limits<float>::max)();
		x2 = y2 = (std::numeric_limits<float>::lowest)();
		for (const auto& pt : p.Points) {
			const auto x = pt.x * scale;
			const auto y = baselineY - (ShapeBaselineY - pt.y) * scale;
			x1 = (std::min)(x1, x), x2 = (std::max)(x2, x);
			y1 = (std::min)(y1, y), y2 = (std::max)(y2, y);
		}
	}

	std::vector<std::u32string> split_lines(const std::u32string& text) {
		std::vector<std::u32string> lines(1);
		for (const auto c : text) {
			if (c == U'\n')
				lines.emplace_back();
			else if (c != U'\r')
				lines.back().push_back(c);
		}
		return lines;
	}

	// Whether a text has nothing but symbols, such as + and arrows, besides spaces and line breaks.
	bool is_symbols_only(const std::u32string& text) {
		auto any = false;
		for (const auto c : text) {
			if (c == U' ' || c == U'\n' || c == U'\r')
				continue;

			const auto symbol = (c < 0x80 && std::ispunct(static_cast<int>(c)))
				|| c == U'\u00D7'  // multiplication sign
				|| c == U'\u00F7'  // division sign
				|| (c >= 0x2190 && c <= 0x2BFF);  // arrows, mathematical operators, technical and geometric shapes, dingbats
			if (!symbol)
				return false;
			any = true;
		}
		return any;
	}

	struct text_layout {
		struct glyph {
			// A glyph of a shaped line has its index, and a position of its origin on the baseline, which may be between
			// pixels; otherwise, the position is of the top left of the line that the codepoint is drawn at.
			char32_t Codepoint;
			uint32_t GlyphIndex;
			float X;
			float Y;
		};

		std::vector<glyph> Glyphs;

		// Ink bounds, relative to the top of the first line. Horizontal bounds are of the outlines of shaped glyphs,
		// which partially covered pixels may extend past.
		float InkX1 = 0, InkX2 = 0;
		int InkY1 = 0, InkY2 = 0;

		// Top of the capitals of the first line, and the baseline of the last line, relative to the top of the first line.
		int CapTop = 0;
		int LastBaseline = 0;

		// Width of the widest line, by advance.
		int AdvanceWidth = 0;
	};

	// Lines are aligned by their ink when the text is put in a shape; otherwise, they are aligned by their advances, so
	// that the side bearings of the glyphs are kept as they are in text of the font.
	text_layout layout_text(const xivres::fontgen::fixed_size_font& font, const std::vector<std::u32string>& lines, const xivres::fontgen::glyph_merge_params& params, bool alignByInk) {
		struct line {
			struct item {
				char32_t Codepoint;
				uint32_t GlyphIndex;
				float X;

				// Vertical offset of a shaped glyph from the baseline.
				float Y;

				// Horizontal extent of the ink, relative to X.
				float InkX1;
				float InkX2;

				// Vertical extent of the ink, relative to the top of the line.
				int InkY1;
				int InkY2;
			};

			std::vector<item> Glyphs;
			float InkX1 = (std::numeric_limits<float>::max)();
			float InkX2 = (std::numeric_limits<float>::lowest)();
			int Width = 0;
		};

		const auto letterSpacing = static_cast<int>(std::lround(params.LetterSpacing));
		std::vector<line> laidOutLines;
		auto maxInkWidth = 0.f;
		text_layout res;
		for (const auto& text : lines) {
			auto& l = laidOutLines.emplace_back();
			std::u32string available;
			for (const auto c : text) {
				if (font.all_codepoints().contains(c))
					available.push_back(c);
			}

			if (const auto shaped = font.shape_line(available, letterSpacing)) {
				// Shaped as a whole, with all the features of the font, including ligatures and contextual kerning, and
				// placed between pixels where the shaping says so, so that lines can be aligned closer than by pixels.
				for (const auto& g : shaped->Glyphs) {
					xivres::fontgen::glyph_metrics gm;
					float inkX1, inkX2;
					if (!font.try_get_glyph_index_metrics(g.GlyphIndex, 0, g.Y, gm) || !font.try_get_glyph_index_ink_extent(g.GlyphIndex, inkX1, inkX2))
						continue;
					gm.translate(0, font.ascent());
					l.Glyphs.push_back({0, g.GlyphIndex, g.X, g.Y, inkX1, inkX2, gm.Y1, gm.Y2});
					if (!gm.is_effectively_empty()) {
						l.InkX1 = (std::min)(l.InkX1, g.X + inkX1);
						l.InkX2 = (std::max)(l.InkX2, g.X + inkX2);
					}
				}
				l.Width = shaped->AdvanceWidth;
			} else {
				auto pen = 0;
				for (size_t i = 0; i < available.size(); i++) {
					const auto c = available[i];
					xivres::fontgen::glyph_metrics gm;
					if (!font.try_get_glyph_metrics(c, gm))
						continue;
					l.Glyphs.push_back({c, 0, static_cast<float>(pen), 0, static_cast<float>(gm.X1), static_cast<float>(gm.X2), gm.Y1, gm.Y2});
					if (!gm.is_effectively_empty()) {
						l.InkX1 = (std::min)(l.InkX1, static_cast<float>(pen + gm.X1));
						l.InkX2 = (std::max)(l.InkX2, static_cast<float>(pen + gm.X2));
					}
					pen += i + 1 < available.size() ? font.get_adjusted_advance_width(c, available[i + 1]) + letterSpacing : gm.AdvanceX;
				}
				l.Width = pen;
			}
			if (l.InkX1 > l.InkX2)
				l.InkX1 = l.InkX2 = 0;
			maxInkWidth = (std::max)(maxInkWidth, l.InkX2 - l.InkX1);
			res.AdvanceWidth = (std::max)(res.AdvanceWidth, l.Width);
		}

		auto capHeight = static_cast<int>(std::lround(font.ascent() * 0.7f));
		if (xivres::fontgen::glyph_metrics gm; font.try_get_glyph_metrics(U'H', gm) && !gm.is_effectively_empty())
			capHeight = font.ascent() - gm.Y1;

		// Lines are stacked closely, as in the AM/PM glyphs of the game, instead of by the line height of the font.
		const auto lineAdvance = static_cast<int>(std::lround(capHeight * StackedLineAdvance + params.LineSpacing));
		res.InkX1 = (std::numeric_limits<float>::max)();
		res.InkX2 = (std::numeric_limits<float>::lowest)();
		res.InkY1 = (std::numeric_limits<int>::max)();
		res.InkY2 = (std::numeric_limits<int>::min)();
		for (size_t i = 0; i < laidOutLines.size(); i++) {
			const auto& l = laidOutLines[i];

			// Glyphs drawn per codepoint stay on whole pixels.
			const auto shaped = !l.Glyphs.empty() && l.Glyphs.front().GlyphIndex;
			const auto lineWidth = alignByInk ? l.InkX2 - l.InkX1 : static_cast<float>(l.Width);
			const auto maxWidth = alignByInk ? maxInkWidth : static_cast<float>(res.AdvanceWidth);
			auto shift = alignByInk ? -l.InkX1 : 0.f;
			switch (params.LineAlignment) {
				case xivres::fontgen::glyph_merge_line_alignment::Left:
					break;
				case xivres::fontgen::glyph_merge_line_alignment::Center:
					shift += (maxWidth - lineWidth) / 2;
					break;
				case xivres::fontgen::glyph_merge_line_alignment::Right:
					shift += maxWidth - lineWidth;
					break;
			}
			if (!shaped)
				shift = std::round(shift);

			const auto y = static_cast<int>(i) * lineAdvance;
			for (const auto& g : l.Glyphs) {
				if (g.GlyphIndex)
					res.Glyphs.push_back({0, g.GlyphIndex, g.X + shift, static_cast<float>(y + font.ascent()) + g.Y});
				else
					res.Glyphs.push_back({g.Codepoint, 0, g.X + shift, static_cast<float>(y)});
				if (g.InkY1 < g.InkY2) {
					res.InkX1 = (std::min)(res.InkX1, g.X + shift + g.InkX1);
					res.InkX2 = (std::max)(res.InkX2, g.X + shift + g.InkX2);
					res.InkY1 = (std::min)(res.InkY1, y + g.InkY1);
					res.InkY2 = (std::max)(res.InkY2, y + g.InkY2);
				}
			}
		}
		if (res.InkX1 > res.InkX2 || res.InkY1 > res.InkY2) {
			res.InkX1 = res.InkX2 = 0;
			res.InkY1 = res.InkY2 = 0;
		}

		res.CapTop = font.ascent() - capHeight;
		res.LastBaseline = font.ascent() + static_cast<int>(laidOutLines.size() - 1) * lineAdvance;
		return res;
	}
}

xivres::fontgen::glyph_merging_fixed_size_font::glyph_merging_fixed_size_font(std::shared_ptr<const fixed_size_font> baseFont, glyph_merge_params params, text_font_factory textFontFactory) {
	auto info = std::make_shared<struct info>();
	info->BaseFont = std::move(baseFont);
	info->Params = std::move(params);
	info->TextFontFactory = std::move(textFontFactory);
	for (size_t i = 0; i < info->Params.Mappings.size(); i++) {
		const auto& mapping = info->Params.Mappings[i];
		for (size_t j = 0; j < mapping.Codepoints.size() && j < mapping.Texts.size(); j++) {
			if (mapping.Texts[j].empty())
				continue;
			if (info->MappingIndices.emplace(mapping.Codepoints[j], std::make_pair(i, j)).second)
				info->Codepoints.insert(mapping.Codepoints[j]);
		}
	}
	m_info = std::move(info);
}

xivres::fontgen::glyph_merging_fixed_size_font::glyph_merging_fixed_size_font() = default;
xivres::fontgen::glyph_merging_fixed_size_font::glyph_merging_fixed_size_font(const glyph_merging_fixed_size_font& r) = default;
xivres::fontgen::glyph_merging_fixed_size_font::glyph_merging_fixed_size_font(glyph_merging_fixed_size_font&& r) noexcept = default;
xivres::fontgen::glyph_merging_fixed_size_font& xivres::fontgen::glyph_merging_fixed_size_font::operator=(const glyph_merging_fixed_size_font& r) = default;
xivres::fontgen::glyph_merging_fixed_size_font& xivres::fontgen::glyph_merging_fixed_size_font::operator=(glyph_merging_fixed_size_font&& r) noexcept = default;

std::string xivres::fontgen::glyph_merging_fixed_size_font::family_name() const {
	return m_info->BaseFont->family_name();
}

std::string xivres::fontgen::glyph_merging_fixed_size_font::subfamily_name() const {
	return m_info->BaseFont->subfamily_name();
}

float xivres::fontgen::glyph_merging_fixed_size_font::font_size() const {
	return m_info->BaseFont->font_size();
}

int xivres::fontgen::glyph_merging_fixed_size_font::ascent() const {
	return m_info->BaseFont->ascent();
}

int xivres::fontgen::glyph_merging_fixed_size_font::line_height() const {
	return m_info->BaseFont->line_height();
}

const std::set<char32_t>& xivres::fontgen::glyph_merging_fixed_size_font::all_codepoints() const {
	return m_info->Codepoints;
}

bool xivres::fontgen::glyph_merging_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const {
	const auto glyph = get_rendered_glyph(codepoint);
	if (!glyph)
		return false;
	gm = glyph->Metrics;
	return true;
}

const std::map<std::pair<char32_t, char32_t>, int>& xivres::fontgen::glyph_merging_fixed_size_font::all_kerning_pairs() const {
	static const std::map<std::pair<char32_t, char32_t>, int> empty;
	return empty;
}

bool xivres::fontgen::glyph_merging_fixed_size_font::draw(char32_t codepoint, util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::b8g8r8a8 fgColor, util::b8g8r8a8 bgColor) const {
	const auto glyph = get_rendered_glyph(codepoint);
	if (!glyph)
		return false;

	auto dest = glyph->Metrics;
	dest.translate(drawX, drawY);
	auto src = dest;
	src.translate(-src.X1, -src.Y1);
	src.adjust_to_intersection(dest, src.width(), src.height(), destWidth, destHeight);
	if (src.is_effectively_empty() || dest.is_effectively_empty())
		return true;

	static const auto gammaTable = util::bitmap_copy::create_gamma_table(1.f);
	util::bitmap_copy::to_b8g8r8a8()
		.from(glyph->Alpha.data(), glyph->Metrics.width(), glyph->Metrics.height(), 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.back_color(bgColor)
		.gamma_table(gammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

bool xivres::fontgen::glyph_merging_fixed_size_font::draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	const auto glyph = get_rendered_glyph(codepoint);
	if (!glyph)
		return false;

	auto dest = glyph->Metrics;
	dest.translate(drawX, drawY);
	auto src = dest;
	src.translate(-src.X1, -src.Y1);
	src.adjust_to_intersection(dest, src.width(), src.height(), destWidth, destHeight);
	if (src.is_effectively_empty() || dest.is_effectively_empty())
		return true;

	static const auto gammaTable = util::bitmap_copy::create_gamma_table(1.f);
	util::bitmap_copy::to_l8()
		.from(glyph->Alpha.data(), glyph->Metrics.width(), glyph->Metrics.height(), 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, stride, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.fore_opacity(fgOpacity)
		.back_color(bgColor)
		.back_opacity(bgOpacity)
		.gamma_table(gammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

std::shared_ptr<xivres::fontgen::fixed_size_font> xivres::fontgen::glyph_merging_fixed_size_font::get_threadsafe_view() const {
	auto res = std::make_shared<glyph_merging_fixed_size_font>();
	auto info = std::make_shared<struct info>(*m_info);
	info->BaseFont = m_info->BaseFont->get_threadsafe_view();
	res->m_info = std::move(info);
	return res;
}

const xivres::fontgen::fixed_size_font* xivres::fontgen::glyph_merging_fixed_size_font::get_base_font(char32_t codepoint) const {
	return m_info->Codepoints.contains(codepoint) ? this : nullptr;
}

std::optional<float> xivres::fontgen::glyph_merging_fixed_size_font::get_baseline(uint32_t baselineTag) const {
	return m_info->BaseFont->get_baseline(baselineTag);
}

float xivres::fontgen::glyph_merging_fixed_size_font::get_shape_advance(glyph_merge_shape shape) {
	return make_shape(shape).Advance;
}

std::optional<std::string> xivres::fontgen::glyph_merging_fixed_size_font::get_shape_path_data(char32_t codepoint) const {
	const auto it = m_info->MappingIndices.find(codepoint);
	if (it == m_info->MappingIndices.end())
		return std::nullopt;

	const auto& mapping = m_info->Params.Mappings[it->second.first];
	if (mapping.Shape == glyph_merge_shape::Custom)
		return mapping.CustomPath.empty() || !mapping.CustomSvg.empty() ? std::nullopt : std::optional(mapping.CustomPath);

	const auto& text = it->second.second < mapping.Texts.size() ? mapping.Texts[it->second.second] : std::u32string();
	const auto characterCount = static_cast<size_t>(std::ranges::count_if(text, [](char32_t c) { return c != U' ' && c != U'\n' && c != U'\r'; }));
	const auto shape = make_shape(mapping.Shape, characterCount);
	if (shape.Path.Points.empty())
		return std::nullopt;

	// Contours close by themselves, so curves at the end of one go back to its first point.
	const auto& p = shape.Path;
	std::string res;
	const auto append = [&res](char command, std::initializer_list<point> points) {
		res += command;
		for (const auto& pt : points)
			res += std::format(" {:g},{:g}", pt.x, pt.y);
		res += ' ';
	};
	size_t begin = 0;
	for (const auto end : p.ContourEnds) {
		const auto at = [&](size_t i) { return p.Points[i <= end ? i : begin]; };
		append('M', {p.Points[begin]});
		for (auto i = begin + 1; i <= end;) {
			switch (p.Tags[i]) {
				case FT_CURVE_TAG_CONIC:
					append('Q', {at(i), at(i + 1)});
					i += 2;
					break;
				case FT_CURVE_TAG_CUBIC:
					append('C', {at(i), at(i + 1), at(i + 2)});
					i += 3;
					break;
				default:
					append('L', {p.Points[i]});
					i++;
					break;
			}
		}
		res += "Z ";
		begin = end + 1;
	}
	res.pop_back();
	return res;
}

const xivres::fontgen::glyph_merging_fixed_size_font::rendered_glyph* xivres::fontgen::glyph_merging_fixed_size_font::get_rendered_glyph(char32_t codepoint) const {
	if (const auto it = m_glyphs.find(codepoint); it != m_glyphs.end())
		return &it->second;

	const auto it = m_info->MappingIndices.find(codepoint);
	if (it == m_info->MappingIndices.end())
		return nullptr;

	const auto& mapping = m_info->Params.Mappings[it->second.first];
	rendered_glyph glyph;
	try {
		glyph = render(codepoint, mapping, mapping.Texts[it->second.second]);
	} catch (const std::exception&) {
		// An unusable text font or custom shape leaves the glyph empty.
		glyph = {};
	}
	return &m_glyphs.emplace(codepoint, std::move(glyph)).first->second;
}

const xivres::fontgen::fixed_size_font& xivres::fontgen::glyph_merging_fixed_size_font::get_text_font(float size, float condense) const {
	const auto key = std::make_pair(static_cast<int>(std::lround(size * 20.f)), static_cast<int>(std::lround(condense * 1000.f)));
	auto& font = m_textFonts[key];
	if (!font) {
		font = m_info->TextFontFactory(static_cast<float>(key.first) / 20.f, static_cast<float>(key.second) / 1000.f);
		if (!font)
			throw std::runtime_error("Text font unavailable");
	}
	return *font;
}

xivres::fontgen::glyph_merging_fixed_size_font::rendered_glyph xivres::fontgen::glyph_merging_fixed_size_font::render(char32_t codepoint, const glyph_merge_mapping& mapping, const std::u32string& text) const {
	const auto& params = m_info->Params;
	const auto size = m_info->BaseFont->font_size();
	const auto scale = size / ShapeUnitsPerEm;
	const auto baselineY = static_cast<float>(m_info->BaseFont->ascent());

	const auto characterCount = static_cast<size_t>(std::ranges::count_if(text, [](char32_t c) { return c != U' ' && c != U'\n' && c != U'\r'; }));
	auto shape = make_shape(mapping.Shape, characterCount);

	// Shapes that are drawn into pixels instead of from a path: the glyph of the element, or a whole SVG document. Alpha
	// is empty for the glyph of the element, which the base font draws.
	struct raster_shape {
		glyph_metrics Box;
		std::vector<uint8_t> Alpha;
	};
	std::optional<raster_shape> rasterShape;

	// Text is fitted into the given area, or the middle of the bounds of the shape, given in the coordinates of shapes.
	const auto setArea = [&](float x1, float y1, float x2, float y2) {
		if (mapping.CustomTextArea) {
			shape.AreaX1 = (*mapping.CustomTextArea)[0], shape.AreaY1 = (*mapping.CustomTextArea)[1];
			shape.AreaX2 = (*mapping.CustomTextArea)[2], shape.AreaY2 = (*mapping.CustomTextArea)[3];
		} else if (x1 < x2 && y1 < y2) {
			const auto insetX = (x2 - x1) * 0.15f, insetY = (y2 - y1) * 0.2f;
			shape.AreaX1 = x1 + insetX, shape.AreaX2 = x2 - insetX;
			shape.AreaY1 = y1 + insetY, shape.AreaY2 = y2 - insetY;
		}
	};
	const auto setAreaFromPixels = [&](const glyph_metrics& box) {
		setArea(
			static_cast<float>(box.X1) / scale,
			ShapeBaselineY - (baselineY - static_cast<float>(box.Y1)) / scale,
			static_cast<float>(box.X2) / scale,
			ShapeBaselineY - (baselineY - static_cast<float>(box.Y2)) / scale);
	};

	if (mapping.Shape == glyph_merge_shape::Glyph) {
		if (glyph_metrics gm; m_info->BaseFont->try_get_glyph_metrics(codepoint, gm)) {
			shape.Advance = static_cast<float>(gm.AdvanceX) / scale;
			if (!gm.is_effectively_empty()) {
				rasterShape = raster_shape{.Box = gm};
				setAreaFromPixels(gm);
			}
		}
	} else if (mapping.Shape == glyph_merge_shape::Custom && !mapping.CustomSvg.empty()) {
		shape.Advance = mapping.CustomAdvance;

		// Drawn with room around the line box, and trimmed to the ink.
		const auto bx1 = static_cast<int>(std::floor(-ShapeUnitsPerEm * scale));
		const auto bx2 = static_cast<int>(std::ceil(((std::max)(0.f, mapping.CustomAdvance) + ShapeUnitsPerEm) * scale));
		const auto by1 = static_cast<int>(std::floor(baselineY - 2 * ShapeUnitsPerEm * scale));
		const auto by2 = static_cast<int>(std::ceil(baselineY + ShapeUnitsPerEm * scale));
		const auto bw = bx2 - bx1, bh = by2 - by1;
		std::vector<uint8_t> coverage(static_cast<size_t>(bw) * bh);
		image_fixed_size_font::draw_svg_coverage(
			mapping.CustomSvg,
			{scale, 0, static_cast<float>(-bx1), 0, scale, baselineY - ShapeBaselineY * scale - static_cast<float>(by1)},
			bw, bh, coverage);

		auto tx1 = bw, ty1 = bh, tx2 = 0, ty2 = 0;
		for (auto y = 0; y < bh; y++) {
			for (auto x = 0; x < bw; x++) {
				if (coverage[static_cast<size_t>(y) * bw + x]) {
					tx1 = (std::min)(tx1, x), ty1 = (std::min)(ty1, y);
					tx2 = (std::max)(tx2, x + 1), ty2 = (std::max)(ty2, y + 1);
				}
			}
		}
		if (tx1 < tx2 && ty1 < ty2) {
			raster_shape r{.Box = {.X1 = bx1 + tx1, .Y1 = by1 + ty1, .X2 = bx1 + tx2, .Y2 = by1 + ty2}};
			r.Alpha.resize(static_cast<size_t>(tx2 - tx1) * (ty2 - ty1));
			for (auto y = ty1; y < ty2; y++)
				std::copy_n(&coverage[static_cast<size_t>(y) * bw + tx1], tx2 - tx1, &r.Alpha[static_cast<size_t>(y - ty1) * (tx2 - tx1)]);
			setAreaFromPixels(r.Box);
			rasterShape = std::move(r);
		}
	} else if (mapping.Shape == glyph_merge_shape::Custom) {
		shape.Path = parse_svg_path(mapping.CustomPath);
		shape.Advance = mapping.CustomAdvance;

		float x1, y1, x2, y2;
		get_path_bounds(shape.Path, 1.f, ShapeBaselineY, x1, y1, x2, y2);
		setArea(x1, y1, x2, y2);
	}
	const auto hasShape = !shape.Path.Points.empty() || rasterShape;
	const auto hasArea = shape.AreaX1 < shape.AreaX2 && shape.AreaY1 < shape.AreaY2;

	// Decide the size and the horizontal scale of the text from its layout at the size of the element.
	const auto lines = split_lines(text);
	auto textScale = params.TextSize ? *params.TextSize / size : 1.f;
	auto condense = 1.f;
	if (!hasShape && !params.TextSize) {
		// Without a shape, the text is smaller than that of the element, as are the level indicators and the units of the
		// game.
		const auto& referenceFont = get_text_font(size, 1.f);
		const auto reference = layout_text(referenceFont, lines, params, false);
		if (const auto capHeight = static_cast<float>(referenceFont.ascent() - reference.CapTop); capHeight > 0)
			textScale = ShapelessCapHeight * scale / capHeight;
	}
	if (hasArea) {
		const auto areaWidth = (shape.AreaX2 - shape.AreaX1) * scale;
		const auto areaHeight = (shape.AreaY2 - shape.AreaY1) * scale;
		const auto reference = layout_text(get_text_font(size, 1.f), lines, params, true);
		const auto referenceHeight = static_cast<float>(reference.LastBaseline - reference.CapTop);
		if (!params.TextSize && referenceHeight > 0)
			textScale = areaHeight / referenceHeight;

		const auto width = static_cast<float>(reference.InkX2 - reference.InkX1) * textScale;
		if (width > areaWidth && width > 0) {
			switch (params.FitMode) {
				case glyph_merge_fit_mode::CondenseThenShrink:
					condense = (std::max)(0.7f, areaWidth / width);
					if (width * condense > areaWidth)
						textScale *= areaWidth / (width * condense);
					break;
				case glyph_merge_fit_mode::Shrink:
					textScale *= areaWidth / width;
					break;
				case glyph_merge_fit_mode::Overflow:
					break;
			}
		}
	}

	const auto& textFont = get_text_font(size * textScale, condense);
	const auto layout = layout_text(textFont, lines, params, hasArea);

	// Position of the top left corner of the text block, relative to the origin of the glyph; shaped glyphs may be drawn
	// between pixels horizontally.
	float textX;
	int textY;
	auto advance = static_cast<int>(std::lround(shape.Advance * scale));
	if (mapping.Shape == glyph_merge_shape::Glyph) {
		if (glyph_metrics gm; m_info->BaseFont->try_get_glyph_metrics(codepoint, gm))
			advance = gm.AdvanceX;
	}
	if (hasArea) {
		const auto centerX = (shape.AreaX1 + shape.AreaX2) / 2 * scale + params.TextOffsetX;
		const auto centerY = baselineY - (ShapeBaselineY - (shape.AreaY1 + shape.AreaY2) / 2) * scale + params.TextOffsetY;
		textX = centerX - (layout.InkX1 + layout.InkX2) / 2;

		// Letters and digits are centered by the height of capitals, so that they line up across glyphs; symbols such as
		// + sit around the middle of lowercase letters instead, and are centered by themselves.
		if (is_symbols_only(text) && layout.InkY1 < layout.InkY2)
			textY = static_cast<int>(std::lround(centerY - static_cast<float>(layout.InkY1 + layout.InkY2) / 2));
		else
			textY = static_cast<int>(std::lround(centerY - static_cast<float>(layout.CapTop + layout.LastBaseline) / 2));
	} else {
		// Without a shape, the text sits on the baseline of the element.
		textX = params.TextOffsetX;
		textY = static_cast<int>(std::lround(baselineY - static_cast<float>(textFont.ascent()) + params.TextOffsetY));
		if (!hasShape)
			advance = layout.AdvanceWidth;
	}

	// Bounds of the glyph: the shape, plus the text where it can be visible.
	auto x1 = (std::numeric_limits<int>::max)(), y1 = (std::numeric_limits<int>::max)();
	auto x2 = (std::numeric_limits<int>::min)(), y2 = (std::numeric_limits<int>::min)();
	if (rasterShape) {
		x1 = rasterShape->Box.X1, y1 = rasterShape->Box.Y1;
		x2 = rasterShape->Box.X2, y2 = rasterShape->Box.Y2;
	} else if (hasShape) {
		float fx1, fy1, fx2, fy2;
		get_path_bounds(shape.Path, scale, baselineY, fx1, fy1, fx2, fy2);
		x1 = static_cast<int>(std::floor(fx1)), y1 = static_cast<int>(std::floor(fy1));
		x2 = static_cast<int>(std::ceil(fx2)), y2 = static_cast<int>(std::ceil(fy2));
	}
	const auto textVisibleOutside = !hasShape || shape.DrawsText || mapping.TextMode == glyph_merge_text_mode::Difference;
	if (textVisibleOutside && layout.InkX1 < layout.InkX2) {
		// Partially covered pixels may extend past the outlines; empty columns are trimmed later.
		x1 = (std::min)(x1, static_cast<int>(std::floor(textX + layout.InkX1)) - 1), y1 = (std::min)(y1, textY + layout.InkY1);
		x2 = (std::max)(x2, static_cast<int>(std::ceil(textX + layout.InkX2)) + 1), y2 = (std::max)(y2, textY + layout.InkY2);
	}
	if (x1 >= x2 || y1 >= y2)
		return {.Metrics = {.AdvanceX = advance}};

	const auto width = x2 - x1, height = y2 - y1;
	std::vector<uint8_t> shapeCoverage(static_cast<size_t>(width) * height);
	std::vector<uint8_t> textCoverage(static_cast<size_t>(width) * height);

	if (rasterShape && rasterShape->Alpha.empty()) {
		m_info->BaseFont->draw(codepoint, shapeCoverage.data(), 1, -x1, -y1, width, height, 255, 0, 255, 0);
	} else if (rasterShape) {
		const auto& box = rasterShape->Box;
		for (auto y = box.Y1; y < box.Y2; y++)
			std::copy_n(&rasterShape->Alpha[static_cast<size_t>(y - box.Y1) * box.width()], box.width(), &shapeCoverage[static_cast<size_t>(y - y1) * width + (box.X1 - x1)]);
	} else if (hasShape) {
		const auto library = std::unique_ptr<std::remove_pointer_t<FT_Library>, decltype(&FT_Done_FreeType)>([] {
			FT_Library lib;
			if (FT_Init_FreeType(&lib))
				throw std::runtime_error("FT_Init_FreeType failed");
			return lib;
		}(), &FT_Done_FreeType);
		draw_path(library.get(), shape.Path, scale, baselineY, x1, y1, width, height, shapeCoverage);
	}
	for (const auto& g : layout.Glyphs) {
		if (g.GlyphIndex) {
			textFont.draw_glyph_index(g.GlyphIndex, textCoverage.data(), 1, textX + g.X - static_cast<float>(x1), static_cast<float>(textY - y1) + g.Y, width, height, 255, 0, 255, 0);
		} else {
			const auto x = static_cast<int>(std::lround(textX + g.X)) - x1;
			const auto y = textY + static_cast<int>(std::lround(g.Y)) - y1;
			textFont.draw(g.Codepoint, textCoverage.data(), 1, x, y, width, height, 255, 0, 255, 0);
		}
	}

	rendered_glyph res;
	res.Alpha.resize(shapeCoverage.size());
	for (size_t i = 0; i < res.Alpha.size(); i++) {
		const auto a = static_cast<int>(shapeCoverage[i]), t = static_cast<int>(textCoverage[i]);
		int v;
		if (!hasShape || shape.DrawsText)
			v = a + t - a * t / 255;
		else if (mapping.TextMode == glyph_merge_text_mode::Difference)
			v = a + t - 2 * a * t / 255;
		else
			v = a * (255 - t) / 255;
		res.Alpha[i] = static_cast<uint8_t>(std::clamp(v, 0, 255));
	}

	// Trim the empty rows and columns.
	auto tx1 = width, ty1 = height, tx2 = 0, ty2 = 0;
	for (auto y = 0; y < height; y++) {
		for (auto x = 0; x < width; x++) {
			if (res.Alpha[static_cast<size_t>(y) * width + x]) {
				tx1 = (std::min)(tx1, x), ty1 = (std::min)(ty1, y);
				tx2 = (std::max)(tx2, x + 1), ty2 = (std::max)(ty2, y + 1);
			}
		}
	}
	if (tx1 >= tx2 || ty1 >= ty2)
		return {.Metrics = {.AdvanceX = advance}};

	std::vector<uint8_t> trimmed(static_cast<size_t>(tx2 - tx1) * (ty2 - ty1));
	for (auto y = ty1; y < ty2; y++)
		std::copy_n(&res.Alpha[static_cast<size_t>(y) * width + tx1], tx2 - tx1, &trimmed[static_cast<size_t>(y - ty1) * (tx2 - tx1)]);
	res.Alpha = std::move(trimmed);
	res.Metrics = {
		.X1 = x1 + tx1,
		.Y1 = y1 + ty1,
		.X2 = x1 + tx2,
		.Y2 = y1 + ty2,
		.AdvanceX = advance,
	};
	return res;
}
