#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "../include/xivres.fontgen/DirectWriteFixedSizeFont.h"

static HRESULT SuccessOrThrow(HRESULT hr, std::initializer_list<HRESULT> acceptables = {}) {
	if (SUCCEEDED(hr))
		return hr;

	for (const auto& h : acceptables) {
		if (h == hr)
			return hr;
	}

	const auto err = _com_error(hr);
	wchar_t* pszMsg = nullptr;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		hr,
		MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
		reinterpret_cast<LPWSTR>(&pszMsg),
		0,
		NULL);
	if (pszMsg) {
		std::unique_ptr<wchar_t, decltype(LocalFree)*> pszMsgFree(pszMsg, LocalFree);

		throw std::runtime_error(std::format(
			"Error (HRESULT=0x{:08X}): {}",
			static_cast<uint32_t>(hr),
			xivres::util::unicode::convert<std::string>(std::wstring(pszMsg))
		));

	} else {
		throw std::runtime_error(std::format(
			"Error (HRESULT=0x{:08X})",
			static_cast<uint32_t>(hr),
			xivres::util::unicode::convert<std::string>(std::wstring(pszMsg))
		));
	}
}

class IStreamBasedDWriteFontFileLoader : public IDWriteFontFileLoader {
	class IStreamAsDWriteFontFileStream : public IDWriteFontFileStream {
		const std::shared_ptr<xivres::stream> m_stream;

		std::atomic_uint32_t m_nRef = 1;

		IStreamAsDWriteFontFileStream(std::shared_ptr<xivres::stream> pStream);

	public:
		static IStreamAsDWriteFontFileStream* New(std::shared_ptr<xivres::stream> pStream);

		HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override;

		ULONG __stdcall AddRef() override;

		ULONG __stdcall Release() override;

		HRESULT __stdcall ReadFileFragment(void const** pFragmentStart, uint64_t fileOffset, uint64_t fragmentSize, void** pFragmentContext) override;

		void __stdcall ReleaseFileFragment(void* fragmentContext) override;

		HRESULT __stdcall GetFileSize(uint64_t* pFileSize) override;

		HRESULT __stdcall GetLastWriteTime(uint64_t* pLastWriteTime) override;
	};

	IStreamBasedDWriteFontFileLoader() = default;
	IStreamBasedDWriteFontFileLoader(IStreamBasedDWriteFontFileLoader&&) = delete;
	IStreamBasedDWriteFontFileLoader(const IStreamBasedDWriteFontFileLoader&) = delete;
	IStreamBasedDWriteFontFileLoader& operator=(IStreamBasedDWriteFontFileLoader&&) = delete;
	IStreamBasedDWriteFontFileLoader& operator=(const IStreamBasedDWriteFontFileLoader&) = delete;

public:
	static IStreamBasedDWriteFontFileLoader& GetInstance();

	HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override;

	ULONG __stdcall AddRef() override;

	ULONG __stdcall Release() override;

	HRESULT __stdcall CreateStreamFromKey(void const* fontFileReferenceKey, uint32_t fontFileReferenceKeySize, IDWriteFontFileStream** pFontFileStream) override;
};

class IStreamBasedDWriteFontCollectionLoader : public IDWriteFontCollectionLoader {
public:
	class IStreamAsDWriteFontFileEnumerator : public IDWriteFontFileEnumerator {
		const IDWriteFactoryPtr m_factory;
		const std::shared_ptr<xivres::stream> m_stream;

		std::atomic_uint32_t m_nRef = 1;
		int m_nCurrentFile = -1;

		IStreamAsDWriteFontFileEnumerator(IDWriteFactoryPtr factoryPtr, std::shared_ptr<xivres::stream> pStream);

	public:
		static IStreamAsDWriteFontFileEnumerator* New(IDWriteFactoryPtr factoryPtr, std::shared_ptr<xivres::stream> pStream);

		HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override;

		ULONG __stdcall AddRef() override;

		ULONG __stdcall Release() override;

		HRESULT __stdcall MoveNext(BOOL* pHasCurrentFile) override;

		HRESULT __stdcall GetCurrentFontFile(IDWriteFontFile** pFontFile) override;
	};

