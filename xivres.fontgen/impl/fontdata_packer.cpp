#include "../include/xivres.fontgen/fontdata_packer.h"

#include "xivres/util.bitmap_copy.h"

#ifdef min
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include "../include/xivres.fontgen/TeamHypersomnia-rectpack2D/src/finders_interface.h"
#pragma pop_macro("max")
#pragma pop_macro("min")
#else
#include "../include/xivres.fontgen/TeamHypersomnia-rectpack2D/src/finders_interface.h"
#endif

xivres::fontgen::fontdata_packer::target_plan::target_glyph::target_glyph(fontdata::stream& font, const fontdata::glyph_entry& entry, size_t sourceFontIndex)
	: Font(font)
	, Entry(entry)
	, SourceFontIndex(sourceFontIndex) {
}

float xivres::fontgen::fontdata_packer::progress_scaled() const {
	return 1.f * static_cast<float>(m_nCurrentProgress) / static_cast<float>(m_nMaxProgress);
}

const char* xivres::fontgen::fontdata_packer::progress_description() const {
	return m_pszProgressString;
}

bool xivres::fontgen::fontdata_packer::is_running() const {
	return m_pszProgressString;
}

const std::vector<std::shared_ptr<xivres::texture::memory_mipmap_stream>>& xivres::fontgen::fontdata_packer::compiled_mipmap_streams() const {
	return m_targetMipmapStreams;
}

const std::vector<std::shared_ptr<xivres::fontdata::stream>>& xivres::fontgen::fontdata_packer::compiled_fontdatas() const {
	return m_targetFonts;
}

std::string xivres::fontgen::fontdata_packer::get_error_if_failed() const {
	return m_error;
}

void xivres::fontgen::fontdata_packer::compile() {
	if (m_pszProgressString)
		throw std::runtime_error("Compile already in progress");

	m_nMaxProgress = 1;
	m_nCurrentProgress = 0;
	m_bCancelRequested = false;

	std::mutex startMtx;
	auto startLock = std::unique_lock(startMtx);
	std::condition_variable cv;
	m_workerThread = std::thread([this, &cv]() {
		{
			const auto lock = std::lock_guard(m_runningMtx);
			cv.notify_all();
			try {
				m_pszProgressString = "Preparing source fonts...";
				prepare_threadsafe_source_fonts();
				if (m_bCancelRequested) {
					m_pszProgressString = nullptr;
					return;
				}

				m_pszProgressString = "Preparing target fonts...";
				prepare_target_font_basic_info();
				if (m_bCancelRequested) {
					m_pszProgressString = nullptr;
					return;
				}

				m_pszProgressString = "Discovering glyphs...";
				prepare_target_codepoints();
				if (m_bCancelRequested) {
					m_pszProgressString = nullptr;
					return;
				}

				m_nMaxProgress = 3 * m_targetPlans.size();
				m_pszProgressString = "Measuring glyphs...";
				measure_glyphs();
				if (m_bCancelRequested) {
					m_pszProgressString = nullptr;
					return;
				}

				m_pszProgressString = "Laying out and drawing glyphs...";
				layout_glyphs();

				m_error.clear();
			} catch (const std::exception& e) {
				m_error = e.what();
			}
			m_pszProgressString = nullptr;
		}
		m_workerThread.detach();
	});
	cv.wait(startLock);
}

std::shared_ptr<xivres::fontgen::fixed_size_font> xivres::fontgen::fontdata_packer::get_font(size_t index) const {
	return m_sourceFonts.at(index);
}

size_t xivres::fontgen::fontdata_packer::add_font(std::shared_ptr<fixed_size_font> font) {
	m_sourceFonts.emplace_back(std::move(font));
	return m_sourceFonts.size() - 1;
}

xivres::fontgen::fontdata_packer::~fontdata_packer() {
	m_bCancelRequested = true;
	wait();
	if (m_workerThread.joinable())
		m_workerThread.join();
}

void xivres::fontgen::fontdata_packer::set_side_length(int n) {
	if (n > 4096)
		throw std::out_of_range("Side length can be up to 4096");
	m_nSideLength = n;
}

void xivres::fontgen::fontdata_packer::set_discard_step(int n) {
	m_nDiscardStep = n;
}

void xivres::fontgen::fontdata_packer::set_thread_count(size_t n) {
	m_nThreads = n;
}

