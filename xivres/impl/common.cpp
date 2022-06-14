#include "../include/xivres/common.h"

const char* xivres::game_language_code(game_language lang) {
	switch (lang) {
		case game_language::Unspecified: return nullptr;
		case game_language::Japanese: return "ja";
		case game_language::English: return "en";
		case game_language::German: return "de";
		case game_language::French: return "fr";
		case game_language::ChineseSimplified: return "chs";
		case game_language::Korean: return "ko";
		default: throw std::out_of_range("Invalid language");
	}
}
