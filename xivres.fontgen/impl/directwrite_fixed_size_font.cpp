#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "../include/xivres.fontgen/directwrite_fixed_size_font.h"

static HRESULT success_or_throw(HRESULT hr, std::initializer_list<HRESULT> acceptables = {}) {
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
		nullptr);
	if (pszMsg) {
		std::unique_ptr<wchar_t, decltype(&LocalFree)> pszMsgFree(pszMsg, LocalFree);

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

class stream_based_dwrite_font_file_loader : public IDWriteFontFileLoader {
	class stream_based_dwrite_font_file_stream : public IDWriteFontFileStream {
		const std::shared_ptr<xivres::stream> m_stream;

		std::atomic_uint32_t m_nRef = 1;

		stream_based_dwrite_font_file_stream(std::shared_ptr<xivres::stream> pStream);

	public:
		virtual ~stream_based_dwrite_font_file_stream() = default;

		static stream_based_dwrite_font_file_stream* New(std::shared_ptr<xivres::stream> pStream);

		HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) noexcept override;

		ULONG __stdcall AddRef() noexcept override;

		ULONG __stdcall Release() noexcept override;

		HRESULT __stdcall ReadFileFragment(void const** pFragmentStart, uint64_t fileOffset, uint64_t fragmentSize, void** pFragmentContext) noexcept override;

		void __stdcall ReleaseFileFragment(void* fragmentContext) noexcept override;

		HRESULT __stdcall GetFileSize(uint64_t* pFileSize) noexcept override;

		HRESULT __stdcall GetLastWriteTime(uint64_t* pLastWriteTime) noexcept override;
	};

	stream_based_dwrite_font_file_loader() = default;

public:
	stream_based_dwrite_font_file_loader(stream_based_dwrite_font_file_loader&&) = delete;
	stream_based_dwrite_font_file_loader(const stream_based_dwrite_font_file_loader&) = delete;
	stream_based_dwrite_font_file_loader& operator=(stream_based_dwrite_font_file_loader&&) = delete;
	stream_based_dwrite_font_file_loader& operator=(const stream_based_dwrite_font_file_loader&) = delete;
	virtual ~stream_based_dwrite_font_file_loader() = default;

	static stream_based_dwrite_font_file_loader& GetInstance();

	HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) noexcept override;

	ULONG __stdcall AddRef() noexcept override;

	ULONG __stdcall Release() noexcept override;

	HRESULT __stdcall CreateStreamFromKey(void const* fontFileReferenceKey, uint32_t fontFileReferenceKeySize, IDWriteFontFileStream** pFontFileStream) noexcept override;
};

class stream_based_dwrite_font_collection_loader : public IDWriteFontCollectionLoader {
public:
	virtual ~stream_based_dwrite_font_collection_loader() = default;

	class stream_based_dwrite_font_collection_enumerator : public IDWriteFontFileEnumerator {
		const IDWriteFactoryPtr m_factory;
		const std::shared_ptr<xivres::stream> m_stream;

		std::atomic_uint32_t m_nRef = 1;
		int m_nCurrentFile = -1;

		stream_based_dwrite_font_collection_enumerator(IDWriteFactoryPtr factoryPtr, std::shared_ptr<xivres::stream> pStream);

	public:
		virtual ~stream_based_dwrite_font_collection_enumerator() = default;

		static stream_based_dwrite_font_collection_enumerator* New(IDWriteFactoryPtr factoryPtr, std::shared_ptr<xivres::stream> pStream);

		HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) noexcept override;

		ULONG __stdcall AddRef() noexcept override;

		ULONG __stdcall Release() noexcept override;

		HRESULT __stdcall MoveNext(BOOL* pHasCurrentFile) noexcept override;