void xivres::fontgen::fontdata_packer::layout_glyphs() {
	using namespace rectpack2D;
	using spaces_type = empty_spaces<false, default_empty_spaces>;
	using rect_type = output_rect_t<spaces_type>;

	std::vector<rect_type> pendingRectangles;
	std::vector<target_plan*> plansInProgress;
	std::vector<target_plan*> plansToTryAgain;
	pendingRectangles.reserve(m_targetPlans.size());
	plansInProgress.reserve(m_targetPlans.size());
	plansToTryAgain.reserve(m_targetPlans.size());

	util::thread_pool::task_waiter waiter;

	for (auto& rectangleInfo : m_targetPlans)
		plansToTryAgain.emplace_back(&rectangleInfo);

	for (size_t planeIndex = 0; !m_bCancelRequested && !plansToTryAgain.empty(); planeIndex++) {
		std::vector<target_plan*> successfulPlans;
		successfulPlans.reserve(m_targetPlans.size());

		pendingRectangles.clear();
		plansInProgress.clear();
		for (const auto pInfo : plansToTryAgain) {
			pendingRectangles.emplace_back(
				0,
				0,
				pInfo->BaseEntry.BoundingWidth + 1,
				pInfo->BaseEntry.BoundingHeight + 1 + pInfo->PadUp + pInfo->PadDown);
			plansInProgress.emplace_back(pInfo);
		}
		plansToTryAgain.clear();

		const auto onPackedRectangle = [this, planeIndex, &successfulPlans, &pendingRectangles, &plansInProgress](rect_type& r) {
			if (m_bCancelRequested)
				return callback_result::ABORT_PACKING;

			++m_nCurrentProgress;
			const auto index = &r - &pendingRectangles.front();
			auto& info = *plansInProgress[index];

			info.BaseEntry.TextureOffsetX = util::range_check_cast<uint16_t>(r.x + 1);
			info.BaseEntry.TextureOffsetY = util::range_check_cast<uint16_t>(r.y + 1);
			info.BaseEntry.TextureIndex = util::range_check_cast<uint16_t>(planeIndex);

			for (auto& target : info.Targets) {
				target.Entry.TextureOffsetX = util::range_check_cast<uint16_t>(r.x + 1 + info.BaseEntry.BoundingWidth - *target.Entry.BoundingWidth);
				target.Entry.TextureOffsetY = static_cast<uint16_t>(r.y + 1 + info.PadUp - target.Entry.TextureOffsetY);
				target.Entry.TextureIndex = static_cast<uint16_t>(planeIndex);
				target.Font.add_glyph(target.Entry);
			}

			successfulPlans.emplace_back(&info);

			return callback_result::CONTINUE_PACKING;
		};

		const auto onFailedRectangle = [this, &plansToTryAgain, &pendingRectangles, &plansInProgress](rect_type& r) {
			if (m_bCancelRequested)
				return callback_result::ABORT_PACKING;

			plansToTryAgain.emplace_back(plansInProgress[&r - &pendingRectangles.front()]);
			return callback_result::CONTINUE_PACKING;
		};

		if (planeIndex == 0) {
			find_best_packing<spaces_type>(
				pendingRectangles,
				make_finder_input(
					m_nSideLength - 1,
					m_nDiscardStep,
					onPackedRectangle,
					onFailedRectangle,
					flipping_option::DISABLED
				)
				);

		} else {
			// Already sorted from above
			find_best_packing_dont_sort<spaces_type>(
				pendingRectangles,
				make_finder_input(
					m_nSideLength - 1,
					m_nDiscardStep,
					onPackedRectangle,
					onFailedRectangle,
					flipping_option::DISABLED
				)
				);
		}

		if (successfulPlans.empty())
			throw std::runtime_error("Failed to pack some characters");

		draw_layoutted_glyphs(waiter, planeIndex, std::move(successfulPlans));
	}

	waiter.wait_all();
}

