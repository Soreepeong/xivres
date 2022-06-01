﻿#pragma once

#include <ranges>
#include <iostream>

#include "IFixedSizeFont.h"

#include "../Internal/BitmapCopy.h"
#include "../Internal/ThreadPool.h"

#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

#include "../Internal/TeamHypersomnia-rectpack2D/src/finders_interface.h"

#pragma pop_macro("max")
#pragma pop_macro("min")

namespace XivRes::FontGenerator {
	class FontdataPacker {
		size_t m_nThreads = std::thread::hardware_concurrency();
		int m_nSideLength = 4096;
		int m_nDiscardStep = 1;
		std::vector<std::shared_ptr<IFixedSizeFont>> m_sourceFonts;

		struct TargetPlan {
			char32_t Codepoint{};
			const IFixedSizeFont* BaseFont{};
			FontdataGlyphEntry BaseEntry{};
			const Unicode::UnicodeBlocks::BlockDefinition* UnicodeBlock{};
			int CurrentOffsetX{};

			struct TargetGlyph {
				FontdataStream& Font;
				FontdataGlyphEntry Entry;
				size_t SourceFontIndex;

				TargetGlyph(FontdataStream& font, const FontdataGlyphEntry& entry, size_t sourceFontIndex)
					: Font(font)
					, Entry(entry)
					, SourceFontIndex(sourceFontIndex) {}
			};

			std::vector<TargetGlyph> Targets{};
		};
		std::vector<std::shared_ptr<FontdataStream>> m_targetFonts;
		std::vector<std::shared_ptr<MemoryMipmapStream>> m_targetMipmapStreams;
		std::vector<TargetPlan> m_targetPlans;

		std::map<const IFixedSizeFont*, std::vector<std::shared_ptr<IFixedSizeFont>>> m_threadSafeBaseFonts;
		std::vector<std::vector<std::shared_ptr<IFixedSizeFont>>> m_threadSafeSourceFonts;

		uint64_t m_nMaxProgress = 1;
		uint64_t m_nCurrentProgress = 0;
		const char* m_pszProgressString = nullptr;

		bool m_bCancelRequested = false;
		std::thread m_workerThread;
		std::timed_mutex m_runningMtx;
		std::string m_error;

		const IFixedSizeFont& GetThreadSafeBaseFont(const IFixedSizeFont* font, size_t threadIndex) {
			auto& copy = m_threadSafeBaseFonts[font][threadIndex];
			if (!copy)
				copy = m_threadSafeBaseFonts[font][0]->GetThreadSafeView();
			return *copy;
		};

		const IFixedSizeFont& GetThreadSafeSourceFont(size_t fontIndex, size_t threadIndex) {
			auto& copy = m_threadSafeSourceFonts[fontIndex][threadIndex];
			if (!copy)
				copy = m_threadSafeSourceFonts[fontIndex][0]->GetThreadSafeView();
			return *copy;
		};

		void PrepareThreadSafeSourceFonts() {
			m_threadSafeSourceFonts.reserve(m_sourceFonts.size());
			size_t nMaxCharacterCount = 0;
			for (const auto& font : m_sourceFonts) {
				nMaxCharacterCount += font->GetAllCodepoints().size();
				m_threadSafeSourceFonts.emplace_back();
				m_threadSafeSourceFonts.back().resize(m_nThreads);
				m_threadSafeSourceFonts.back()[0] = font;
			}

			m_targetPlans.reserve(nMaxCharacterCount);
		}

		void PrepareTargetFontBasicInfo() {
			m_targetFonts.clear();
			m_targetFonts.reserve(m_sourceFonts.size());
			for (size_t i = 0; i < m_sourceFonts.size(); i++) {
				m_targetFonts.emplace_back(std::make_shared<FontdataStream>());

				auto& targetFont = *m_targetFonts.back();
				const auto& sourceFont = *m_sourceFonts[i];

				targetFont.TextureWidth(m_nSideLength);
				targetFont.TextureHeight(m_nSideLength);
				targetFont.Size(sourceFont.GetSize());
				targetFont.LineHeight(sourceFont.GetLineHeight());
				targetFont.Ascent(sourceFont.GetAscent());
				targetFont.ReserveFontEntries(sourceFont.GetAllCodepoints().size());
				targetFont.ReserveKerningEntries(sourceFont.GetAllKerningPairs().size());
				for (const auto& kerning : sourceFont.GetAllKerningPairs()) {
					if (kerning.second)
						targetFont.AddKerning(kerning.first.first, kerning.first.second, kerning.second);
				}
			}
		}

