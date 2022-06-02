#ifndef _XIVRES_FONTGENERATOR_FREETYPEFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_FREETYPEFIXEDSIZEFONT_H_

#ifndef FT2BUILD_H_
#include <ft2build.h>
#endif

#include FT_FREETYPE_H
#include FT_BITMAP_H
#include FT_OUTLINE_H 
#include FT_GLYPH_H
#include FT_TRUETYPE_TABLES_H\

#include <filesystem>

#include "IFixedSizeFont.h"

#include "xivres/util.bitmap_copy.h"
#include "util.truetype.h"

namespace xivres::fontgen {
	class FreeTypeFixedSizeFont : public DefaultAbstractFixedSizeFont {
		using LibraryPtr = std::unique_ptr<std::remove_pointer_t<FT_Library>, decltype(FT_Done_FreeType)*>;

	public:
		struct CreateStruct {
			int LoadFlags = FT_LOAD_DEFAULT;
			FT_Render_Mode RenderMode = FT_RENDER_MODE_LIGHT;

			std::wstring GetLoadFlagsString() const;

			std::wstring GetRenderModeString() const;
		};

	private:
		class FreeTypeFaceWrapper {
			struct InfoStruct {
				std::vector<uint8_t> Data;
				std::set<char32_t> Characters;
				std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
				std::map<char32_t, GlyphMetrics> AllGlyphMetrics;
				std::vector<uint8_t> GammaTable;
				FT_Matrix Matrix;
				CreateStruct Params{};
				int FaceIndex = 0;
				float Size = 0.f;
			};

			LibraryPtr m_library;
			std::shared_ptr<const InfoStruct> m_info;
			FT_Face m_face{};

		public:
			FreeTypeFaceWrapper();
			FreeTypeFaceWrapper(std::vector<uint8_t> data, int faceIndex, float size, float gamma, const FontRenderTransformationMatrix& matrix, CreateStruct createStruct);
			FreeTypeFaceWrapper(FreeTypeFaceWrapper&& r) noexcept;
			FreeTypeFaceWrapper(const FreeTypeFaceWrapper& r);
			FreeTypeFaceWrapper& operator=(FreeTypeFaceWrapper&& r) noexcept;
			FreeTypeFaceWrapper& operator=(const FreeTypeFaceWrapper& r);
			FreeTypeFaceWrapper& operator=(const std::nullptr_t&);
			~FreeTypeFaceWrapper();

			FT_Face operator*() const;

			FT_Face operator->() const;

			int GetCharIndex(char32_t codepoint) const;

			std::unique_ptr<std::remove_pointer_t<FT_Glyph>, decltype(FT_Done_Glyph)*> LoadGlyph(int glyphIndex, bool render) const;

			FT_Library GetLibrary() const;

			float GetSize() const;

			std::span<const uint8_t> GetGammaTable() const;

			const std::set<char32_t>& GetAllCharacters() const;

			const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const;

			int GetLoadFlags() const;

			std::span<const uint8_t> GetRawData() const;

		private:
			static FT_Face CreateFace(FT_Library library, const InfoStruct& info);
		};

		FreeTypeFaceWrapper m_face;

	public:
		FreeTypeFixedSizeFont();
		FreeTypeFixedSizeFont(const std::filesystem::path& path, int faceIndex, float size, float gamma, const FontRenderTransformationMatrix& matrix, CreateStruct createStruct);
		FreeTypeFixedSizeFont(stream& strm, int faceIndex, float fSize, float gamma, const FontRenderTransformationMatrix& matrix, CreateStruct createStruct);
		FreeTypeFixedSizeFont(std::vector<uint8_t> data, int faceIndex, float fSize, float gamma, const FontRenderTransformationMatrix& matrix, CreateStruct createStruct);
		FreeTypeFixedSizeFont(FreeTypeFixedSizeFont&& r) noexcept;
		FreeTypeFixedSizeFont(const FreeTypeFixedSizeFont& r);
		FreeTypeFixedSizeFont& operator=(FreeTypeFixedSizeFont&& r) noexcept;
		FreeTypeFixedSizeFont& operator=(const FreeTypeFixedSizeFont& r);

		std::string GetFamilyName() const override;

		std::string GetSubfamilyName() const override;

		float GetSize() const override;

		int GetAscent() const override;

		int GetLineHeight() const override;

		const std::set<char32_t>& GetAllCodepoints() const override;

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override;

		const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const override;

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override;

		bool Draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const override;

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override;

		const IFixedSizeFont* GetBaseFont(char32_t codepoint) const override;

	private:
		GlyphMetrics FreeTypeGlyphToMetrics(FT_Glyph glyph, int x = 0, int y = 0) const;
	};
}

#endif
