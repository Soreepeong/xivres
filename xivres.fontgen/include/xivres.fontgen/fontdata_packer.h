#pragma once

#include "fixed_size_font.h"

#include "xivres/util.thread_pool.h"

namespace xivres::fontgen {
	class fontdata_packer {
		size_t m_nThreads = std::thread::hardware_concurrency();
		int m_nSideLength = 4096;
		int m_nDiscardStep = 1;
		std::vector<std::shared_ptr<fixed_size_font>> m_sourceFonts;

		struct target_plan {
			char32_t Codepoint{};
			const fixed_size_font* BaseFont{};
			fontdata::glyph_entry BaseEntry{};
			const util::unicode::blocks::block_definition* UnicodeBlock{};
			int CurrentOffsetX{};

			struct target_glyph {
				fontdata::stream& Font;
				fontdata::glyph_entry Entry;
				size_t SourceFontIndex;

				target_glyph(fontdata::stream& font, const fontdata::glyph_entry& entry, size_t sourceFontIndex);
			};

			std::vector<target_glyph> Targets{};
		};
		std::vector<std::shared_ptr<fontdata::stream>> m_targetFonts;
		std::vector<std::shared_ptr<texture::memory_mipmap_stream>> m_targetMipmapStreams;
		std::vector<target_plan> m_targetPlans;

		std::map<const fixed_size_font*, std::vector<std::shared_ptr<fixed_size_font>>> m_threadSafeBaseFonts;
		std::vector<std::vector<std::shared_ptr<fixed_size_font>>> m_threadSafeSourceFonts;

		uint64_t m_nMaxProgress = 1;
		uint64_t m_nCurrentProgress = 0;
		const char* m_pszProgressString = nullptr;

		bool m_bCancelRequested = false;
		std::thread m_workerThread;
		std::timed_mutex m_runningMtx;
		std::string m_error;

		const fixed_size_font& get_threadsafe_base_font(const fixed_size_font* font, size_t threadIndex);

		const fixed_size_font& get_threadsafe_source_font(size_t fontIndex, size_t threadIndex);
		
		void prepare_threadsafe_source_fonts();

		void prepare_target_font_basic_info();

		void prepare_target_codepoints();

		void measure_glyphs();

		void draw_layoutted_glyphs(size_t planeIndex, util::thread_pool<>& pool, std::vector<target_plan*> successfulPlans);

		void layout_glyphs();

	public:
		fontdata_packer() = default;
		fontdata_packer(fontdata_packer&&) = delete;
		fontdata_packer(const fontdata_packer&) = delete;
		fontdata_packer& operator=(fontdata_packer&&) = delete;
		fontdata_packer& operator=(const fontdata_packer&) = delete;
		~fontdata_packer();
		
		void set_thread_count(size_t n = std::thread::hardware_concurrency());

		void set_discard_step(int n);

		void set_side_length(int n);

		size_t add_font(std::shared_ptr<fixed_size_font> font);

		[[nodiscard]] std::shared_ptr<fixed_size_font> get_font(size_t index) const;

		void compile();

		[[nodiscard]] std::string get_error_if_failed() const;

		[[nodiscard]] const std::vector<std::shared_ptr<fontdata::stream>>& compiled_fontdatas() const;

		[[nodiscard]] const std::vector<std::shared_ptr<texture::memory_mipmap_stream>>& compiled_mipmap_streams() const;

		[[nodiscard]] bool is_running() const;

		void request_cancel();

		void wait() {
			void(std::lock_guard(m_runningMtx));
		}

		template <class TRep, class TPeriod>
		[[nodiscard]] bool wait(const std::chrono::duration<TRep, TPeriod>& t) {
			return std::unique_lock(m_runningMtx, std::defer_lock).try_lock_for(t);
		}

		template <class TClock, class TDuration>
		[[nodiscard]] bool wait(const std::chrono::time_point<TClock, TDuration>& t) {
			return std::unique_lock(m_runningMtx, std::defer_lock).try_lock_until(t);
		}

		[[nodiscard]] const char* progress_description() const;

		[[nodiscard]] float progress_scaled() const;
	};
}