		void PrepareTargetCodepoints() {
			std::map<const void*, TargetPlan*> rectangleInfoMap;
			for (size_t i = 0; i < m_sourceFonts.size(); i++) {
				const auto& font = m_sourceFonts[i];
				for (const auto& codepoint : font->GetAllCodepoints()) {
					auto& block = Unicode::UnicodeBlocks::GetCorrespondingBlock(codepoint);
					if (block.Flags & Unicode::UnicodeBlocks::RTL)
						continue;
					if (codepoint < 0x20 || codepoint == 0x7F)
						continue;

					const auto uniqid = font->GetBaseFontGlyphUniqid(codepoint);
					auto& pInfo = rectangleInfoMap[uniqid];
					if (!pInfo) {
						m_targetPlans.emplace_back();
						pInfo = &m_targetPlans.back();
						pInfo->BaseFont = font->GetBaseFont(codepoint);
						pInfo->Codepoint = pInfo->BaseFont->UniqidToGlyph(uniqid);
						if (m_threadSafeBaseFonts[pInfo->BaseFont].empty()) {
							m_threadSafeBaseFonts[pInfo->BaseFont].resize(m_nThreads);
							m_threadSafeBaseFonts[pInfo->BaseFont][0] = pInfo->BaseFont->GetThreadSafeView();
						}
						pInfo->UnicodeBlock = &block;
						pInfo->BaseEntry.Char(pInfo->Codepoint);
					}
					pInfo->Targets.emplace_back(*m_targetFonts[i], FontdataGlyphEntry(), i);
					pInfo->Targets.back().Entry.Char(codepoint);
					pInfo->Targets.back().Font.AddFontEntry(codepoint, 0, 0, 0, 0, 0, 0, 0);
				}
			}
		}

		void MeasureGlyphs() {
			Internal::ThreadPool pool(m_nThreads);

			const auto divideUnit = (std::max<size_t>)(1, static_cast<size_t>(std::sqrt(static_cast<double>(m_targetPlans.size()))));
			for (size_t nBase = 0; nBase < divideUnit; nBase++) {
				pool.Submit([this, divideUnit, &pool, nBase](size_t nThreadIndex) {
					for (size_t i = nBase; i < m_targetPlans.size() && !m_bCancelRequested; i += divideUnit) {
						++m_nCurrentProgress;
						pool.AbortIfErrorOccurred();

						auto& info = m_targetPlans[i];
						const auto& baseFont = GetThreadSafeBaseFont(info.BaseFont, nThreadIndex);

						GlyphMetrics gm;
						if (!baseFont.GetGlyphMetrics(info.Codepoint, gm))
							throw std::runtime_error("Base font reported to have a codepoint but it's failing to report glyph metrics");

						const auto baseY1 = gm.Y1;
						info.CurrentOffsetX = (std::min)(0, gm.X1);
						info.BaseEntry.CurrentOffsetY = gm.Y1;
						info.BaseEntry.BoundingHeight = Internal::RangeCheckedCast<uint8_t>(gm.GetHeight());
						info.BaseEntry.BoundingWidth = Internal::RangeCheckedCast<uint8_t>(gm.X2 - info.CurrentOffsetX);
						info.BaseEntry.NextOffsetX = gm.AdvanceX - gm.X2;

						for (auto& target : info.Targets) {
							const auto& sourceFont = GetThreadSafeSourceFont(target.SourceFontIndex, nThreadIndex);
							if (!sourceFont.GetGlyphMetrics(info.Codepoint, gm))
								throw std::runtime_error("Font reported to have a codepoint but it's failing to report glyph metrics");
							if (gm.X1 < 0)
								throw std::runtime_error("Glyphs for target fonts cannot have negative LSB");
							if (gm.GetHeight() != *info.BaseEntry.BoundingHeight)
								throw std::runtime_error("Target font has a glyph with different bounding height from the source");

							target.Entry.CurrentOffsetY = gm.Y1;
							target.Entry.BoundingHeight = Internal::RangeCheckedCast<uint8_t>(gm.GetHeight());
							target.Entry.BoundingWidth = Internal::RangeCheckedCast<uint8_t>(gm.X2 - (std::min)(0, gm.X1));
							target.Entry.NextOffsetX = gm.AdvanceX - gm.X2;

							if (*info.BaseEntry.BoundingWidth < *target.Entry.BoundingWidth) {
								info.CurrentOffsetX -= *target.Entry.BoundingWidth - *info.BaseEntry.BoundingWidth;
								info.BaseEntry.BoundingWidth = *target.Entry.BoundingWidth;
							}
						}
					}
				});
			}

			pool.SubmitDoneAndWait();
		}