		HRESULT __stdcall GetCurrentFontFile(IDWriteFontFile** pFontFile) noexcept override;
	};

	stream_based_dwrite_font_collection_loader() = default;
	stream_based_dwrite_font_collection_loader(stream_based_dwrite_font_collection_loader&&) = delete;
	stream_based_dwrite_font_collection_loader(const stream_based_dwrite_font_collection_loader&) = delete;
	stream_based_dwrite_font_collection_loader& operator=(stream_based_dwrite_font_collection_loader&&) = delete;
	stream_based_dwrite_font_collection_loader& operator=(const stream_based_dwrite_font_collection_loader&) = delete;

	static stream_based_dwrite_font_collection_loader& GetInstance();

	HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) noexcept override;

	ULONG __stdcall AddRef() noexcept override;

	ULONG __stdcall Release() noexcept override;

	HRESULT __stdcall CreateEnumeratorFromKey(IDWriteFactory* factory, void const* collectionKey, uint32_t collectionKeySize, IDWriteFontFileEnumerator** pFontFileEnumerator) noexcept override;
};

class dwrite_font_table {
	const IDWriteFontFacePtr m_pFontFace;
	const void* m_pData;
	void* m_pTableContext;
	uint32_t m_nSize;
	BOOL m_bExists;

public:
	dwrite_font_table(IDWriteFontFace* pFace, uint32_t tag);

	~dwrite_font_table();

	operator bool() const;

	template<typename T = uint8_t>
	[[nodiscard]] std::span<const T> get_span() const {
		if (!m_bExists)
			return {};

		return {static_cast<const T*>(m_pData), m_nSize};
	}
};

xivres::fontgen::directwrite_fixed_size_font::directwrite_fixed_size_font(std::filesystem::path path, int fontIndex, float size, float gamma, const font_render_transformation_matrix& matrix, const create_struct& params)
	: directwrite_fixed_size_font(std::make_shared<memory_stream>(file_stream(std::move(path))), fontIndex, size, gamma, matrix, params) {}

xivres::fontgen::directwrite_fixed_size_font::directwrite_fixed_size_font(IDWriteFactoryPtr factory, IDWriteFontPtr font, float size, float gamma, const font_render_transformation_matrix& matrix, const create_struct& params) {
	if (!font)
		return;

	auto info = std::make_shared<info_t>();
	info->Factory = std::move(factory);
	info->Font = std::move(font);
	info->Params = params;
	info->Size = size;
	info->GammaTable = util::bitmap_copy::create_gamma_table(gamma);
	info->Matrix = {matrix.M11, matrix.M12, matrix.M21, matrix.M22, 0.f, 0.f};

	m_dwrite = face_from_info_t(*info);
	m_dwrite.Face->GetMetrics(&info->Metrics);

	{
		uint32_t rangeCount;
		success_or_throw(m_dwrite.Face1->GetUnicodeRanges(0, nullptr, &rangeCount), {E_NOT_SUFFICIENT_BUFFER});
		std::vector<DWRITE_UNICODE_RANGE> ranges(rangeCount);
		success_or_throw(m_dwrite.Face1->GetUnicodeRanges(rangeCount, &ranges[0], &rangeCount));

		for (const auto& range : ranges)
			for (uint32_t i = range.first; i <= range.last; ++i)
				info->Characters.insert(static_cast<char32_t>(i));
	}
	{
		dwrite_font_table kernDataRef(m_dwrite.Face, util::TrueType::Kern::DirectoryTableTag.NativeValue);
		dwrite_font_table gposDataRef(m_dwrite.Face, util::TrueType::Gpos::DirectoryTableTag.NativeValue);
		dwrite_font_table cmapDataRef(m_dwrite.Face, util::TrueType::Cmap::DirectoryTableTag.NativeValue);
		util::TrueType::Kern::View kern(kernDataRef.get_span<char>());
		util::TrueType::Gpos::View gpos(gposDataRef.get_span<char>());
		util::TrueType::Cmap::View cmap(cmapDataRef.get_span<char>());
		if (cmap && (kern || gpos)) {
			const auto cmapVector = cmap.GetGlyphToCharMap();

			if (kern)
				info->KerningPairs = kern.Parse(cmapVector);

			if (gpos) {
				const auto pairs = gpos.ExtractAdvanceX(cmapVector);
				// do not overwrite
				info->KerningPairs.insert(pairs.begin(), pairs.end());
			}

			for (auto it = info->KerningPairs.begin(); it != info->KerningPairs.end();) {
				it->second = info->scale_from_font_unit(it->second);
				if (it->second)
					++it;
				else
					it = info->KerningPairs.erase(it);
			}
		}
	}

	m_info = std::move(info);
}