	IStreamBasedDWriteFontCollectionLoader() = default;
	IStreamBasedDWriteFontCollectionLoader(IStreamBasedDWriteFontCollectionLoader&&) = delete;
	IStreamBasedDWriteFontCollectionLoader(const IStreamBasedDWriteFontCollectionLoader&) = delete;
	IStreamBasedDWriteFontCollectionLoader& operator=(IStreamBasedDWriteFontCollectionLoader&&) = delete;
	IStreamBasedDWriteFontCollectionLoader& operator=(const IStreamBasedDWriteFontCollectionLoader&) = delete;

public:
	static IStreamBasedDWriteFontCollectionLoader& GetInstance();

	HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override;

	ULONG __stdcall AddRef() override;

	ULONG __stdcall Release() override;

	HRESULT __stdcall CreateEnumeratorFromKey(IDWriteFactory* factory, void const* collectionKey, uint32_t collectionKeySize, IDWriteFontFileEnumerator** pFontFileEnumerator) override;
};

class DWriteFontTable {
	const IDWriteFontFacePtr m_pFontFace;
	const void* m_pData;
	void* m_pTableContext;
	uint32_t m_nSize;
	BOOL m_bExists;

public:
	DWriteFontTable(IDWriteFontFace* pFace, uint32_t tag);

	~DWriteFontTable();

	operator bool() const;

	template<typename T = uint8_t>
	std::span<const T> GetSpan() const {
		if (!m_bExists)
			return {};

		return { static_cast<const T*>(m_pData), m_nSize };
	}
};

xivres::fontgen::DirectWriteFixedSizeFont::DirectWriteFixedSizeFont(std::filesystem::path path, int fontIndex, float size, float gamma, const FontRenderTransformationMatrix& matrix, CreateStruct params)
	: DirectWriteFixedSizeFont(std::make_shared<memory_stream>(file_stream(path)), fontIndex, size, gamma, matrix, std::move(params)) {
}

xivres::fontgen::DirectWriteFixedSizeFont::DirectWriteFixedSizeFont(IDWriteFactoryPtr factory, IDWriteFontPtr font, float size, float gamma, const FontRenderTransformationMatrix& matrix, CreateStruct params) {
	if (!font)
		return;

	auto info = std::make_shared<ParsedInfoStruct>();
	info->Factory = std::move(factory);
	info->Font = std::move(font);
	info->Params = std::move(params);
	info->Size = size;
	info->GammaTable = util::bitmap_copy::create_gamma_table(gamma);
	info->Matrix = { matrix.M11, matrix.M12, matrix.M21, matrix.M22, 0.f, 0.f };

	m_dwrite = FaceFromInfoStruct(*info);
	m_dwrite.Face->GetMetrics(&info->Metrics);

	{
		uint32_t rangeCount;
		SuccessOrThrow(m_dwrite.Face1->GetUnicodeRanges(0, nullptr, &rangeCount), { E_NOT_SUFFICIENT_BUFFER });
		std::vector<DWRITE_UNICODE_RANGE> ranges(rangeCount);
		SuccessOrThrow(m_dwrite.Face1->GetUnicodeRanges(rangeCount, &ranges[0], &rangeCount));

		for (const auto& range : ranges)
			for (uint32_t i = range.first; i <= range.last; ++i)
				info->Characters.insert(static_cast<char32_t>(i));
	}
	{
		DWriteFontTable kernDataRef(m_dwrite.Face, util::TrueType::Kern::DirectoryTableTag.NativeValue);
		DWriteFontTable gposDataRef(m_dwrite.Face, util::TrueType::Gpos::DirectoryTableTag.NativeValue);
		DWriteFontTable cmapDataRef(m_dwrite.Face, util::TrueType::Cmap::DirectoryTableTag.NativeValue);
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
				it->second = info->ScaleFromFontUnit(it->second);
				if (it->second)
					++it;
				else
					it = info->KerningPairs.erase(it);
			}
		}
	}

	m_info = std::move(info);
}

