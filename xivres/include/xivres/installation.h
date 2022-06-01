#pragma once

#include <filesystem>

#include "SqpackReader.h"

namespace xivres::excel {
	class reader;
}

namespace xivres::fontgen {
	class GameFontdataFixedSizeFont;
	class GameFontdataSet;
	struct GameFontdataDefinition;
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
		mutable std::map<uint32_t, std::optional<SqpackReader>> m_readers;
		mutable std::mutex m_populateMtx;

	public:
		installation(std::filesystem::path gamePath);

		[[nodiscard]] std::shared_ptr<packed_stream> get_file_packed(const xiv_path_spec& pathSpec) const;

		[[nodiscard]] std::shared_ptr<PackedFileUnpackingStream> get_file(const xiv_path_spec& pathSpec, std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;

		[[nodiscard]] const SqpackReader& get_sqpack(const xiv_path_spec& rawpath_spec) const;

		[[nodiscard]] const SqpackReader& get_sqpack(uint32_t packId) const;

		[[nodiscard]] const SqpackReader& get_sqpack(uint8_t categoryId, uint8_t expacId, uint8_t partId) const;

		[[nodiscard]] excel::reader get_excel(const std::string& name) const;

		fontgen::GameFontdataSet get_fontdata_set(xivres::font_type gameFontType, std::span<const fontgen::GameFontdataDefinition> gameFontdataDefinitions, const char* pcszTexturePathPattern) const;

		fontgen::GameFontdataSet get_fontdata_set(font_type fontType = font_type::font) const;

		void preload_all_sqpacks() const;
	};
}