xivres::fontgen::directwrite_fixed_size_font::directwrite_fixed_size_font(std::shared_ptr<xivres::stream> strm, int fontIndex, float size, float gamma, const font_render_transformation_matrix& matrix, const create_struct& params) {
	if (!strm)
		return;

	auto info = std::make_shared<info_t>();
	info->Stream = std::move(strm);
	info->Params = params;
	info->FontIndex = fontIndex;
	info->Size = size;
	info->GammaTable = util::bitmap_copy::create_gamma_table(gamma);
	info->Matrix = {matrix.M11, matrix.M12, matrix.M21, matrix.M22, 0.f, 0.f};

	m_dwrite = face_from_info_t(*info);
	m_dwrite.Face->GetMetrics(&info->Metrics);

	{
		uint32_t rangeCount;
		success_or_throw(m_dwrite.Face1->GetUnicodeRanges(0, nullptr, &rangeCount), {E_NOT_SUFFICIENT_BUFFER});
		std::vector<DWRITE_UNICODE_RANGE> ranges(rangeCount);
		success_or_throw(m_dwrite.Face1->GetUnicodeRanges(rangeCount, &ranges[0], &rangeCount));

		for (const auto& range : ranges)
			for (uint32_t i = range.first; i <= range.last; ++i)
				info->Characters.insert(static_cast<char32_t>(i));
	}
	{
		dwrite_font_table kernDataRef(m_dwrite.Face, util::TrueType::Kern::DirectoryTableTag.NativeValue);
		dwrite_font_table gposDataRef(m_dwrite.Face, util::TrueType::Gpos::DirectoryTableTag.NativeValue);
		dwrite_font_table cmapDataRef(m_dwrite.Face, util::TrueType::Cmap::DirectoryTableTag.NativeValue);
		util::TrueType::Kern::View kern(kernDataRef.get_span<char>());
		util::TrueType::Gpos::View gpos(gposDataRef.get_span<char>());
		util::TrueType::Cmap::View cmap(cmapDataRef.get_span<char>());
		if (cmap && (kern || gpos)) {
			const auto cmapVector = cmap.GetGlyphToCharMap();

			if (kern)
				info->KerningPairs = kern.Parse(cmapVector);

			if (gpos) {
				const auto pairs = gpos.ExtractAdvanceX(cmapVector);
				// do not overwrite
				info->KerningPairs.insert(pairs.begin(), pairs.end());
			}

			for (auto it = info->KerningPairs.begin(); it != info->KerningPairs.end();) {
				it->second = info->scale_from_font_unit(it->second);
				if (it->second)
					++it;
				else
					it = info->KerningPairs.erase(it);
			}
		}
	}

	m_info = std::move(info);
}

xivres::fontgen::directwrite_fixed_size_font::directwrite_fixed_size_font(const directwrite_fixed_size_font& r)
	: directwrite_fixed_size_font() {
	if (r.m_info == nullptr)
		return;

	m_dwrite = face_from_info_t(*r.m_info);
	m_info = r.m_info;
}

xivres::fontgen::directwrite_fixed_size_font::directwrite_fixed_size_font() = default;

xivres::fontgen::directwrite_fixed_size_font::directwrite_fixed_size_font(directwrite_fixed_size_font&&) noexcept = default;

const std::map<std::pair<char32_t, char32_t>, int>& xivres::fontgen::directwrite_fixed_size_font::all_kerning_pairs() const {
	return m_info->KerningPairs;
}

const std::set<char32_t>& xivres::fontgen::directwrite_fixed_size_font::all_codepoints() const {
	return m_info->Characters;
}

int xivres::fontgen::directwrite_fixed_size_font::line_height() const {
	return m_info->scale_from_font_unit(m_info->Metrics.ascent + m_info->Metrics.descent + m_info->Metrics.lineGap);
}

int xivres::fontgen::directwrite_fixed_size_font::ascent() const {
	return m_info->scale_from_font_unit(m_info->Metrics.ascent);
}

