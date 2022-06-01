#ifndef _XIVRES_FONTGENERATOR_IFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_IFIXEDSIZEFONT_H_

#include <array>
#include <set>
#include <vector>

#include "xivres/Fontdata.h"
#include "xivres/MipmapStream.h"
#include "xivres/PixelFormats.h"
#include "xivres/util.unicode.h"

namespace xivres::fontgen {
	struct GlyphMetrics {
		using MetricType = int;

		MetricType X1 = 0;
		MetricType Y1 = 0;
		MetricType X2 = 0;
		MetricType Y2 = 0;
		MetricType AdvanceX = 0;

		void AdjustToIntersection(GlyphMetrics& r, MetricType srcWidth, MetricType srcHeight, MetricType destWidth, MetricType destHeight);

		void Clear() { X1 = Y1 = X2 = Y2 = AdvanceX = 0; }

		[[nodiscard]] MetricType GetWidth() const { return X2 - X1; }

		[[nodiscard]] MetricType GetHeight() const { return Y2 - Y1; }

		[[nodiscard]] MetricType GetArea() const { return GetWidth() * GetHeight(); }

		[[nodiscard]] bool IsEffectivelyEmpty() const { return X1 == X2 || Y1 == Y2; }

#ifdef _WINDOWS_
		GlyphMetrics& SetFrom(const RECT& r) {
			X1 = r.left;
			Y1 = r.top;
			X2 = r.right;
			Y2 = r.bottom;
			return *this;
		}

		operator RECT() const {
			return { static_cast<LONG>(X1), static_cast<LONG>(Y1), static_cast<LONG>(X2), static_cast<LONG>(Y2) };
		}

		struct AsMutableRectPtrType {
			GlyphMetrics& m;
			RECT r;
			operator RECT* () {
				return &r;
			}

			AsMutableRectPtrType(GlyphMetrics& m)
				: m(m)
				, r(m) {

			}
			~AsMutableRectPtrType() { m.SetFrom(r); }
		} AsMutableRectPtr() {
			return AsMutableRectPtrType(*this);
		}

		[[nodiscard]] struct AsConstRectPtrType {
			RECT r;
			operator RECT* () { return &r; }
			AsConstRectPtrType(const GlyphMetrics& m) : r(m) {}
		} AsConstRectPtr() const {
			return AsConstRectPtrType(*this);
		}
#endif

		template<typename Mul, typename Div>
		GlyphMetrics& Scale(Mul mul, Div div) {
			const auto mMul = static_cast<MetricType>(mul);
			const auto mDiv = static_cast<MetricType>(div);
			X1 = X1 * mMul / mDiv;
			Y1 = Y1 * mMul / mDiv;
			X2 = X2 * mMul / mDiv;
			Y2 = Y2 * mMul / mDiv;
			AdvanceX = AdvanceX * mMul / mDiv;
			return *this;
		}

		GlyphMetrics& Translate(MetricType x, MetricType y);

		GlyphMetrics& ExpandToFit(const GlyphMetrics& r);
	};

	struct FontRenderTransformationMatrix {
		float M11;
		float M12;
		float M21;
		float M22;
	};

	class IFixedSizeFont {
	public:
		virtual std::string GetFamilyName() const = 0;

		virtual std::string GetSubfamilyName() const = 0;

		virtual float GetSize() const = 0;

		virtual int GetAscent() const = 0;

		virtual int GetLineHeight() const = 0;

		virtual const std::set<char32_t>& GetAllCodepoints() const = 0;

		virtual bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const = 0;

		virtual const void* GetBaseFontGlyphUniqid(char32_t c) const = 0;

		virtual char32_t UniqidToGlyph(const void*) const = 0;

		virtual const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const = 0;

		virtual int GetAdjustedAdvanceX(char32_t left, char32_t right) const = 0;

		virtual bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const = 0;

		virtual bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const = 0;

		virtual std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const = 0;

		virtual const IFixedSizeFont* GetBaseFont(char32_t codepoint) const = 0;
	};

	class DefaultAbstractFixedSizeFont : public IFixedSizeFont {
	public:
		const void* GetBaseFontGlyphUniqid(char32_t c) const override;

		char32_t UniqidToGlyph(const void* pc) const override;

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override;
	};

	class EmptyFixedSizeFont : public IFixedSizeFont {
	public:
		struct CreateStruct {
			int Ascent = 0;
			int LineHeight = 0;
		};

	private:
		float m_size = 0.f;
		CreateStruct m_fontDef;

	public:
		EmptyFixedSizeFont(float size, CreateStruct fontDef);

		EmptyFixedSizeFont();
		EmptyFixedSizeFont(EmptyFixedSizeFont&&) noexcept;
		EmptyFixedSizeFont(const EmptyFixedSizeFont& r);
		EmptyFixedSizeFont& operator=(EmptyFixedSizeFont&&) noexcept;
		EmptyFixedSizeFont& operator=(const EmptyFixedSizeFont&);

		std::string GetFamilyName() const override;

		std::string GetSubfamilyName() const override;

		float GetSize() const override;

		int GetAscent() const override;

		int GetLineHeight() const override;

		const std::set<char32_t>& GetAllCodepoints() const override;

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override;

		const void* GetBaseFontGlyphUniqid(char32_t c) const override;

		char32_t UniqidToGlyph(const void* pc) const override;

		const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const override;

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override;

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const override;

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override;

		const IFixedSizeFont* GetBaseFont(char32_t codepoint) const override;
	};
}

#endif