void xivres::fontgen::fontdata_packer::draw_layoutted_glyphs(util::thread_pool::task_waiter<>& waiter, size_t planeIndex, std::vector<target_plan*> successfulPlans) {
	if (m_bCancelRequested)
		return;

	const auto mipmapIndex = planeIndex >> 2;
	const auto channelIndex = fontdata::glyph_entry::ChannelMap[planeIndex % 4];

	while (m_targetMipmapStreams.size() <= mipmapIndex)
		m_targetMipmapStreams.emplace_back(std::make_shared<texture::memory_mipmap_stream>(m_nSideLength, m_nSideLength, 1, texture::formats::B8G8R8A8));
	const auto& pStream = m_targetMipmapStreams[mipmapIndex];
	const auto pCurrentTargetBuffer = &pStream->as_span<uint8_t>()[channelIndex];

	auto pSuccesses = std::make_shared<std::vector<target_plan*>>(std::move(successfulPlans));

	const auto divideUnit = (std::max<size_t>)(1, static_cast<size_t>(std::sqrt(static_cast<double>(pSuccesses->size()))));

	for (size_t nBase = 0; nBase < divideUnit; nBase++) {
		waiter.submit([this, divideUnit, pSuccesses, nBase, pCurrentTargetBuffer](auto& task) {
			for (size_t i = nBase; i < pSuccesses->size() && !m_bCancelRequested; i += divideUnit) {
				task.throw_if_cancelled();
				++m_nCurrentProgress;
				const auto& info = *(*pSuccesses)[i];

				auto pooledBaseFont = *m_threadSafeBaseFonts[info.BaseFont];
				if (!pooledBaseFont)
					pooledBaseFont.emplace(m_baseFonts[info.BaseFont]->get_threadsafe_view());
				const auto& baseFont = **pooledBaseFont;

				baseFont.draw(
					info.Codepoint,
					pCurrentTargetBuffer,
					4,
					info.BaseEntry.TextureOffsetX - info.CurrentOffsetX,
					info.BaseEntry.TextureOffsetY - info.BaseEntry.CurrentOffsetY + info.PadUp,
					m_nSideLength,
					m_nSideLength,
					255, 0, 255, 255
				);
			}
		});
	}
}

void xivres::fontgen::fontdata_packer::measure_glyphs() {
	util::thread_pool::task_waiter waiter;

	const auto divideUnit = (std::max<size_t>)(1, static_cast<size_t>(std::sqrt(static_cast<double>(m_targetPlans.size()))));
	for (size_t nBase = 0; nBase < divideUnit; nBase++) {
		waiter.submit([this, divideUnit, nBase](auto& task) {
			for (size_t i = nBase; i < m_targetPlans.size() && !m_bCancelRequested; i += divideUnit) {
				++m_nCurrentProgress;
				task.throw_if_cancelled();

				auto& info = m_targetPlans[i];

				auto pooledBaseFont = *m_threadSafeBaseFonts[info.BaseFont];
				if (!pooledBaseFont)
					pooledBaseFont.emplace(m_baseFonts[info.BaseFont]->get_threadsafe_view());
				const auto& baseFont = **pooledBaseFont;

				glyph_metrics gm;
				if (!baseFont.try_get_glyph_metrics(info.Codepoint, gm))
					throw std::runtime_error("Base font reported to have a codepoint but it's failing to report glyph metrics");

				info.CurrentOffsetX = util::range_check_cast<int16_t>((std::min<int>)(0, gm.X1));
				info.BaseEntry.BoundingWidth = util::range_check_cast<uint8_t>(gm.X2 - info.CurrentOffsetX);

				info.PadUp = info.PadDown = 0;
				info.BaseEntry.CurrentOffsetY = util::range_check_cast<int8_t>(gm.Y1);
				info.BaseEntry.BoundingHeight = util::range_check_cast<uint8_t>(gm.height());

				for (auto& target : info.Targets) {
					auto pooledSourceFont = **m_threadSafeSourceFonts[target.SourceFontIndex];
					if (!pooledSourceFont)
						pooledSourceFont.emplace(m_sourceFonts[target.SourceFontIndex]->get_threadsafe_view());

					auto& sourceFont = **pooledSourceFont;

					if (!sourceFont.try_get_glyph_metrics(info.Codepoint, gm))
						throw std::runtime_error("Font reported to have a codepoint but it's failing to report glyph metrics");
					if (gm.X1 < 0)
						throw std::runtime_error("Glyphs for target fonts cannot have negative LSB");
					if (gm.height() != *info.BaseEntry.BoundingHeight)
						throw std::runtime_error("Target font has a glyph with different bounding height from the source");

					if (gm.Y1 > 0)
						target.Entry.TextureOffsetY = util::range_check_cast<uint16_t>(gm.Y1);
					else
						target.Entry.CurrentOffsetY = util::range_check_cast<int8_t>(gm.Y1);
					target.Entry.BoundingHeight = util::range_check_cast<uint8_t>((std::max<uint32_t>)(gm.Y2, target.Font.line_height()) - (std::min)(0, gm.Y1));
					target.Entry.BoundingWidth = util::range_check_cast<uint8_t>(gm.X2 - (std::min)(0, gm.X1));
					target.Entry.NextOffsetX = util::range_check_cast<int8_t>(gm.AdvanceX - target.Entry.BoundingWidth);

					if (*info.BaseEntry.BoundingWidth < *target.Entry.BoundingWidth) {
						info.CurrentOffsetX = util::range_check_cast<int16_t>(info.CurrentOffsetX - *target.Entry.BoundingWidth + *info.BaseEntry.BoundingWidth);
						info.BaseEntry.BoundingWidth = *target.Entry.BoundingWidth;
					}

					if (gm.Y1 > info.PadUp)
						info.PadUp = util::range_check_cast<int8_t>(gm.Y1);
					if (info.PadDown + info.PadUp + *info.BaseEntry.BoundingHeight < target.Entry.BoundingHeight)
						info.PadDown = util::range_check_cast<int8_t>(target.Entry.BoundingHeight - info.PadUp - *info.BaseEntry.BoundingHeight);
				}
			}
		});
	}

	waiter.wait_all();
}

