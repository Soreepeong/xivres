#include "../include/xivres/path_spec.h"

std::string xivres::path_spec::required_prefix(uint32_t categoryId, uint32_t expacId, uint32_t partId) {
	switch (categoryId){
		case 0x00: return "common/";
		case 0x01: return "bgcommon/";
		case 0x02: return expacId > 0 ? std::format("bg/ex{}/{:02x}_", expacId, partId) : "bg/ffxiv/";
		case 0x03: return expacId > 0 ? std::format("cut/ex{}/", expacId) : "cut/ffxiv/";
		case 0x04: return "chara/";
		case 0x05: return "shader/";
		case 0x06: return "ui/";
		case 0x07: return "sound/";
		case 0x08: return "vfx/";
		case 0x09: return "ui_script/";
		case 0x0a: return "exd/";
		case 0x0b: return "game_script/";
		case 0x0c: return "music/";
		case 0x12: return "sqpack_test/";
		case 0x13: return "debug/";
		default: return "invalid/";
	}
}

xivres::path_spec::path_spec(std::string fullPath) {
	std::vector<std::span<char>> parts;
	size_t previousOffset = 0, offset;
	while ((offset = fullPath.find_first_of("/\\", previousOffset)) != std::string::npos) {
		auto part = std::span(fullPath).subspan(previousOffset, offset - previousOffset);
		previousOffset = offset + 1;

		if (part.empty() || (part.size() == 1 && part[0] == '.'))
			void();
		else if (part.size() == 2 && part[0] == '.' && part[1] == '.') {
			if (!parts.empty())
				parts.pop_back();
		} else {
			parts.push_back(part);
		}
	}

	if (auto part = std::span(fullPath).subspan(previousOffset); part.empty() || (part.size() == 1 && part[0] == '.'))
		void();
	else if (part.size() == 2 && part[0] == '.' && part[1] == '.') {
		if (!parts.empty())
			parts.pop_back();
	} else {
		parts.push_back(part);
	}

	if (parts.empty())
		return;

	m_empty = false;
	m_text.reserve(std::accumulate(parts.begin(), parts.end(), SIZE_MAX, [](size_t curr, const std::string_view& view) { return curr + view.size() + 1; }));

	m_pathHash = m_nameHash = 0;
	for (size_t i = 0; i < parts.size(); i++) {
		if (i > 0) {
			m_text += "/";
			if (i == 1)
				m_pathHash = m_nameHash;
			else
				m_pathHash = crc32_combine(crc32_combine(m_pathHash, ~SlashHashValue, 1), m_nameHash, static_cast<long>(parts[i - 1].size()));
		}
		m_text += parts[i];
		for (auto& p : parts[i]) {
			if ('A' <= p && p <= 'Z')
				p += 'a' - 'A';
		}
		m_nameHash = crc32_z(0, reinterpret_cast<const uint8_t*>(parts[i].data()), parts[i].size());
	}

	m_fullPathHash = crc32_combine(crc32_combine(m_pathHash, ~SlashHashValue, 1), m_nameHash, parts.empty() ? 0 : static_cast<long>(parts.back().size()));

	m_fullPathHash = ~m_fullPathHash;
	m_pathHash = ~m_pathHash;
	m_nameHash = ~m_nameHash;

	if (!parts.empty()) {
		std::vector<std::string_view> views;
		views.reserve(parts.size());
		for (const auto& part : parts)
			views.emplace_back(part);

		m_expacId = m_partId = 0;

		if (views[0] == "common") {
			m_categoryId = 0x00;

		} else if (views[0] == "bgcommon") {
			m_categoryId = 0x01;

		} else if (views[0] == "bg") {
			m_categoryId = 0x02;
			m_expacId = views.size() >= 2 && views[1].starts_with("ex") ? static_cast<uint8_t>(std::strtol(&views[1][2], nullptr, 10)) : 0;
			m_partId = views.size() >= 3 && m_expacId > 0 ? static_cast<uint8_t>(std::strtol(&views[2][0], nullptr, 10)) : 0;

		} else if (views[0] == "cut") {
			m_categoryId = 0x03;
			m_expacId = views.size() >= 2 && views[1].starts_with("ex") ? static_cast<uint8_t>(std::strtol(&views[1][2], nullptr, 10)) : 0;

		} else if (views[0] == "chara") {
			m_categoryId = 0x04;

		} else if (views[0] == "shader") {
			m_categoryId = 0x05;

		} else if (views[0] == "ui") {
			m_categoryId = 0x06;

		} else if (views[0] == "sound") {
			m_categoryId = 0x07;

		} else if (views[0] == "vfx") {
			m_categoryId = 0x08;

		} else if (views[0] == "exd") {
			m_categoryId = 0x0a;

		} else if (views[0] == "game_script") {
			m_categoryId = 0x0b;

		} else if (views[0] == "music") {
			m_categoryId = 0x0c;
			m_expacId = views.size() >= 2 && views[1].starts_with("ex") ? static_cast<uint8_t>(std::strtol(&views[1][2], nullptr, 10)) : 0;

		} else
			m_categoryId = 0x00;
	}
}
