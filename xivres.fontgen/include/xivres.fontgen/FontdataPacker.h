#pragma once

#include <ranges>
#include <iostream>

#include "IFixedSizeFont.h"

#include "xivres/util.thread_pool.h"

namespace xivres::fontgen {
	class FontdataPacker {
		size_t m_nThreads = std::thread::hardware_concurrency();
		int m_nSideLength = 4096;
		int m_nDiscardStep = 1;
		std::vector<std::shared_ptr<IFixedSizeFont>> m_sourceFonts;

		struct TargetPlan {
			char32_t Codepoint{};
			const IFixedSizeFont* BaseFont{};
			fontdata::glyph_entry BaseEntry{};
			const util::unicode::blocks::block_definition* UnicodeBlock{};
			int CurrentOffsetX{};

			struct TargetGlyph {
				fontdata::stream& Font;
				fontdata::glyph_entry Entry;
				size_t SourceFontIndex;

				TargetGlyph(fontdata::stream& font, const fontdata::glyph_entry& entry, size_t sourceFontIndex);
			};

			std::vector<TargetGlyph> Targets{};
		};
		std::vector<std::shared_ptr<fontdata::stream>> m_targetFonts;
		std::vector<std::shared_ptr<texture::memory_mipmap_stream>> m_targetMipmapStreams;
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

		const IFixedSizeFont& GetThreadSafeBaseFont(const IFixedSizeFont* font, size_t threadIndex);;

		const IFixedSizeFont& GetThreadSafeSourceFont(size_t fontIndex, size_t threadIndex);;

		void PrepareThreadSafeSourceFonts();

		void PrepareTargetFontBasicInfo();

		void PrepareTargetCodepoints();

		void MeasureGlyphs();

		void DrawLayouttedGlyphs(size_t planeIndex, util::thread_pool<>& pool, std::vector<TargetPlan*> successfulPlans);

		void LayoutGlyphs();

	public:
		void SetThreadCount(size_t n = std::thread::hardware_concurrency());

		void SetDiscardStep(int n);

		void SetSideLength(int n);

		~FontdataPacker();

		size_t AddFont(std::shared_ptr<IFixedSizeFont> font);

		std::shared_ptr<IFixedSizeFont> GetFont(size_t index) const;

		void Compile();

		std::string GetErrorIfFailed() const;

		const std::vector<std::shared_ptr<fontdata::stream>>& GetTargetFonts() const;

		const std::vector<std::shared_ptr<texture::memory_mipmap_stream>>& GetMipmapStreams() const;

		bool IsRunning() const;

		void RequestCancel();

		void Wait() {
			void(std::lock_guard(m_runningMtx));
		}

		template <class _Rep, class _Period>
		[[nodiscard]] bool Wait(const std::chrono::duration<_Rep, _Period>& t) {
			return std::unique_lock(m_runningMtx, std::defer_lock).try_lock_for(t);
		}

		template <class _Clock, class _Duration>
		[[nodiscard]] bool Wait(const std::chrono::time_point<_Clock, _Duration>& t) {
			return std::unique_lock(m_runningMtx, std::defer_lock).try_lock_until(t);
		}

		const char* GetProgressDescription();

		float GetProgress() const;
	};
}