void xivres::fontgen::fontdata_packer::prepare_target_codepoints() {
	std::map<const void*, target_plan*> rectangleInfoMap;
	for (size_t i = 0; i < m_sourceFonts.size(); i++) {
		const auto& font = m_sourceFonts[i];
		for (const auto& codepoint : font->all_codepoints()) {
			auto& block = util::unicode::blocks::block_for(codepoint);
			if (block.Purpose & util::unicode::blocks::RTL)
				continue;
			if (codepoint < 0x20 || codepoint == 0x7F)
				continue;

			const auto uniqid = font->get_base_font_glyph_uniqid(codepoint);
			auto& pInfo = rectangleInfoMap[uniqid];
			if (!pInfo) {
				m_targetPlans.emplace_back();
				pInfo = &m_targetPlans.back();
				pInfo->BaseFont = font->get_base_font(codepoint);
				pInfo->Codepoint = pInfo->BaseFont->uniqid_to_glyph(uniqid);
				if (!m_baseFonts[pInfo->BaseFont])
					m_baseFonts[pInfo->BaseFont] = pInfo->BaseFont->get_threadsafe_view();
				pInfo->UnicodeBlock = &block;
				pInfo->BaseEntry.codepoint(pInfo->Codepoint);
			}
			pInfo->Targets.emplace_back(*m_targetFonts[i], fontdata::glyph_entry(), i);
			pInfo->Targets.back().Entry.codepoint(codepoint);
			pInfo->Targets.back().Font.add_glyph(codepoint, 0, 0, 0, 0, 0, 0, 0);
		}
	}
}

void xivres::fontgen::fontdata_packer::prepare_target_font_basic_info() {
	m_targetFonts.clear();
	m_targetFonts.reserve(m_sourceFonts.size());
	for (auto& pSourceFont : m_sourceFonts) {
		m_targetFonts.emplace_back(std::make_shared<fontdata::stream>());

		auto& targetFont = *m_targetFonts.back();
		const auto& sourceFont = *pSourceFont;

		targetFont.texture_width(static_cast<uint16_t>(m_nSideLength));
		targetFont.texture_height(static_cast<uint16_t>(m_nSideLength));
		targetFont.font_size(sourceFont.font_size());
		targetFont.line_height(sourceFont.line_height());
		targetFont.ascent(sourceFont.ascent());
		targetFont.reserve_glyphs(sourceFont.all_codepoints().size());

		const auto& kerningPairs = sourceFont.all_kerning_pairs();
		targetFont.reserve_kernings(kerningPairs.size());
		for (const auto& kp : kerningPairs)
			targetFont.add_kerning(kp.first.first, kp.first.second, kp.second);
	}
}

void xivres::fontgen::fontdata_packer::prepare_threadsafe_source_fonts() {
	m_threadSafeSourceFonts.reserve(m_sourceFonts.size());
	size_t nMaxCharacterCount = 0;
	for (const auto& font : m_sourceFonts) {
		nMaxCharacterCount += font->all_codepoints().size();
		m_threadSafeSourceFonts.emplace_back(std::make_unique<util::thread_pool::object_pool<std::shared_ptr<fixed_size_font>>>());
	}

	m_targetPlans.reserve(nMaxCharacterCount);
}

void xivres::fontgen::fontdata_packer::request_cancel() {
	m_bCancelRequested = true;
}