xivres::fontgen::DirectWriteFixedSizeFont::DirectWriteFixedSizeFont(std::shared_ptr<xivres::stream> strm, int fontIndex, float size, float gamma, const FontRenderTransformationMatrix& matrix, CreateStruct params) {
	if (!strm)
		return;

	auto info = std::make_shared<ParsedInfoStruct>();
	info->Stream = std::move(strm);
	info->Params = std::move(params);
	info->FontIndex = fontIndex;
	info->Size = size;
	info->GammaTable = util::bitmap_copy::create_gamma_table(gamma);
	info->Matrix = { matrix.M11, matrix.M12, matrix.M21, matrix.M22, 0.f, 0.f };

	m_dwrite = FaceFromInfoStruct(*info);
	m_dwrite.Face->GetMetrics(&info->Metrics);

	{
		uint32_t rangeCount;
		SuccessOrThrow(m_dwrite.Face1->GetUnicodeRanges(0, nullptr, &rangeCount), { E_NOT_SUFFICIENT_BUFFER });
		std::vector<DWRITE_UNICODE_RANGE> ranges(rangeCount);
		SuccessOrThrow(m_dwrite.Face1->GetUnicodeRanges(rangeCount, &ranges[0], &rangeCount));

		for (const auto& range : ranges)
			for (uint32_t i = range.first; i <= range.last; ++i)
				info->Characters.insert(static_cast<char32_t>(i));
	}
	{
		DWriteFontTable kernDataRef(m_dwrite.Face, util::TrueType::Kern::DirectoryTableTag.NativeValue);
		DWriteFontTable gposDataRef(m_dwrite.Face, util::TrueType::Gpos::DirectoryTableTag.NativeValue);
		DWriteFontTable cmapDataRef(m_dwrite.Face, util::TrueType::Cmap::DirectoryTableTag.NativeValue);
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
				it->second = info->ScaleFromFontUnit(it->second);
				if (it->second)
					++it;
				else
					it = info->KerningPairs.erase(it);
			}
		}
	}

	m_info = std::move(info);
}

xivres::fontgen::DirectWriteFixedSizeFont::DirectWriteFixedSizeFont(const DirectWriteFixedSizeFont& r) : DirectWriteFixedSizeFont() {
	if (r.m_info == nullptr)
		return;

	m_dwrite = FaceFromInfoStruct(*r.m_info);
	m_info = r.m_info;
}

xivres::fontgen::DirectWriteFixedSizeFont::DirectWriteFixedSizeFont() = default;

xivres::fontgen::DirectWriteFixedSizeFont::DirectWriteFixedSizeFont(DirectWriteFixedSizeFont&&)  noexcept = default;

const std::map<std::pair<char32_t, char32_t>, int>& xivres::fontgen::DirectWriteFixedSizeFont::GetAllKerningPairs() const {
	return m_info->KerningPairs;
}

const std::set<char32_t>& xivres::fontgen::DirectWriteFixedSizeFont::GetAllCodepoints() const {
	return m_info->Characters;
}

int xivres::fontgen::DirectWriteFixedSizeFont::GetLineHeight() const {
	return m_info->ScaleFromFontUnit(m_info->Metrics.ascent + m_info->Metrics.descent + m_info->Metrics.lineGap);
}

int xivres::fontgen::DirectWriteFixedSizeFont::GetAscent() const {
	return m_info->ScaleFromFontUnit(m_info->Metrics.ascent);
}

float xivres::fontgen::DirectWriteFixedSizeFont::GetSize() const {
	return m_info->Size;
}

std::string xivres::fontgen::DirectWriteFixedSizeFont::GetSubfamilyName() const {
	IDWriteLocalizedStringsPtr strings;
	SuccessOrThrow(m_dwrite.Font->GetFaceNames(&strings));

	uint32_t index;
	BOOL exists;
	SuccessOrThrow(strings->FindLocaleName(L"en-us", &index, &exists));
	if (exists)
		index = 0;

	uint32_t length;
	SuccessOrThrow(strings->GetStringLength(index, &length));

	std::wstring res(length + 1, L'\0');
	SuccessOrThrow(strings->GetString(index, &res[0], length + 1));
	res.resize(length);

	return util::unicode::convert<std::string>(res);
}

