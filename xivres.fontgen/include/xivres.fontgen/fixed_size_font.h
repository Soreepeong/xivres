#ifndef XIVRES_FONTGENERATOR_IFIXEDSIZEFONT_H_
#define XIVRES_FONTGENERATOR_IFIXEDSIZEFONT_H_

#include <set>
#include <vector>

#include "xivres/fontdata.h"
#include "xivres/texture.mipmap_stream.h"
#include "xivres/util.pixel_formats.h"

namespace xivres::fontgen {
	struct glyph_metrics {
		using MetricType = int;

		MetricType X1 = 0;
		MetricType Y1 = 0;
		MetricType X2 = 0;
		MetricType Y2 = 0;
		MetricType AdvanceX = 0;

		void adjust_to_intersection(glyph_metrics& r, MetricType srcWidth, MetricType srcHeight, MetricType destWidth, MetricType destHeight);

		void clear() { X1 = Y1 = X2 = Y2 = AdvanceX = 0; }

		[[nodiscard]] MetricType width() const { return X2 - X1; }

		[[nodiscard]] MetricType height() const { return Y2 - Y1; }

		[[nodiscard]] MetricType area() const { return width() * height(); }

		[[nodiscard]] bool is_effectively_empty() const { return X1 == X2 || Y1 == Y2; }

#ifdef _WINDOWS_
		glyph_metrics& set_from(const RECT& r) {
			X1 = r.left;
			Y1 = r.top;
			X2 = r.right;
			Y2 = r.bottom;
			return *this;
		}

		operator RECT() const {
			return { static_cast<LONG>(X1), static_cast<LONG>(Y1), static_cast<LONG>(X2), static_cast<LONG>(Y2) };
		}

		struct as_mutable_rect_pointer_struct {
			glyph_metrics& m;
			RECT r;
			operator RECT* () {
				return &r;
			}

			as_mutable_rect_pointer_struct(glyph_metrics& m)
				: m(m)
				, r(m) {

			}
			~as_mutable_rect_pointer_struct() { m.set_from(r); }
		} as_mutable_rect_pointer() {
			return as_mutable_rect_pointer_struct(*this);
		}

		[[nodiscard]] struct as_const_rect_pointer_struct {
			RECT r;
			operator RECT* () { return &r; }
			as_const_rect_pointer_struct(const glyph_metrics& m) : r(m) {}
		} as_const_rect_pointer() const {
			return as_const_rect_pointer_struct(*this);
		}
#endif

		template<typename Mul, typename Div>
		glyph_metrics& scale(Mul mul, Div div) {
			const auto mMul = static_cast<MetricType>(mul);
			const auto mDiv = static_cast<MetricType>(div);
			X1 = X1 * mMul / mDiv;
			Y1 = Y1 * mMul / mDiv;
			X2 = X2 * mMul / mDiv;
			Y2 = Y2 * mMul / mDiv;
			AdvanceX = AdvanceX * mMul / mDiv;
			return *this;
		}

		glyph_metrics& translate(MetricType x, MetricType y);

		glyph_metrics& expand_to_fit(const glyph_metrics& r);
	};

	struct font_render_transformation_matrix {
		float M11;
		float M12;
		float M21;
		float M22;

		void SetIdentity();
	};

	class fixed_size_font {
	public:
		fixed_size_font() = default;
		fixed_size_font(fixed_size_font&&) = default;
		fixed_size_font(const fixed_size_font&) = default;
		fixed_size_font& operator=(fixed_size_font&&) = default;
		fixed_size_font& operator=(const fixed_size_font&) = default;
		virtual ~fixed_size_font() = default;

		[[nodiscard]] virtual std::string family_name() const = 0;

		[[nodiscard]] virtual std::string subfamily_name() const = 0;

		[[nodiscard]] virtual float font_size() const = 0;

		[[nodiscard]] virtual int ascent() const = 0;

		[[nodiscard]] virtual int line_height() const = 0;

		[[nodiscard]] virtual const std::set<char32_t>& all_codepoints() const = 0;

		[[nodiscard]] virtual bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const = 0;

		[[nodiscard]] virtual const void* get_base_font_glyph_uniqid(char32_t c) const = 0;

		[[nodiscard]] virtual char32_t uniqid_to_glyph(const void*) const = 0;

		[[nodiscard]] virtual const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const = 0;

		[[nodiscard]] virtual int get_adjusted_advance_width(char32_t left, char32_t right) const = 0;

		virtual bool draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const = 0;

		virtual bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const = 0;

		[[nodiscard]] virtual std::shared_ptr<fixed_size_font> get_threadsafe_view() const = 0;

		[[nodiscard]] virtual const fixed_size_font* get_base_font(char32_t codepoint) const = 0;
	};

	class default_abstract_fixed_size_font : public fixed_size_font {
	public:
		default_abstract_fixed_size_font() = default;
		default_abstract_fixed_size_font(default_abstract_fixed_size_font&&) = default;
		default_abstract_fixed_size_font(const default_abstract_fixed_size_font&) = default;
		default_abstract_fixed_size_font& operator=(default_abstract_fixed_size_font&&) = default;
		default_abstract_fixed_size_font& operator=(const default_abstract_fixed_size_font&) = default;

		[[nodiscard]] const void* get_base_font_glyph_uniqid(char32_t c) const override;

		[[nodiscard]] char32_t uniqid_to_glyph(const void* pc) const override;

		[[nodiscard]] int get_adjusted_advance_width(char32_t left, char32_t right) const override;
	};

	class empty_fixed_size_font : public fixed_size_font {
	public:
		struct create_struct {
			int Ascent = 0;
			int LineHeight = 0;
		};

	private:
		float m_size = 0.f;
		create_struct m_fontDef;

	public:
		empty_fixed_size_font(float size, create_struct fontDef);

		empty_fixed_size_font();
		empty_fixed_size_font(empty_fixed_size_font&&) noexcept;
		empty_fixed_size_font(const empty_fixed_size_font& r);
		empty_fixed_size_font& operator=(empty_fixed_size_font&&) noexcept;
		empty_fixed_size_font& operator=(const empty_fixed_size_font&);

		[[nodiscard]] std::string family_name() const override;

		[[nodiscard]] std::string subfamily_name() const override;

		[[nodiscard]] float font_size() const override;

		[[nodiscard]] int ascent() const override;

		[[nodiscard]] int line_height() const override;

		[[nodiscard]] const std::set<char32_t>& all_codepoints() const override;

		[[nodiscard]] bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const override;

		[[nodiscard]] const void* get_base_font_glyph_uniqid(char32_t c) const override;

		[[nodiscard]] char32_t uniqid_to_glyph(const void* pc) const override;

		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const override;

		[[nodiscard]] int get_adjusted_advance_width(char32_t left, char32_t right) const override;

		bool draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const override;

		bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		[[nodiscard]] std::shared_ptr<fixed_size_font> get_threadsafe_view() const override;

		[[nodiscard]] const fixed_size_font* get_base_font(char32_t codepoint) const override;
	};
}

#endif
