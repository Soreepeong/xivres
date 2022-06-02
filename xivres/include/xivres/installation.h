#pragma once

#include <filesystem>

#include "sqpack.reader.h"

namespace xivres::excel {
	class reader;
}

namespace xivres::fontgen {
	class fontdata_fixed_size_font;
	class game_fontdata_set;
	struct game_fontdata_definition;
}

namespace xivres {
	enum class font_type {
		undefined,
		font,
		font_lobby,
		chn_axis,
		krn_axis,
	};

	class installation {
		const std::filesystem::path m_gamePath;
		mutable std::map<uint32_t, std::optional<sqpack::reader>> m_readers;
		mutable std::mutex m_populateMtx;

	public:
		installation(std::filesystem::path gamePath);

		[[nodiscard]] std::shared_ptr<packed_stream> get_file_packed(const path_spec& pathSpec) const;

		[[nodiscard]] std::shared_ptr<unpacked_stream> get_file(const path_spec& pathSpec, std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;

		[[nodiscard]] const std::vector<uint32_t> get_sqpack_ids() const;

		[[nodiscard]] const sqpack::reader& get_sqpack(const path_spec& rawpath_spec) const;

		[[nodiscard]] const sqpack::reader& get_sqpack(uint32_t packId) const;

		[[nodiscard]] const sqpack::reader& get_sqpack(uint8_t categoryId, uint8_t expacId, uint8_t partId) const;

		[[nodiscard]] excel::reader get_excel(const std::string& name) const;

		fontgen::game_fontdata_set get_fontdata_set(xivres::font_type gameFontType, std::span<const fontgen::game_fontdata_definition> gameFontdataDefinitions, const char* pcszTexturePathPattern) const;

		fontgen::game_fontdata_set get_fontdata_set(font_type fontType = font_type::font) const;

		void preload_all_sqpacks() const;
	};
}