std::string xivres::fontgen::DirectWriteFixedSizeFont::GetFamilyName() const {
	IDWriteLocalizedStringsPtr strings;
	SuccessOrThrow(m_dwrite.Family->GetFamilyNames(&strings));

	uint32_t index;
	BOOL exists;
	SuccessOrThrow(strings->FindLocaleName(L"en-us", &index, &exists));
	if (exists)
		index = 0;

	uint32_t length;
	SuccessOrThrow(strings->GetStringLength(index, &length));

	std::wstring res(length + 1, L'\0');
	SuccessOrThrow(strings->GetString(index, &res[0], length + 1));
	res.resize(length);

	return util::unicode::convert<std::string>(res);
}

xivres::fontgen::DirectWriteFixedSizeFont& xivres::fontgen::DirectWriteFixedSizeFont::operator=(const DirectWriteFixedSizeFont & r) {
	if (this == &r)
		return *this;

	if (r.m_info == nullptr) {
		m_dwrite = {};
		m_info = nullptr;
	} else {
		m_dwrite = FaceFromInfoStruct(*r.m_info);
		m_info = r.m_info;
	}

	return *this;
}

xivres::fontgen::DirectWriteFixedSizeFont& xivres::fontgen::DirectWriteFixedSizeFont::operator=(DirectWriteFixedSizeFont&&) noexcept = default;

bool xivres::fontgen::DirectWriteFixedSizeFont::Draw(char32_t codepoint, util::RGBA8888 * pBuf, int drawX, int drawY, int destWidth, int destHeight, util::RGBA8888 fgColor, util::RGBA8888 bgColor) const {
	IDWriteGlyphRunAnalysisPtr analysis;
	GlyphMetrics gm;
	if (!GetGlyphMetrics(codepoint, gm, analysis))
		return false;

	auto src = gm;
	src.Translate(-src.X1, -src.Y1);
	auto dest = gm;
	dest.Translate(drawX, drawY + GetAscent());
	src.AdjustToIntersection(dest, src.GetWidth(), src.GetHeight(), destWidth, destHeight);
	if (src.IsEffectivelyEmpty() || dest.IsEffectivelyEmpty())
		return true;

	m_drawBuffer.resize(gm.GetArea());
	SuccessOrThrow(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, gm.AsConstRectPtr(), &m_drawBuffer[0], static_cast<uint32_t>(m_drawBuffer.size())));

	util::bitmap_copy::to_rgba8888()
		.from(&m_drawBuffer[0], gm.GetWidth(), gm.GetHeight(), 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.back_color(bgColor)
		.gamma_table(m_info->GammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);

	return true;
}