float xivres::fontgen::directwrite_fixed_size_font::font_size() const {
	return m_info->Size;
}

std::string xivres::fontgen::directwrite_fixed_size_font::subfamily_name() const {
	IDWriteLocalizedStringsPtr strings;
	success_or_throw(m_dwrite.Font->GetFaceNames(&strings));

	uint32_t index;
	BOOL exists;
	success_or_throw(strings->FindLocaleName(L"en-us", &index, &exists));
	if (exists)
		index = 0;

	uint32_t length;
	success_or_throw(strings->GetStringLength(index, &length));

	std::wstring res(length + 1, L'\0');
	success_or_throw(strings->GetString(index, &res[0], length + 1));
	res.resize(length);

	return util::unicode::convert<std::string>(res);
}

std::string xivres::fontgen::directwrite_fixed_size_font::family_name() const {
	IDWriteLocalizedStringsPtr strings;
	success_or_throw(m_dwrite.Family->GetFamilyNames(&strings));

	uint32_t index;
	BOOL exists;
	success_or_throw(strings->FindLocaleName(L"en-us", &index, &exists));
	if (exists)
		index = 0;

	uint32_t length;
	success_or_throw(strings->GetStringLength(index, &length));

	std::wstring res(length + 1, L'\0');
	success_or_throw(strings->GetString(index, &res[0], length + 1));
	res.resize(length);

	return util::unicode::convert<std::string>(res);
}

xivres::fontgen::directwrite_fixed_size_font& xivres::fontgen::directwrite_fixed_size_font::operator=(const directwrite_fixed_size_font& r) {
	if (this == &r)
		return *this;

	if (r.m_info == nullptr) {
		m_dwrite = {};
		m_info = nullptr;
	} else {
		m_dwrite = face_from_info_t(*r.m_info);
		m_info = r.m_info;
	}

	return *this;
}

xivres::fontgen::directwrite_fixed_size_font& xivres::fontgen::directwrite_fixed_size_font::operator=(directwrite_fixed_size_font&&) noexcept = default;

bool xivres::fontgen::directwrite_fixed_size_font::draw(char32_t codepoint, util::b8g8r8a8* pBuf, int drawX, int drawY, int destWidth, int destHeight, util::b8g8r8a8 fgColor, util::b8g8r8a8 bgColor) const {
	IDWriteGlyphRunAnalysisPtr analysis;
	glyph_metrics gm;
	if (!try_get_glyph_metrics(codepoint, gm, analysis))
		return false;

	auto src = gm;
	src.translate(-src.X1, -src.Y1);
	auto dest = gm;
	dest.translate(drawX, drawY + ascent());
	src.adjust_to_intersection(dest, src.width(), src.height(), destWidth, destHeight);
	if (src.is_effectively_empty() || dest.is_effectively_empty())
		return true;

	m_drawBuffer.resize(gm.area());
	success_or_throw(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, gm.as_const_rect_pointer(), &m_drawBuffer[0], static_cast<uint32_t>(m_drawBuffer.size())));

	util::bitmap_copy::to_b8g8r8a8()
		.from(&m_drawBuffer[0], gm.width(), gm.height(), 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.back_color(bgColor)
		.gamma_table(m_info->GammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);

	return true;
}

bool xivres::fontgen::directwrite_fixed_size_font::draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const {
	IDWriteGlyphRunAnalysisPtr analysis;
	glyph_metrics gm;
	if (!try_get_glyph_metrics(codepoint, gm, analysis))
		return false;

	auto src = gm;
	src.translate(-src.X1, -src.Y1);
	auto dest = gm;
	dest.translate(drawX, drawY + ascent());
	src.adjust_to_intersection(dest, src.width(), src.height(), destWidth, destHeight);
	if (src.is_effectively_empty() || dest.is_effectively_empty())
		return true;

	m_drawBuffer.resize(gm.area());
	success_or_throw(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, gm.as_const_rect_pointer(), &m_drawBuffer[0], static_cast<uint32_t>(m_drawBuffer.size())));

	util::bitmap_copy::to_l8()
		.from(&m_drawBuffer[0], gm.width(), gm.height(), 1, util::bitmap_vertical_direction::TopRowFirst)
		.to(pBuf, destWidth, destHeight, stride, util::bitmap_vertical_direction::TopRowFirst)
		.fore_color(fgColor)
		.fore_opacity(fgOpacity)
		.back_color(bgColor)
		.back_opacity(bgOpacity)
		.gamma_table(m_info->GammaTable)
		.copy(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
	return true;
}