		void DrawLayouttedGlyphs(size_t planeIndex, Internal::ThreadPool<>& pool, std::vector<TargetPlan*> successfulPlans) {
			if (m_bCancelRequested)
				return;

			const auto mipmapIndex = planeIndex >> 2;
			const auto channelIndex = FontdataGlyphEntry::ChannelMap[planeIndex % 4];

			while (m_targetMipmapStreams.size() <= mipmapIndex)
				m_targetMipmapStreams.emplace_back(std::make_shared<XivRes::MemoryMipmapStream>(m_nSideLength, m_nSideLength, 1, XivRes::TextureFormat::A8R8G8B8));
			const auto& pStream = m_targetMipmapStreams[mipmapIndex];
			const auto pCurrentTargetBuffer = &pStream->View<uint8_t>()[channelIndex];

			auto pSuccesses = std::make_shared<std::vector<TargetPlan*>>(std::move(successfulPlans));

			const auto divideUnit = (std::max<size_t>)(1, static_cast<size_t>(std::sqrt(static_cast<double>(pSuccesses->size()))));

			for (size_t nBase = 0; nBase < divideUnit; nBase++) {
				pool.Submit([this, planeIndex, divideUnit, pSuccesses, nBase, pCurrentTargetBuffer](size_t nThreadIndex) {
					for (size_t i = nBase; i < pSuccesses->size() && !m_bCancelRequested; i += divideUnit) {
						++m_nCurrentProgress;
						const auto& info = *(*pSuccesses)[i];
						const auto& font = GetThreadSafeBaseFont(info.BaseFont, nThreadIndex);
						font.Draw(
							info.Codepoint,
							pCurrentTargetBuffer,
							4,
							info.BaseEntry.TextureOffsetX - info.CurrentOffsetX,
							info.BaseEntry.TextureOffsetY - info.BaseEntry.CurrentOffsetY,
							m_nSideLength,
							m_nSideLength,
							255, 0, 255, 255
						);
					}
				});
			}
		}