bool xivres::fontgen::DirectWriteFixedSizeFont::Draw(char32_t codepoint, uint8_t * pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	IDWriteGlyphRunAnalysisPtr analysis;
	GlyphMetrics gm;
	if (!GetGlyphMetrics(codepoint, gm, analysis))
		return false;

	auto src = gm;
	src.Translate(-src.X1, -src.Y1);
	auto dest = gm;
	dest.Translate(drawX, drawY + GetAscent());
	src.AdjustToIntersection(dest, src.GetWidth(), src.GetHeight(), destWidth, destHeight);
	if (src.IsEffectivelyEmpty() || dest.IsEffectivelyEmpty())
		return true;

	m_drawBuffer.resize(gm.GetArea());
	SuccessOrThrow(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, gm.AsConstRectPtr(), &m_drawBuffer[0], static_cast<uint32_t>(m_drawBuffer.size())));

	util::bitmap_copy::to_l8()
		.from(&m_drawBuffer[0], gm.GetWidth(), gm.GetHeight(), 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, stride, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.fore_opacity(fgOpacity)
		.back_color(bgColor)
		.back_opacity(bgOpacity)
		.gamma_table(m_info->GammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

std::shared_ptr<xivres::fontgen::IFixedSizeFont> xivres::fontgen::DirectWriteFixedSizeFont::GetThreadSafeView() const {
	return std::make_shared<DirectWriteFixedSizeFont>(*this);
}

const xivres::fontgen::IFixedSizeFont* xivres::fontgen::DirectWriteFixedSizeFont::GetBaseFont(char32_t codepoint) const {
	return this;
}

xivres::fontgen::DirectWriteFixedSizeFont::DWriteInterfaceStruct xivres::fontgen::DirectWriteFixedSizeFont::FaceFromInfoStruct(const ParsedInfoStruct & info) {
	DWriteInterfaceStruct res{};

	if (!!info.Font != !!info.Factory)
		throw std::invalid_argument("Both Font and Factory either must be set or not set.");

	if (info.Font) {
		res.Font = info.Font;
		res.Factory = info.Factory;
		SuccessOrThrow(res.Factory.QueryInterface(decltype(res.Factory3)::GetIID(), &res.Factory3), { E_NOINTERFACE });
		SuccessOrThrow(res.Font->GetFontFamily(&res.Family));
		SuccessOrThrow(res.Family->GetFontCollection(&res.Collection));
		SuccessOrThrow(res.Font->CreateFontFace(&res.Face));
		SuccessOrThrow(res.Face.QueryInterface(decltype(res.Face1)::GetIID(), &res.Face1));

	} else {
		SuccessOrThrow(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&res.Factory)));
		SuccessOrThrow(res.Factory->RegisterFontFileLoader(&IStreamBasedDWriteFontFileLoader::GetInstance()), { DWRITE_E_ALREADYREGISTERED });
		SuccessOrThrow(res.Factory->RegisterFontCollectionLoader(&IStreamBasedDWriteFontCollectionLoader::GetInstance()), { DWRITE_E_ALREADYREGISTERED });
		SuccessOrThrow(res.Factory.QueryInterface(decltype(res.Factory3)::GetIID(), &res.Factory3), { E_NOINTERFACE });
		SuccessOrThrow(res.Factory->CreateCustomFontCollection(&IStreamBasedDWriteFontCollectionLoader::GetInstance(), &info.Stream, sizeof info.Stream, &res.Collection));
		SuccessOrThrow(res.Collection->GetFontFamily(0, &res.Family));
		SuccessOrThrow(res.Family->GetFont(info.FontIndex, &res.Font));
		SuccessOrThrow(res.Font->CreateFontFace(&res.Face));
		SuccessOrThrow(res.Face.QueryInterface(decltype(res.Face1)::GetIID(), &res.Face1));
	}

	return res;
}

bool xivres::fontgen::DirectWriteFixedSizeFont::GetGlyphMetrics(char32_t codepoint, GlyphMetrics & gm, IDWriteGlyphRunAnalysisPtr & analysis) const {
	try {
		uint16_t glyphIndex;
		SuccessOrThrow(m_dwrite.Face->GetGlyphIndices(reinterpret_cast<const uint32_t*>(&codepoint), 1, &glyphIndex));
		if (!glyphIndex)
			return false;

		DWRITE_GLYPH_METRICS dgm;
		SuccessOrThrow(m_dwrite.Face->GetGdiCompatibleGlyphMetrics(
			m_info->Size, 1.0f, &m_info->Matrix,
			m_info->Params.MeasureMode == DWRITE_MEASURING_MODE_GDI_NATURAL ? TRUE : FALSE,
			&glyphIndex, 1, &dgm));

		float glyphAdvance{};
		DWRITE_GLYPH_OFFSET glyphOffset{};
		const DWRITE_GLYPH_RUN run{
			.fontFace = m_dwrite.Face,
			.fontEmSize = m_info->Size,
			.glyphCount = 1,
			.glyphIndices = &glyphIndex,
			.glyphAdvances = &glyphAdvance,
			.glyphOffsets = &glyphOffset,
			.isSideways = FALSE,
			.bidiLevel = 0,
		};

		auto renderMode = m_info->Params.RenderMode;
		if (renderMode == DWRITE_RENDERING_MODE_DEFAULT)
			SuccessOrThrow(m_dwrite.Face->GetRecommendedRenderingMode(m_info->Size, 1.f, m_info->Params.MeasureMode, nullptr, &renderMode));

		SuccessOrThrow(m_dwrite.Factory3->CreateGlyphRunAnalysis(
			&run,
			&m_info->Matrix,
			renderMode,
			m_info->Params.MeasureMode,
			m_info->Params.GridFitMode,
			DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
			0,
			0,
			&analysis));

		SuccessOrThrow(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, gm.AsMutableRectPtr()));

		gm.AdvanceX = m_info->ScaleFromFontUnit(static_cast<float>(dgm.advanceWidth) * m_info->Matrix.m11);

		return true;

	} catch (...) {
		return false;
	}
}