std::shared_ptr<xivres::fontgen::fixed_size_font> xivres::fontgen::directwrite_fixed_size_font::get_threadsafe_view() const {
	return std::make_shared<directwrite_fixed_size_font>(*this);
}

const xivres::fontgen::fixed_size_font* xivres::fontgen::directwrite_fixed_size_font::get_base_font(char32_t codepoint) const {
	return this;
}

xivres::fontgen::directwrite_fixed_size_font::dwrite_interfaces_t xivres::fontgen::directwrite_fixed_size_font::face_from_info_t(const info_t& info) {
	dwrite_interfaces_t res{};

	if (!!info.Font != !!info.Factory)
		throw std::invalid_argument("Both Font and Factory either must be set or not set.");

	if (info.Font) {
		res.Font = info.Font;
		res.Factory = info.Factory;
		success_or_throw(res.Factory.QueryInterface(decltype(res.Factory3)::GetIID(), &res.Factory3), {E_NOINTERFACE});
		success_or_throw(res.Font->GetFontFamily(&res.Family));
		success_or_throw(res.Family->GetFontCollection(&res.Collection));
		success_or_throw(res.Font->CreateFontFace(&res.Face));
		success_or_throw(res.Face.QueryInterface(decltype(res.Face1)::GetIID(), &res.Face1));
	} else {
		success_or_throw(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&res.Factory)));
		success_or_throw(res.Factory->RegisterFontFileLoader(&stream_based_dwrite_font_file_loader::GetInstance()), {DWRITE_E_ALREADYREGISTERED});
		success_or_throw(res.Factory->RegisterFontCollectionLoader(&stream_based_dwrite_font_collection_loader::GetInstance()), {DWRITE_E_ALREADYREGISTERED});
		success_or_throw(res.Factory.QueryInterface(decltype(res.Factory3)::GetIID(), &res.Factory3), {E_NOINTERFACE});
		success_or_throw(res.Factory->CreateCustomFontCollection(&stream_based_dwrite_font_collection_loader::GetInstance(), &info.Stream, sizeof info.Stream, &res.Collection));
		success_or_throw(res.Collection->GetFontFamily(0, &res.Family));
		success_or_throw(res.Family->GetFont(info.FontIndex, &res.Font));
		success_or_throw(res.Font->CreateFontFace(&res.Face));
		success_or_throw(res.Face.QueryInterface(decltype(res.Face1)::GetIID(), &res.Face1));
	}

	IDWriteLocalizedStringsPtr familyNames;
	success_or_throw(res.Family->GetFamilyNames(&familyNames));
	uint32_t index;
	if (BOOL exists; FAILED(familyNames->FindLocaleName(L"en-us", &index, &exists)) || !exists)
		index = 0;
	uint32_t length;
	success_or_throw(familyNames->GetStringLength(index, &length));
	std::wstring familyName(length + 1, L'\0');
	success_or_throw(familyNames->GetString(index, familyName.data(), length + 1));
	familyName.resize(length);
	success_or_throw(res.Factory->CreateTextFormat(
		familyName.c_str(),
		res.Collection,
		res.Font->GetWeight(),
		res.Font->GetStyle(),
		res.Font->GetStretch(),
		info.Size,
		L"en-us",
		&res.Format));
	success_or_throw(res.Factory->CreateTypography(&res.Typography));
	for (const auto& feature : info.Params.Features)
		success_or_throw(res.Typography->AddFontFeature(feature));
	return res;
}

