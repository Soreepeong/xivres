#ifndef _XIVRES_FONTGENERATOR_DIRECTWRITEFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_DIRECTWRITEFIXEDSIZEFONT_H_

#include <filesystem>
#include <numeric>

#include "IFixedSizeFont.h"

#include "xivres/util.bitmap_copy.h"
#include "util.truetype.h"

#include <comdef.h>
#include <dwrite_3.h>
#pragma comment(lib, "dwrite.lib")

_COM_SMARTPTR_TYPEDEF(IDWriteFactory, __uuidof(IDWriteFactory));
_COM_SMARTPTR_TYPEDEF(IDWriteFactory3, __uuidof(IDWriteFactory3));
_COM_SMARTPTR_TYPEDEF(IDWriteFont, __uuidof(IDWriteFont));
_COM_SMARTPTR_TYPEDEF(IDWriteFontCollection, __uuidof(IDWriteFontCollection));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace, __uuidof(IDWriteFontFace));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace1, __uuidof(IDWriteFontFace1));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace3, __uuidof(IDWriteFontFace3));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFaceReference, __uuidof(IDWriteFontFaceReference));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFamily, __uuidof(IDWriteFontFamily));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFile, __uuidof(IDWriteFontFile));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFileLoader, __uuidof(IDWriteFontFileLoader));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFileStream, __uuidof(IDWriteFontFileStream));
_COM_SMARTPTR_TYPEDEF(IDWriteFontSetBuilder, __uuidof(IDWriteFontSetBuilder));
_COM_SMARTPTR_TYPEDEF(IDWriteGdiInterop, __uuidof(IDWriteGdiInterop));
_COM_SMARTPTR_TYPEDEF(IDWriteGlyphRunAnalysis, __uuidof(IDWriteGlyphRunAnalysis));
_COM_SMARTPTR_TYPEDEF(IDWriteLocalizedStrings, __uuidof(IDWriteLocalizedStrings));

namespace xivres::fontgen {
	class DirectWriteFixedSizeFont : public DefaultAbstractFixedSizeFont {
	public:
		struct CreateStruct {
			DWRITE_RENDERING_MODE RenderMode = DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL;
			DWRITE_MEASURING_MODE MeasureMode = DWRITE_MEASURING_MODE_GDI_CLASSIC;
			DWRITE_GRID_FIT_MODE GridFitMode = DWRITE_GRID_FIT_MODE_ENABLED;

			const wchar_t* GetMeasuringModeString() const;

			const wchar_t* GetRenderModeString() const;

			const wchar_t* GetGridFitModeString() const;
		};

	private:
		struct ParsedInfoStruct {
			IDWriteFactoryPtr Factory;
			IDWriteFontPtr Font;
			std::shared_ptr<stream> Stream;
			std::set<char32_t> Characters;
			std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
			std::vector<uint8_t> GammaTable;
			DWRITE_FONT_METRICS1 Metrics;
			DWRITE_MATRIX Matrix;
			CreateStruct Params;
			int FontIndex = 0;
			float Size = 0.f;

			template<decltype(std::roundf) TIntCastFn = std::roundf, typename T>
			int ScaleFromFontUnit(T fontUnitValue) const {
				return static_cast<int>(TIntCastFn(static_cast<float>(fontUnitValue) * Size / static_cast<float>(Metrics.designUnitsPerEm)));
			}
		};

		struct DWriteInterfaceStruct {
			IDWriteFactoryPtr Factory;
			IDWriteFactory3Ptr Factory3;
			IDWriteFontCollectionPtr Collection;
			IDWriteFontFamilyPtr Family;
			IDWriteFontPtr Font;
			IDWriteFontFacePtr Face;
			IDWriteFontFace1Ptr Face1;
		};

		DWriteInterfaceStruct m_dwrite;
		std::shared_ptr<const ParsedInfoStruct> m_info;
		mutable std::vector<uint8_t> m_drawBuffer;

	public:
		DirectWriteFixedSizeFont();
		DirectWriteFixedSizeFont(std::filesystem::path path, int fontIndex, float size, float gamma, const FontRenderTransformationMatrix& matrix, CreateStruct params);
		DirectWriteFixedSizeFont(IDWriteFactoryPtr factory, IDWriteFontPtr font, float size, float gamma, const FontRenderTransformationMatrix& matrix, CreateStruct params);
		DirectWriteFixedSizeFont(std::shared_ptr<stream> strm, int fontIndex, float size, float gamma, const FontRenderTransformationMatrix& matrix, CreateStruct params);
		DirectWriteFixedSizeFont(DirectWriteFixedSizeFont&&) noexcept;
		DirectWriteFixedSizeFont& operator=(DirectWriteFixedSizeFont&&) noexcept;
		DirectWriteFixedSizeFont(const DirectWriteFixedSizeFont& r);
		DirectWriteFixedSizeFont& operator=(const DirectWriteFixedSizeFont& r);

		std::string GetFamilyName() const override;

		std::string GetSubfamilyName() const override;

		float GetSize() const override;

		int GetAscent() const override;

		int GetLineHeight() const override;

		const std::set<char32_t>& GetAllCodepoints() const override;

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override;

		const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const override;

		bool Draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const override;

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override;

		const IFixedSizeFont* GetBaseFont(char32_t codepoint) const override;

	private:
		static DWriteInterfaceStruct FaceFromInfoStruct(const ParsedInfoStruct& info);

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm, IDWriteGlyphRunAnalysisPtr& analysis) const;
	};
}

#endif