bool xivres::fontgen::DirectWriteFixedSizeFont::GetGlyphMetrics(char32_t codepoint, GlyphMetrics & gm) const {
	IDWriteGlyphRunAnalysisPtr analysis;
	if (!GetGlyphMetrics(codepoint, gm, analysis))
		return false;

	gm.Translate(0, GetAscent());
	return true;
}

const wchar_t* xivres::fontgen::DirectWriteFixedSizeFont::CreateStruct::GetGridFitModeString() const {
	switch (GridFitMode) {
		case DWRITE_GRID_FIT_MODE_DEFAULT: return L"Default";
		case DWRITE_GRID_FIT_MODE_DISABLED: return L"Disabled";
		case DWRITE_GRID_FIT_MODE_ENABLED: return L"Enabled";
		default: return L"Invalid";
	}
}

const wchar_t* xivres::fontgen::DirectWriteFixedSizeFont::CreateStruct::GetRenderModeString() const {
	switch (RenderMode) {
		case DWRITE_RENDERING_MODE_DEFAULT: return L"Default";
		case DWRITE_RENDERING_MODE_ALIASED: return L"Aliased";
		case DWRITE_RENDERING_MODE_GDI_CLASSIC: return L"GDI Classic";
		case DWRITE_RENDERING_MODE_GDI_NATURAL: return L"GDI Natural";
		case DWRITE_RENDERING_MODE_NATURAL: return L"Natural";
		case DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC: return L"Natural Symmetric";
		default: return L"Invalid";
	}
}

const wchar_t* xivres::fontgen::DirectWriteFixedSizeFont::CreateStruct::GetMeasuringModeString() const {
	switch (MeasureMode) {
		case DWRITE_MEASURING_MODE_NATURAL: return L"Natural";
		case DWRITE_MEASURING_MODE_GDI_CLASSIC: return L"GDI Classic";
		case DWRITE_MEASURING_MODE_GDI_NATURAL: return L"GDI Natural";
		default: return L"Invalid";
	}
}

DWriteFontTable::operator bool() const {
	return m_bExists;
}

DWriteFontTable::~DWriteFontTable() {
	if (m_bExists)
		m_pFontFace->ReleaseFontTable(m_pTableContext);
}

DWriteFontTable::DWriteFontTable(IDWriteFontFace * pFace, uint32_t tag)
	: m_pFontFace(pFace, true) {
	SuccessOrThrow(pFace->TryGetFontTable(tag, &m_pData, &m_nSize, &m_pTableContext, &m_bExists));
}

HRESULT __stdcall IStreamBasedDWriteFontCollectionLoader::CreateEnumeratorFromKey(IDWriteFactory * factory, void const* collectionKey, uint32_t collectionKeySize, IDWriteFontFileEnumerator * *pFontFileEnumerator) {
	if (collectionKeySize != sizeof(std::shared_ptr<xivres::stream>))
		return E_INVALIDARG;


	*pFontFileEnumerator = IStreamAsDWriteFontFileEnumerator::New(IDWriteFactoryPtr(factory, true), *static_cast<const std::shared_ptr<xivres::stream>*>(collectionKey));
	return S_OK;
}

ULONG __stdcall IStreamBasedDWriteFontCollectionLoader::Release(void) {
	return 0;
}

ULONG __stdcall IStreamBasedDWriteFontCollectionLoader::AddRef(void) {
	return 1;
}

HRESULT __stdcall IStreamBasedDWriteFontCollectionLoader::QueryInterface(REFIID riid, void** ppvObject) {
	if (riid == __uuidof(IUnknown))
		*ppvObject = static_cast<IUnknown*>(this);
	else if (riid == __uuidof(IDWriteFontCollectionLoader))
		*ppvObject = static_cast<IDWriteFontCollectionLoader*>(this);
	else
		*ppvObject = nullptr;

	if (!*ppvObject)
		return E_NOINTERFACE;

	AddRef();
	return S_OK;
}

