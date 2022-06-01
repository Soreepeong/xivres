#include "../include/xivres.fontgen/FreeTypeFixedSizeFont.h"

static FT_Error SuccessOrThrow(FT_Error error, std::initializer_list<FT_Error> acceptables = {}) {
	if (!error)
		return error;

	for (const auto& er : acceptables) {
		if (er == error)
			return error;
	}

	throw std::runtime_error(std::format("FreeType Error: 0x{:x}", error));
}

template<typename T>
static T FirstArgIsReturn(FT_Error(*pfn)(T*)) {
	T p{};
	SuccessOrThrow(pfn(&p));
	return p;
}

class FreeTypeFontTable {
	std::vector<uint8_t> m_buf;

public:
	FreeTypeFontTable(FT_Face face, uint32_t tag);

	operator bool() const;

	template<typename T = uint8_t>
	std::span<const T> GetSpan() const {
		if (m_buf.empty())
			return {};

		return { reinterpret_cast<const T*>(&m_buf[0]), m_buf.size() / sizeof T };
	}
};

class FreeTypeBitmapWrapper {
	const FT_Library m_library;
	FT_Bitmap m_bitmap;

public:
	FreeTypeBitmapWrapper(FT_Library library);
	FreeTypeBitmapWrapper(FreeTypeBitmapWrapper&&) = delete;
	FreeTypeBitmapWrapper(const FreeTypeBitmapWrapper&) = delete;
	FreeTypeBitmapWrapper& operator=(FreeTypeBitmapWrapper&&) = delete;
	FreeTypeBitmapWrapper& operator=(const FreeTypeBitmapWrapper&) = delete;

	~FreeTypeBitmapWrapper();

	FT_Bitmap& operator*();

	FT_Bitmap* operator->();

	void ConvertFrom(const FT_Bitmap& source, int alignment);

	std::span<uint8_t> GetBuffer() const;
};

xivres::fontgen::GlyphMetrics xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeGlyphToMetrics(FT_Glyph glyph, int x, int y) const {
	FT_BBox cbox;
	FT_Glyph_Get_CBox(glyph, FT_GLYPH_BBOX_PIXELS, &cbox);
	GlyphMetrics src{
		.X1 = x + cbox.xMin,
		.Y1 = y + GetAscent() - cbox.yMax,
		.X2 = src.X1 + static_cast<int>(cbox.xMax - cbox.xMin),
		.Y2 = src.Y1 + static_cast<int>(cbox.yMax - cbox.yMin),
		.AdvanceX = (glyph->advance.x + 1024 * 64 - 1) / (1024 * 64),
	};
	return src;
}

const xivres::fontgen::IFixedSizeFont* xivres::fontgen::FreeTypeFixedSizeFont::GetBaseFont(char32_t codepoint) const {
	return this;
}

std::shared_ptr<xivres::fontgen::IFixedSizeFont> xivres::fontgen::FreeTypeFixedSizeFont::GetThreadSafeView() const {
	return std::make_shared<FreeTypeFixedSizeFont>(*this);
}