bool xivres::fontgen::directwrite_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm, IDWriteGlyphRunAnalysisPtr& analysis) const {
	try {
		wchar_t buf[3]{};
		UINT32 buflen;
		if (codepoint < 0x10000) {
			buf[0] = static_cast<wchar_t>(codepoint);
			buflen = 1;
		} else if (codepoint < 0x110000) {
			buf[0] = static_cast<wchar_t>(0xD800 + ((codepoint - 0x10000) >> 10));
			buf[1] = static_cast<wchar_t>(0xDC00 + ((codepoint - 0x10000) & 0x3FF));
			buflen = 2;
		} else {
			return false;
		}

		IDWriteTextLayoutPtr layout;
		success_or_throw(m_dwrite.Factory->CreateTextLayout(buf, buflen, m_dwrite.Format, 9999999, 9999999, &layout));
		success_or_throw(layout->SetTypography(m_dwrite.Typography, {.startPosition = 0, .length = buflen}));

		class DummyRenderer final : public IDWriteTextRenderer {
		public:
			uint16_t GlyphIndex = 0;

			STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
				if (!ppv)
					return E_INVALIDARG;

				if (riid == __uuidof(IUnknown)
					|| riid == __uuidof(IDWritePixelSnapping)
					|| riid == __uuidof(IDWriteTextRenderer)) {
					this->AddRef();
					*ppv = this;
					return S_OK;
				}

				return E_NOINTERFACE;
			}
			ULONG __stdcall AddRef() override {
				return 1;
			}
			ULONG __stdcall Release() override {
				return 0;
			}
			STDMETHOD(IsPixelSnappingDisabled)(_In_opt_ void* clientDrawingContext, _Out_ BOOL* isDisabled) override {
				*isDisabled = false;
				return S_OK;
			}
			STDMETHOD(GetCurrentTransform)(_In_opt_ void* clientDrawingContext, _Out_ DWRITE_MATRIX* transform) override {
				*transform = {1, 0, 0, 1, 0, 0};
				return S_OK;
			}
			STDMETHOD(GetPixelsPerDip)(_In_opt_ void* clientDrawingContext, _Out_ FLOAT* pixelsPerDip) override {
				*pixelsPerDip = 96;
				return S_OK;
			}
		    STDMETHOD(DrawGlyphRun)(
		        _In_opt_ void* clientDrawingContext,
		        FLOAT baselineOriginX,
		        FLOAT baselineOriginY,
		        DWRITE_MEASURING_MODE measuringMode,
		        _In_ DWRITE_GLYPH_RUN const* glyphRun,
		        _In_ DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
		        _In_opt_ IUnknown* clientDrawingEffect
		        ) override {
		        GlyphIndex = glyphRun->glyphIndices[0];
		        return S_OK;
		    }

			STDMETHOD(DrawUnderline)(_In_opt_ void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY, _In_ DWRITE_UNDERLINE const* underline, _In_opt_ IUnknown* clientDrawingEffect) override {
				return E_NOTIMPL;
			}
			STDMETHOD(DrawStrikethrough)(_In_opt_ void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY, _In_ DWRITE_STRIKETHROUGH const* strikethrough, _In_opt_ IUnknown* clientDrawingEffect) override {
				return E_NOTIMPL;
			}
			STDMETHOD(DrawInlineObject)(_In_opt_ void* clientDrawingContext, FLOAT originX, FLOAT originY, _In_ IDWriteInlineObject* inlineObject, BOOL isSideways, BOOL isRightToLeft, _In_opt_ IUnknown* clientDrawingEffect) override {
				return E_NOTIMPL;
			}
		} dummyRenderer;
		success_or_throw(layout->Draw(nullptr, &dummyRenderer, 0, 0));
		
		const auto glyphIndex = dummyRenderer.GlyphIndex;
		if (!glyphIndex)
			return false;

		DWRITE_GLYPH_METRICS dgm;
		success_or_throw(m_dwrite.Face->GetGdiCompatibleGlyphMetrics(
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
			success_or_throw(m_dwrite.Face->GetRecommendedRenderingMode(m_info->Size, 1.f, m_info->Params.MeasureMode, nullptr, &renderMode));

		success_or_throw(m_dwrite.Factory3->CreateGlyphRunAnalysis(
			&run,
			&m_info->Matrix,
			renderMode,
			m_info->Params.MeasureMode,
			m_info->Params.GridFitMode,
			DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
			0,
			0,
			&analysis));

		success_or_throw(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, gm.as_mutable_rect_pointer()));

		gm.AdvanceX = m_info->scale_from_font_unit(static_cast<float>(dgm.advanceWidth) * m_info->Matrix.m11);

		return true;
	} catch (...) {
		return false;
	}
}