IStreamBasedDWriteFontCollectionLoader& IStreamBasedDWriteFontCollectionLoader::GetInstance() {
	static IStreamBasedDWriteFontCollectionLoader s_instance;
	return s_instance;
}

HRESULT __stdcall IStreamBasedDWriteFontCollectionLoader::IStreamAsDWriteFontFileEnumerator::GetCurrentFontFile(IDWriteFontFile * *pFontFile) {
	if (m_nCurrentFile != 0)
		return E_FAIL;

	return m_factory->CreateCustomFontFileReference(&m_stream, sizeof m_stream, &IStreamBasedDWriteFontFileLoader::GetInstance(), pFontFile);
}

HRESULT __stdcall IStreamBasedDWriteFontCollectionLoader::IStreamAsDWriteFontFileEnumerator::MoveNext(BOOL * pHasCurrentFile) {
	if (m_nCurrentFile == -1) {
		m_nCurrentFile = 0;
		*pHasCurrentFile = TRUE;
	} else {
		m_nCurrentFile = 1;
		*pHasCurrentFile = FALSE;
	}
	return S_OK;
}

ULONG __stdcall IStreamBasedDWriteFontCollectionLoader::IStreamAsDWriteFontFileEnumerator::Release(void) {
	const auto newRef = --m_nRef;
	if (!newRef)
		delete this;
	return newRef;
}

ULONG __stdcall IStreamBasedDWriteFontCollectionLoader::IStreamAsDWriteFontFileEnumerator::AddRef(void) {
	return ++m_nRef;
}

HRESULT __stdcall IStreamBasedDWriteFontCollectionLoader::IStreamAsDWriteFontFileEnumerator::QueryInterface(REFIID riid, void** ppvObject) {
	if (riid == __uuidof(IUnknown))
		*ppvObject = static_cast<IUnknown*>(this);
	else if (riid == __uuidof(IDWriteFontFileEnumerator))
		*ppvObject = static_cast<IDWriteFontFileEnumerator*>(this);
	else
		*ppvObject = nullptr;

	if (!*ppvObject)
		return E_NOINTERFACE;

	AddRef();
	return S_OK;
}

IStreamBasedDWriteFontCollectionLoader::IStreamAsDWriteFontFileEnumerator* IStreamBasedDWriteFontCollectionLoader::IStreamAsDWriteFontFileEnumerator::New(IDWriteFactoryPtr factoryPtr, std::shared_ptr<xivres::stream> pStream) {
	return new IStreamAsDWriteFontFileEnumerator(std::move(factoryPtr), std::move(pStream));
}

IStreamBasedDWriteFontCollectionLoader::IStreamAsDWriteFontFileEnumerator::IStreamAsDWriteFontFileEnumerator(IDWriteFactoryPtr factoryPtr, std::shared_ptr<xivres::stream> pStream)
	: m_factory(factoryPtr)
	, m_stream(std::move(pStream)) {

}

HRESULT __stdcall IStreamBasedDWriteFontFileLoader::CreateStreamFromKey(void const* fontFileReferenceKey, uint32_t fontFileReferenceKeySize, IDWriteFontFileStream * *pFontFileStream) {
	if (fontFileReferenceKeySize != sizeof(std::shared_ptr<xivres::stream>))
		return E_INVALIDARG;

	*pFontFileStream = IStreamAsDWriteFontFileStream::New(*static_cast<const std::shared_ptr<xivres::stream>*>(fontFileReferenceKey));
	return S_OK;
}

ULONG __stdcall IStreamBasedDWriteFontFileLoader::Release(void) {
	return 0;
}

ULONG __stdcall IStreamBasedDWriteFontFileLoader::AddRef(void) {
	return 1;
}

HRESULT __stdcall IStreamBasedDWriteFontFileLoader::QueryInterface(REFIID riid, void** ppvObject) {
	if (riid == __uuidof(IUnknown))
		*ppvObject = static_cast<IUnknown*>(this);
	else if (riid == __uuidof(IDWriteFontFileLoader))
		*ppvObject = static_cast<IDWriteFontFileLoader*>(this);
	else
		*ppvObject = nullptr;

	if (!*ppvObject)
		return E_NOINTERFACE;

	AddRef();
	return S_OK;
}