		void LayoutGlyphs() {
			using namespace rectpack2D;
			using spaces_type = rectpack2D::empty_spaces<false, default_empty_spaces>;
			using rect_type = output_rect_t<spaces_type>;

			std::vector<rect_type> pendingRectangles;
			std::vector<TargetPlan*> plansInProgress;
			std::vector<TargetPlan*> plansToTryAgain;
			pendingRectangles.reserve(m_targetPlans.size());
			plansInProgress.reserve(m_targetPlans.size());
			plansToTryAgain.reserve(m_targetPlans.size());

			Internal::ThreadPool pool(m_nThreads);

			for (auto& rectangleInfo : m_targetPlans)
				plansToTryAgain.emplace_back(&rectangleInfo);

			for (size_t planeIndex = 0; !m_bCancelRequested && !plansToTryAgain.empty(); planeIndex++) {
				std::vector<TargetPlan*> successfulPlans;
				successfulPlans.reserve(m_targetPlans.size());

				pendingRectangles.clear();
				plansInProgress.clear();
				for (const auto pInfo : plansToTryAgain) {
					pendingRectangles.emplace_back(0, 0, pInfo->BaseEntry.BoundingWidth + 1, pInfo->BaseEntry.BoundingHeight + 1);
					plansInProgress.emplace_back(pInfo);
				}
				plansToTryAgain.clear();

				const auto onPackedRectangle = [this, planeIndex, &successfulPlans, &pendingRectangles, &plansInProgress](rect_type& r) {
					if (m_bCancelRequested)
						return callback_result::ABORT_PACKING;

					++m_nCurrentProgress;
					const auto index = &r - &pendingRectangles.front();
					auto& info = *plansInProgress[index];

					info.BaseEntry.TextureOffsetX = Internal::RangeCheckedCast<uint16_t>(r.x + 1);
					info.BaseEntry.TextureOffsetY = Internal::RangeCheckedCast<uint16_t>(r.y + 1);
					info.BaseEntry.TextureIndex = Internal::RangeCheckedCast<uint16_t>(planeIndex);

					for (auto& target : info.Targets) {
						target.Entry.TextureOffsetX = Internal::RangeCheckedCast<uint16_t>(r.x + 1 + info.BaseEntry.BoundingWidth - *target.Entry.BoundingWidth);
						target.Entry.TextureOffsetY = static_cast<uint16_t>(r.y + 1);
						target.Entry.TextureIndex = static_cast<uint16_t>(planeIndex);
						target.Font.AddFontEntry(target.Entry);
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

				DrawLayouttedGlyphs(planeIndex, pool, std::move(successfulPlans));
			}

			pool.SubmitDoneAndWait();
		}

	public:
		void SetThreadCount(size_t n = std::thread::hardware_concurrency()) {
			m_nThreads = n;
		}

		void SetDiscardStep(int n) {
			m_nDiscardStep = n;
		}

		void SetSideLength(int n) {
			m_nSideLength = n;
		}

		~FontdataPacker() {
			m_bCancelRequested = true;
			Wait();
			if (m_workerThread.joinable())
				m_workerThread.join();
		}

		size_t AddFont(std::shared_ptr<IFixedSizeFont> font) {
			m_sourceFonts.emplace_back(std::move(font));
			return m_sourceFonts.size() - 1;
		}

		std::shared_ptr<IFixedSizeFont> GetFont(size_t index) const {
			return m_sourceFonts.at(index);
		}

		void Compile() {
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
						PrepareThreadSafeSourceFonts();
						if (m_bCancelRequested) {
							m_pszProgressString = nullptr;
							return;
						}

						m_pszProgressString = "Preparing target fonts...";
						PrepareTargetFontBasicInfo();
						if (m_bCancelRequested) {
							m_pszProgressString = nullptr;
							return;
						}

						m_pszProgressString = "Discovering glyphs...";
						PrepareTargetCodepoints();
						if (m_bCancelRequested) {
							m_pszProgressString = nullptr;
							return;
						}

						m_nMaxProgress = 3 * m_targetPlans.size();
						m_pszProgressString = "Measuring glyphs...";
						MeasureGlyphs();
						if (m_bCancelRequested) {
							m_pszProgressString = nullptr;
							return;
						}

						m_pszProgressString = "Laying out and drawing glyphs...";
						LayoutGlyphs();

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

		std::string GetErrorIfFailed() const {
			return m_error;
		}

		const std::vector<std::shared_ptr<FontdataStream>>& GetTargetFonts() const {
			return m_targetFonts;
		}

		const std::vector<std::shared_ptr<MemoryMipmapStream>>& GetMipmapStreams() const {
			return m_targetMipmapStreams;
		}

		bool IsRunning() const {
			return m_pszProgressString;
		}

		void Wait() {
			void(std::lock_guard(m_runningMtx));
		}

		void RequestCancel() {
			m_bCancelRequested = true;
		}

		template <class _Rep, class _Period>
		[[nodiscard]] bool Wait(const std::chrono::duration<_Rep, _Period>& t) {
			return std::unique_lock(m_runningMtx, std::defer_lock).try_lock_for(t);
		}

		template <class _Clock, class _Duration>
		[[nodiscard]] bool Wait(const std::chrono::time_point<_Clock, _Duration>& t) {
			return std::unique_lock(m_runningMtx, std::defer_lock).try_lock_until(t);
		}

		const char* GetProgressDescription() {
			return m_pszProgressString;
		}

		float GetProgress() const {
			return 1.f * m_nCurrentProgress / m_nMaxProgress;
		}
	};
}