bool xivres::fontgen::FreeTypeFixedSizeFont::Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	const auto glyphIndex = m_face.GetCharIndex(codepoint);
	if (!glyphIndex)
		return false;

	auto glyph = m_face.LoadGlyph(glyphIndex, true);
	auto bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>(glyph.get());
	auto dest = FreeTypeGlyphToMetrics(glyph.get(), drawX, drawY);
	auto src = dest;
	src.Translate(-src.X1, -src.Y1);
	src.AdjustToIntersection(dest, src.GetWidth(), src.GetHeight(), destWidth, destHeight);
	if (src.IsEffectivelyEmpty() || dest.IsEffectivelyEmpty())
		return true;

	FreeTypeBitmapWrapper bitmapWrapper(m_face.GetLibrary());
	bitmapWrapper.ConvertFrom(bitmapGlyph->bitmap, 1);

	util::bitmap_copy::to_l8()
		.from(bitmapWrapper->buffer, bitmapWrapper->pitch, bitmapWrapper->rows, 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, stride, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.fore_opacity(fgOpacity)
		.back_color(bgColor)
		.back_opacity(bgOpacity)
		.gamma_table(m_face.GetGammaTable())
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

bool xivres::fontgen::FreeTypeFixedSizeFont::Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const {
	const auto glyphIndex = m_face.GetCharIndex(codepoint);
	if (!glyphIndex)
		return false;

	auto glyph = m_face.LoadGlyph(glyphIndex, true);
	auto bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>(glyph.get());
	auto dest = FreeTypeGlyphToMetrics(glyph.get(), drawX, drawY);
	auto src = dest;
	src.Translate(-src.X1, -src.Y1);
	src.AdjustToIntersection(dest, src.GetWidth(), src.GetHeight(), destWidth, destHeight);
	if (src.IsEffectivelyEmpty() || dest.IsEffectivelyEmpty())
		return true;

	FreeTypeBitmapWrapper bitmapWrapper(m_face.GetLibrary());
	bitmapWrapper.ConvertFrom(bitmapGlyph->bitmap, 1);

	util::bitmap_copy::to_rgba8888()
		.from(bitmapWrapper->buffer, bitmapWrapper->pitch, bitmapWrapper->rows, 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.back_color(bgColor)
		.gamma_table(m_face.GetGammaTable())
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

int xivres::fontgen::FreeTypeFixedSizeFont::GetAdjustedAdvanceX(char32_t left, char32_t right) const {
	GlyphMetrics gm;
	if (!GetGlyphMetrics(left, gm))
		return 0;

	if (const auto it = m_face.GetAllKerningPairs().find(std::make_pair(left, right)); it != m_face.GetAllKerningPairs().end())
		return gm.AdvanceX + it->second;

	return gm.AdvanceX;
}

const std::map<std::pair<char32_t, char32_t>, int>& xivres::fontgen::FreeTypeFixedSizeFont::GetAllKerningPairs() const {
	return m_face.GetAllKerningPairs();
}

bool xivres::fontgen::FreeTypeFixedSizeFont::GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const {
	const auto glyphIndex = m_face.GetCharIndex(codepoint);
	if (!glyphIndex)
		return false;

	gm = FreeTypeGlyphToMetrics(m_face.LoadGlyph(glyphIndex, false).get());
	return true;
}

const std::set<char32_t>& xivres::fontgen::FreeTypeFixedSizeFont::GetAllCodepoints() const {
	return m_face.GetAllCharacters();
}

int xivres::fontgen::FreeTypeFixedSizeFont::GetLineHeight() const {
	return (m_face->size->metrics.height + 63) / 64;
}

int xivres::fontgen::FreeTypeFixedSizeFont::GetAscent() const {
	return (m_face->size->metrics.ascender + 63) / 64;
}

float xivres::fontgen::FreeTypeFixedSizeFont::GetSize() const {
	return m_face.GetSize();
}

std::string xivres::fontgen::FreeTypeFixedSizeFont::GetSubfamilyName() const {
	FreeTypeFontTable nameDataRef(*m_face, util::TrueType::Name::DirectoryTableTag.ReverseNativeValue);
	util::TrueType::Name::View name(nameDataRef.GetSpan<char>());
	if (!name)
		return {};

	return name.GetPreferredSubfamilyName<std::string>(0);
}

std::string xivres::fontgen::FreeTypeFixedSizeFont::GetFamilyName() const {
	FreeTypeFontTable nameDataRef(*m_face, util::TrueType::Name::DirectoryTableTag.ReverseNativeValue);
	util::TrueType::Name::View name(nameDataRef.GetSpan<char>());
	if (!name)
		return {};

	return name.GetPreferredFamilyName<std::string>(0);
}

xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFixedSizeFont() = default;

xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFixedSizeFont(std::vector<uint8_t> data, int faceIndex, float fSize, float gamma, const FontRenderTransformationMatrix & matrix, CreateStruct createStruct)
	: m_face(std::move(data), faceIndex, fSize, gamma, matrix, createStruct) {

}

xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFixedSizeFont(stream & strm, int faceIndex, float fSize, float gamma, const FontRenderTransformationMatrix & matrix, CreateStruct createStruct)
	: m_face(strm.read_vector<uint8_t>(), faceIndex, fSize, gamma, matrix, createStruct) {

}

xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFixedSizeFont(const std::filesystem::path & path, int faceIndex, float size, float gamma, const FontRenderTransformationMatrix & matrix, CreateStruct createStruct)
	: FreeTypeFixedSizeFont(file_stream(path).read_vector<uint8_t>(), faceIndex, size, gamma, matrix, createStruct) {

}

xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFixedSizeFont(FreeTypeFixedSizeFont && r)  noexcept = default;

xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFixedSizeFont(const FreeTypeFixedSizeFont & r) = default;

xivres::fontgen::FreeTypeFixedSizeFont& xivres::fontgen::FreeTypeFixedSizeFont::operator=(FreeTypeFixedSizeFont && r) noexcept = default;

xivres::fontgen::FreeTypeFixedSizeFont& xivres::fontgen::FreeTypeFixedSizeFont::operator=(const FreeTypeFixedSizeFont & r) = default;

std::span<uint8_t> FreeTypeBitmapWrapper::GetBuffer() const {
	return { m_bitmap.buffer, m_bitmap.rows * m_bitmap.pitch };
}

void FreeTypeBitmapWrapper::ConvertFrom(const FT_Bitmap & source, int alignment) {
	SuccessOrThrow(FT_Bitmap_Convert(m_library, &source, &m_bitmap, alignment));
	switch (m_bitmap.num_grays) {
		case 2:
			for (auto& b : GetBuffer())
				b = b ? 255 : 0;
			break;

		case 4:
			for (auto& b : GetBuffer())
				b = b * 85;
			break;

		case 16:
			for (auto& b : GetBuffer())
				b = b * 17;
			break;

		case 256:
			break;

		default:
			throw std::runtime_error("Invalid num_grays");
	}
}

FT_Bitmap* FreeTypeBitmapWrapper::operator->() {
	return &m_bitmap;
}

FT_Bitmap& FreeTypeBitmapWrapper::operator*() {
	return m_bitmap;
}

FreeTypeBitmapWrapper::~FreeTypeBitmapWrapper() {
	SuccessOrThrow(FT_Bitmap_Done(m_library, &m_bitmap));
}

FreeTypeBitmapWrapper::FreeTypeBitmapWrapper(FT_Library library)
	: m_library(library) {
	FT_Bitmap_Init(&m_bitmap);
}

std::unique_ptr<std::remove_pointer_t<FT_Glyph>, decltype(FT_Done_Glyph)*> xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::LoadGlyph(int glyphIndex, bool render) const {
	if (m_face->glyph->glyph_index != glyphIndex)
		SuccessOrThrow(FT_Load_Glyph(m_face, glyphIndex, m_info->Params.LoadFlags));

	FT_Glyph glyph;
	SuccessOrThrow(FT_Get_Glyph(m_face->glyph, &glyph));
	auto uniqueGlyphPtr = std::unique_ptr<std::remove_pointer_t<FT_Glyph>, decltype(FT_Done_Glyph)*>(glyph, FT_Done_Glyph);

	FT_Vector zeroDelta{};
	FT_Glyph_Transform(glyph, &m_info->Matrix, &zeroDelta); // failing this is acceptable

	if (render) {
		SuccessOrThrow(FT_Glyph_To_Bitmap(&glyph, m_info->Params.RenderMode, nullptr, false));
		uniqueGlyphPtr.release();
		uniqueGlyphPtr = { glyph, FT_Done_Glyph };
	}

	return std::move(uniqueGlyphPtr);
}

FT_Face xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::CreateFace(FT_Library library, const InfoStruct & info) {
	FT_Face face;
	SuccessOrThrow(FT_New_Memory_Face(library, &info.Data[0], static_cast<FT_Long>(info.Data.size()), info.FaceIndex, &face));
	try {
		SuccessOrThrow(FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(64.f * info.Size), 72, 72));
		return face;
	} catch (...) {
		SuccessOrThrow(FT_Done_Face(face));
		throw;
	}
}

std::span<const uint8_t> xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::GetRawData() const {
	return m_info->Data;
}

int xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::GetLoadFlags() const {
	return m_info->Params.LoadFlags;
}

const std::map<std::pair<char32_t, char32_t>, int>& xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::GetAllKerningPairs() const {
	return m_info->KerningPairs;
}

const std::set<char32_t>& xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::GetAllCharacters() const {
	return m_info->Characters;
}

std::span<const uint8_t> xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::GetGammaTable() const {
	return std::span(m_info->GammaTable);
}

float xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::GetSize() const {
	return m_info->Size;
}

FT_Library xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::GetLibrary() const {
	return m_library.get();
}

int xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::GetCharIndex(char32_t codepoint) const {
	return FT_Get_Char_Index(m_face, codepoint);
}

FT_Face xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::operator->() const {
	return m_face;
}

FT_Face xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::operator*() const {
	return m_face;
}

xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::~FreeTypeFaceWrapper() {
	*this = nullptr;
}

xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper& xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::operator=(const std::nullptr_t&) {
	if (!m_face)
		return *this;

	SuccessOrThrow(FT_Done_Face(m_face));

	m_face = nullptr;
	m_info = nullptr;
	m_library = nullptr;

	return *this;
}

xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper& xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::operator=(const FreeTypeFaceWrapper & r) {
	if (this == &r)
		return *this;

	auto library = LibraryPtr(FirstArgIsReturn<FT_Library>(FT_Init_FreeType), &FT_Done_FreeType);
	auto face = CreateFace(library.get(), *r.m_info);

	*this = nullptr;

	m_library = std::move(library);
	m_info = r.m_info;
	m_face = std::move(face);

	return *this;
}

xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper& xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::operator=(FreeTypeFaceWrapper && r) noexcept {
	if (this == &r)
		return *this;

	*this = nullptr;

	m_library = std::move(r.m_library);
	m_info = std::move(r.m_info);
	m_face = r.m_face;
	r.m_face = nullptr;

	return *this;
}

xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::FreeTypeFaceWrapper(const FreeTypeFaceWrapper & r)
	: m_library(FirstArgIsReturn<FT_Library>(FT_Init_FreeType), &FT_Done_FreeType)
	, m_info(r.m_info) {
	if (r.m_face)
		m_face = CreateFace(m_library.get(), *m_info);
}

xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::FreeTypeFaceWrapper(FreeTypeFaceWrapper && r) noexcept
	: m_library(std::move(r.m_library))
	, m_info(std::move(r.m_info))
	, m_face(r.m_face) {
	r.m_face = nullptr;
}

xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::FreeTypeFaceWrapper(std::vector<uint8_t> data, int faceIndex, float size, float gamma, const FontRenderTransformationMatrix & matrix, CreateStruct createStruct)
	: m_library(FirstArgIsReturn<FT_Library>(FT_Init_FreeType), &FT_Done_FreeType) {
	auto info = std::make_shared<InfoStruct>();
	info->Data = std::move(data);
	info->Size = size;
	info->GammaTable = util::bitmap_copy::create_gamma_table(gamma);
	info->Params = createStruct;
	info->FaceIndex = faceIndex;
	info->Matrix.xx = static_cast<FT_Fixed>(matrix.M11 * static_cast<float>(0x10000));
	info->Matrix.xy = static_cast<FT_Fixed>(matrix.M12 * static_cast<float>(0x10000));
	info->Matrix.yx = static_cast<FT_Fixed>(matrix.M21 * static_cast<float>(0x10000));
	info->Matrix.yy = static_cast<FT_Fixed>(matrix.M22 * static_cast<float>(0x10000));
	info->Params.LoadFlags &= (0 |
		FT_LOAD_NO_HINTING |
		FT_LOAD_NO_BITMAP |
		FT_LOAD_FORCE_AUTOHINT |
		FT_LOAD_NO_AUTOHINT);

	m_face = CreateFace(m_library.get(), *info);
	FT_UInt glyphIndex;
	for (char32_t c = FT_Get_First_Char(m_face, &glyphIndex); glyphIndex; c = FT_Get_Next_Char(m_face, c, &glyphIndex))
		info->Characters.insert(c);

	FreeTypeFontTable kernDataRef(m_face, util::TrueType::Kern::DirectoryTableTag.ReverseNativeValue);
	FreeTypeFontTable gposDataRef(m_face, util::TrueType::Gpos::DirectoryTableTag.ReverseNativeValue);
	FreeTypeFontTable cmapDataRef(m_face, util::TrueType::Cmap::DirectoryTableTag.ReverseNativeValue);
	util::TrueType::Kern::View kern(kernDataRef.GetSpan<char>());
	util::TrueType::Gpos::View gpos(gposDataRef.GetSpan<char>());
	util::TrueType::Cmap::View cmap(cmapDataRef.GetSpan<char>());
	if (cmap && (kern || gpos)) {
		const auto cmapVector = cmap.GetGlyphToCharMap();

		if (kern)
			info->KerningPairs = kern.Parse(cmapVector);

		if (gpos) {
			const auto pairs = gpos.ExtractAdvanceX(cmapVector);
			// do not overwrite
			info->KerningPairs.insert(pairs.begin(), pairs.end());
		}

		for (auto it = info->KerningPairs.begin(); it != info->KerningPairs.end(); ) {
			it->second = static_cast<int>(it->second * info->Size / static_cast<float>(m_face->size->face->units_per_EM));
			if (it->second)
				++it;
			else
				it = info->KerningPairs.erase(it);
		}
	}

	m_info = std::move(info);
}

xivres::fontgen::FreeTypeFixedSizeFont::FreeTypeFaceWrapper::FreeTypeFaceWrapper()
	: m_library(FirstArgIsReturn<FT_Library>(FT_Init_FreeType), &FT_Done_FreeType) {
}

std::wstring xivres::fontgen::FreeTypeFixedSizeFont::CreateStruct::GetRenderModeString() const {
	switch (RenderMode) {
		case FT_RENDER_MODE_NORMAL: return L"Normal";
		case FT_RENDER_MODE_LIGHT: return L"Light";
		case FT_RENDER_MODE_MONO: return L"Mono";
		case FT_RENDER_MODE_LCD: return L"LCD";
		case FT_RENDER_MODE_LCD_V: return L"LCD_V";
		case FT_RENDER_MODE_SDF: return L"SDF";
		default: return L"Invalid";
	}
}

std::wstring xivres::fontgen::FreeTypeFixedSizeFont::CreateStruct::GetLoadFlagsString() const {
	std::wstring res;
	if (LoadFlags & FT_LOAD_NO_HINTING) res += L", no hinting";
	if (LoadFlags & FT_LOAD_NO_BITMAP) res += L", no bitmap";
	if (LoadFlags & FT_LOAD_FORCE_AUTOHINT) res += L", force autohint";
	if (LoadFlags & FT_LOAD_NO_AUTOHINT) res += L", no autohint";

	if (res.empty())
		return L"Default";
	return res.substr(2);
}

FreeTypeFontTable::operator bool() const {
	return !m_buf.empty();
}

FreeTypeFontTable::FreeTypeFontTable(FT_Face face, uint32_t tag) {
	FT_ULong len = 0;
	if (SuccessOrThrow(FT_Load_Sfnt_Table(face, tag, 0, nullptr, &len), { FT_Err_Table_Missing }))
		return;

	m_buf.resize(len);
	if (SuccessOrThrow(FT_Load_Sfnt_Table(face, tag, 0, &m_buf[0], &len), { FT_Err_Table_Missing }))
		return;
}