IStreamBasedDWriteFontFileLoader& IStreamBasedDWriteFontFileLoader::GetInstance() {
	static IStreamBasedDWriteFontFileLoader s_instance;
	return s_instance;
}

HRESULT __stdcall IStreamBasedDWriteFontFileLoader::IStreamAsDWriteFontFileStream::GetLastWriteTime(uint64_t * pLastWriteTime) {
	*pLastWriteTime = 0;
	return E_NOTIMPL; // E_NOTIMPL by design -- see method documentation in dwrite.h.
}

HRESULT __stdcall IStreamBasedDWriteFontFileLoader::IStreamAsDWriteFontFileStream::GetFileSize(uint64_t * pFileSize) {
	*pFileSize = static_cast<uint16_t>(m_stream->size());
	return S_OK;
}

void __stdcall IStreamBasedDWriteFontFileLoader::IStreamAsDWriteFontFileStream::ReleaseFileFragment(void* fragmentContext) {
	if (fragmentContext)
		delete static_cast<std::vector<uint8_t>*>(fragmentContext);
}

HRESULT __stdcall IStreamBasedDWriteFontFileLoader::IStreamAsDWriteFontFileStream::ReadFileFragment(void const** pFragmentStart, uint64_t fileOffset, uint64_t fragmentSize, void** pFragmentContext) {
	*pFragmentContext = nullptr;
	*pFragmentStart = nullptr;

	if (const auto pMemoryStream = dynamic_cast<xivres::memory_stream*>(m_stream.get())) {
		try {
			*pFragmentStart = pMemoryStream->as_span(static_cast<std::streamoff>(fileOffset), static_cast<std::streamsize>(fragmentSize)).data();
			return S_OK;
		} catch (const std::out_of_range&) {
			return E_INVALIDARG;
		}

	} else {
		const auto size = static_cast<uint64_t>(m_stream->size());
		if (fileOffset <= size && fileOffset + fragmentSize <= size && fragmentSize <= (std::numeric_limits<size_t>::max)()) {
			auto pVec = new std::vector<uint8_t>();
			try {
				pVec->resize(static_cast<size_t>(fragmentSize));
				if (m_stream->read(static_cast<std::streamoff>(fileOffset), pVec->data(), static_cast<std::streamsize>(fragmentSize)) == fragmentSize) {
					*pFragmentStart = pVec->data();
					*pFragmentContext = pVec;
					return S_OK;
				}
			} catch (...) {
				// pass
			}
			delete pVec;
			return E_FAIL;

		} else
			return E_INVALIDARG;
	}
}

ULONG __stdcall IStreamBasedDWriteFontFileLoader::IStreamAsDWriteFontFileStream::Release(void) {
	const auto newRef = --m_nRef;
	if (!newRef)
		delete this;
	return newRef;
}

ULONG __stdcall IStreamBasedDWriteFontFileLoader::IStreamAsDWriteFontFileStream::AddRef(void) {
	return ++m_nRef;
}

HRESULT __stdcall IStreamBasedDWriteFontFileLoader::IStreamAsDWriteFontFileStream::QueryInterface(REFIID riid, void** ppvObject) {
	if (riid == __uuidof(IUnknown))
		*ppvObject = static_cast<IUnknown*>(this);
	else if (riid == __uuidof(IDWriteFontFileStream))
		*ppvObject = static_cast<IDWriteFontFileStream*>(this);
	else
		*ppvObject = nullptr;

	if (!*ppvObject)
		return E_NOINTERFACE;

	AddRef();
	return S_OK;
}

IStreamBasedDWriteFontFileLoader::IStreamAsDWriteFontFileStream* IStreamBasedDWriteFontFileLoader::IStreamAsDWriteFontFileStream::New(std::shared_ptr<xivres::stream> pStream) {
	return new IStreamAsDWriteFontFileStream(std::move(pStream));
}

IStreamBasedDWriteFontFileLoader::IStreamAsDWriteFontFileStream::IStreamAsDWriteFontFileStream(std::shared_ptr<xivres::stream> pStream)
	: m_stream(std::move(pStream)) {

}
