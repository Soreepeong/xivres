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
#include <mutex>

#include "fixed_size_font.h"

#include "xivres/util.bitmap_copy.h"
#include "util.truetype.h"

namespace xivres::fontgen {
	class freetype_fixed_size_font : public default_abstract_fixed_size_font {
		using library_ptr_t = std::unique_ptr<std::remove_pointer_t<FT_Library>, decltype(FT_Done_FreeType)*>;

	public:
		struct create_struct {
			int LoadFlags = FT_LOAD_DEFAULT;
			FT_Render_Mode RenderMode = FT_RENDER_MODE_LIGHT;

			std::wstring get_load_flags_string() const;

			std::wstring get_render_mode_string() const;
		};

	private:
		class freetype_face_wrapper {
			struct info_t {
				std::vector<uint8_t> Data;
				std::set<char32_t> Characters;
				std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
				std::map<char32_t, glyph_metrics> AllGlyphMetrics;
				std::vector<uint8_t> GammaTable;
				FT_Matrix Matrix;
				create_struct Params{};
				int FaceIndex = 0;
				float Size = 0.f;
			};

			library_ptr_t m_library;
			std::shared_ptr<const info_t> m_info;
			FT_Face m_face{};

		public:
			freetype_face_wrapper();
			freetype_face_wrapper(std::vector<uint8_t> data, int faceIndex, float size, float gamma, const font_render_transformation_matrix& matrix, create_struct createStruct);
			freetype_face_wrapper(freetype_face_wrapper&& r) noexcept;
			freetype_face_wrapper(const freetype_face_wrapper& r);
			freetype_face_wrapper& operator=(freetype_face_wrapper&& r) noexcept;
			freetype_face_wrapper& operator=(const freetype_face_wrapper& r);
			freetype_face_wrapper& operator=(const std::nullptr_t&);
			~freetype_face_wrapper();

			FT_Face operator*() const;

			FT_Face operator->() const;

			int get_char_index(char32_t codepoint) const;

			std::unique_ptr<std::remove_pointer_t<FT_Glyph>, decltype(FT_Done_Glyph)*> load_glyph(int glyphIndex, bool render) const;

			FT_Library library() const;

			float font_size() const;

			std::span<const uint8_t> gamma_table() const;

			const std::set<char32_t>& all_characters() const;

			const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const;

		private:
			static FT_Face create_face(FT_Library library, const info_t& info);
		};

		freetype_face_wrapper m_face;

	public:
		freetype_fixed_size_font();
		freetype_fixed_size_font(const std::filesystem::path& path, int faceIndex, float size, float gamma, const font_render_transformation_matrix& matrix, create_struct createStruct);
		freetype_fixed_size_font(stream& strm, int faceIndex, float fSize, float gamma, const font_render_transformation_matrix& matrix, create_struct createStruct);
		freetype_fixed_size_font(std::vector<uint8_t> data, int faceIndex, float fSize, float gamma, const font_render_transformation_matrix& matrix, create_struct createStruct);
		freetype_fixed_size_font(freetype_fixed_size_font&& r) noexcept;
		freetype_fixed_size_font(const freetype_fixed_size_font& r);
		freetype_fixed_size_font& operator=(freetype_fixed_size_font&& r) noexcept;
		freetype_fixed_size_font& operator=(const freetype_fixed_size_font& r);

		std::string family_name() const override;

		std::string subfamily_name() const override;

		float font_size() const override;

		int ascent() const override;

		int line_height() const override;

		const std::set<char32_t>& all_codepoints() const override;

		bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const override;

		const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const override;

		int get_adjusted_advance_width(char32_t left, char32_t right) const override;

		bool draw(char32_t codepoint, util::RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const override;

		bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override;

		std::shared_ptr<fixed_size_font> get_threadsafe_view() const override;

		const fixed_size_font* get_base_font(char32_t codepoint) const override;

	private:
		glyph_metrics freetype_glyph_to_metrics(FT_Glyph glyph, int x = 0, int y = 0) const;
	};
}

#endif