bool xivres::fontgen::directwrite_fixed_size_font::try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const {
	IDWriteGlyphRunAnalysisPtr analysis;
	if (!try_get_glyph_metrics(codepoint, gm, analysis))
		return false;

	gm.translate(0, ascent());
	return true;
}

const wchar_t* xivres::fontgen::directwrite_fixed_size_font::create_struct::get_grid_fit_mode_string() const {
	switch (GridFitMode) {
		case DWRITE_GRID_FIT_MODE_DEFAULT: return L"Default";
		case DWRITE_GRID_FIT_MODE_DISABLED: return L"Disabled";
		case DWRITE_GRID_FIT_MODE_ENABLED: return L"Enabled";
		default: return L"Invalid";
	}
}

const wchar_t* xivres::fontgen::directwrite_fixed_size_font::create_struct::get_rendering_mode_string() const {
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

const wchar_t* xivres::fontgen::directwrite_fixed_size_font::create_struct::get_measuring_mode_string() const {
	switch (MeasureMode) {
		case DWRITE_MEASURING_MODE_NATURAL: return L"Natural";
		case DWRITE_MEASURING_MODE_GDI_CLASSIC: return L"GDI Classic";
		case DWRITE_MEASURING_MODE_GDI_NATURAL: return L"GDI Natural";
		default: return L"Invalid";
	}
}

dwrite_font_table::operator bool() const {
	return m_bExists;
}

dwrite_font_table::~dwrite_font_table() {
	if (m_bExists)
		m_pFontFace->ReleaseFontTable(m_pTableContext);
}

dwrite_font_table::dwrite_font_table(IDWriteFontFace* pFace, uint32_t tag)
	: m_pFontFace(pFace, true) {
	success_or_throw(pFace->TryGetFontTable(tag, &m_pData, &m_nSize, &m_pTableContext, &m_bExists));
}

HRESULT __stdcall stream_based_dwrite_font_collection_loader::CreateEnumeratorFromKey(IDWriteFactory* factory, void const* collectionKey, uint32_t collectionKeySize, IDWriteFontFileEnumerator* * pFontFileEnumerator) noexcept {
	if (collectionKeySize != sizeof(std::shared_ptr<xivres::stream>))
		return E_INVALIDARG;


	*pFontFileEnumerator = stream_based_dwrite_font_collection_enumerator::New(IDWriteFactoryPtr(factory, true), *static_cast<const std::shared_ptr<xivres::stream>*>(collectionKey));
	return S_OK;
}

ULONG __stdcall stream_based_dwrite_font_collection_loader::Release() noexcept {
	return 0;
}

ULONG __stdcall stream_based_dwrite_font_collection_loader::AddRef() noexcept {
	return 1;
}

HRESULT __stdcall stream_based_dwrite_font_collection_loader::QueryInterface(REFIID riid, void** ppvObject) noexcept {
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

stream_based_dwrite_font_collection_loader& stream_based_dwrite_font_collection_loader::GetInstance() {
	static stream_based_dwrite_font_collection_loader s_instance;
	return s_instance;
}

HRESULT __stdcall stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator::GetCurrentFontFile(IDWriteFontFile** pFontFile) noexcept {
	if (m_nCurrentFile != 0)
		return E_FAIL;

	return m_factory->CreateCustomFontFileReference(&m_stream, sizeof m_stream, &stream_based_dwrite_font_file_loader::GetInstance(), pFontFile);
}

HRESULT __stdcall stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator::MoveNext(BOOL* pHasCurrentFile) noexcept {
	if (m_nCurrentFile == -1) {
		m_nCurrentFile = 0;
		*pHasCurrentFile = TRUE;
	} else {
		m_nCurrentFile = 1;
		*pHasCurrentFile = FALSE;
	}
	return S_OK;
}

ULONG __stdcall stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator::Release() noexcept {
	const auto newRef = --m_nRef;
	if (!newRef)
		delete this;
	return newRef;
}

ULONG __stdcall stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator::AddRef() noexcept {
	return ++m_nRef;
}

HRESULT __stdcall stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator::QueryInterface(REFIID riid, void** ppvObject) noexcept {
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

stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator* stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator::New(IDWriteFactoryPtr factoryPtr, std::shared_ptr<xivres::stream> pStream) {
	return new stream_based_dwrite_font_collection_enumerator(std::move(factoryPtr), std::move(pStream));
}

stream_based_dwrite_font_collection_loader::stream_based_dwrite_font_collection_enumerator::stream_based_dwrite_font_collection_enumerator(IDWriteFactoryPtr factoryPtr, std::shared_ptr<xivres::stream> pStream)
	: m_factory(std::move(factoryPtr))
	, m_stream(std::move(pStream)) {}

HRESULT __stdcall stream_based_dwrite_font_file_loader::CreateStreamFromKey(void const* fontFileReferenceKey, uint32_t fontFileReferenceKeySize, IDWriteFontFileStream* * pFontFileStream) noexcept {
	if (fontFileReferenceKeySize != sizeof(std::shared_ptr<xivres::stream>))
		return E_INVALIDARG;

	*pFontFileStream = stream_based_dwrite_font_file_stream::New(*static_cast<const std::shared_ptr<xivres::stream>*>(fontFileReferenceKey));
	return S_OK;
}

ULONG __stdcall stream_based_dwrite_font_file_loader::Release() noexcept {
	return 0;
}

ULONG __stdcall stream_based_dwrite_font_file_loader::AddRef() noexcept {
	return 1;
}

HRESULT __stdcall stream_based_dwrite_font_file_loader::QueryInterface(REFIID riid, void** ppvObject) noexcept {
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

stream_based_dwrite_font_file_loader& stream_based_dwrite_font_file_loader::GetInstance() {
	static stream_based_dwrite_font_file_loader s_instance;
	return s_instance;
}

HRESULT __stdcall stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::GetLastWriteTime(uint64_t* pLastWriteTime) noexcept {
	*pLastWriteTime = 0;
	return E_NOTIMPL; // E_NOTIMPL by design -- see method documentation in dwrite.h.
}

HRESULT __stdcall stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::GetFileSize(uint64_t* pFileSize) noexcept {
	*pFileSize = static_cast<uint16_t>(m_stream->size());
	return S_OK;
}

void __stdcall stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::ReleaseFileFragment(void* fragmentContext) noexcept {
	if (fragmentContext)
		delete static_cast<std::vector<uint8_t>*>(fragmentContext);
}

HRESULT __stdcall stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::ReadFileFragment(void const** pFragmentStart, uint64_t fileOffset, uint64_t fragmentSize, void** pFragmentContext) noexcept {
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
		if (fileOffset <= size && fileOffset + fragmentSize <= size && fragmentSize <= (std::numeric_limits<uint32_t>::max)()) {
			auto pVec = new std::vector<uint8_t>();
			try {
				pVec->resize(static_cast<size_t>(fragmentSize));
				if (m_stream->read(static_cast<std::streamoff>(fileOffset), pVec->data(), static_cast<std::streamsize>(fragmentSize)) == static_cast<std::streamsize>(fragmentSize)) {
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

ULONG __stdcall stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::Release() noexcept {
	const auto newRef = --m_nRef;
	if (!newRef)
		delete this;
	return newRef;
}

ULONG __stdcall stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::AddRef() noexcept {
	return ++m_nRef;
}

HRESULT __stdcall stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::QueryInterface(REFIID riid, void** ppvObject) noexcept {
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

stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream* stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::New(std::shared_ptr<xivres::stream> pStream) {
	return new stream_based_dwrite_font_file_stream(std::move(pStream));
}

stream_based_dwrite_font_file_loader::stream_based_dwrite_font_file_stream::stream_based_dwrite_font_file_stream(std::shared_ptr<xivres::stream> pStream)
	: m_stream(std::move(pStream)) {}
