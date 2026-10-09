#include "../include/xivres/game_layout.h"

#include <algorithm>
#include <execution>
#include <format>
#include <ranges>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "../include/xivres/pe_image.h"
#include "../include/xivres/util.byte_regex.h"

namespace {
	struct signature {
		std::string Name;
		std::string Resolve;
		std::string Pattern;
		std::vector<std::pair<std::string, std::string>> Captures;  // name and type, in group order
	};

	// Finds the one match of a signature in the image's .text; throws if there is none, or more than one.
	xivres::game_layout::match find_one(const signature& s, const xivres::pe_image& image, uint64_t imageBase) {
		const auto text = image.section_data(".text");
		const auto addressOf = [&](const char* p) { return imageBase + image.rva_of(reinterpret_cast<const uint8_t*>(p)); };

		// Matches may overlap; the next search starts a byte after the first match's start.
		const auto found = xivres::util::byte_regex::find_unique(xivres::util::byte_regex::compile(s.Pattern), text, xivres::util::byte_regex::resume::next_byte);
		if (found.Count == 0)
			throw std::runtime_error(std::format("Signature {} wasn't found; the game's code changed.", s.Name));
		if (found.Count > 1)
			throw std::runtime_error(std::format("Signature {} matched more than once ({:X}, {:X}).", s.Name, addressOf(found.First[0].first), addressOf(found.Second[0].first)));
		const auto& first = found.First;

		xivres::game_layout::match res{s.Name, addressOf(first[0].first)};
		for (size_t i = 0; i < s.Captures.size(); i++) {
			const auto& [name, type] = s.Captures[i];
			res.Types.emplace(name, type);

			// A name given to several groups, in alternatives of the pattern, is what the one that matched captured.
			const auto& g = first[i + 1];
			if (!g.matched || res.Captures.contains(name))
				continue;
			std::optional<int32_t> value;
			const auto b = reinterpret_cast<const uint8_t*>(g.first);
			switch (g.length()) {
				case 1: value = static_cast<int8_t>(b[0]); break;
				case 2: value = static_cast<int16_t>(b[0] | (b[1] << 8)); break;
				case 4: value = static_cast<int32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<uint32_t>(b[3]) << 24)); break;
			}
			res.Captures.emplace(name, xivres::game_layout::capture{addressOf(g.first), value});
		}

		// What the signature resolves to: the match's start, the start of the function containing it, or its capture of
		// type position.
		if (s.Resolve == "function") {
			const auto rva = static_cast<uint32_t>(res.Address - imageBase);
			const auto function = image.function_containing(rva);
			if (!function)
				throw std::runtime_error(std::format("RVA {:X} is in no function.", rva));
			res.Address = imageBase + function->BeginAddress;
		} else if (s.Resolve == "capture") {
			res.Address = 0;
			for (const auto& [name, type] : s.Captures) {
				if (type == "position" && res.Captures.contains(name))
					res.Address = res.Captures.at(name).Address;
			}
		} else if (s.Resolve == "none") {
			res.Address = 0;
		}
		return res;
	}
}

uint64_t xivres::game_layout::match::target(const std::string& rel32) const {
	const auto& c = Captures.at(rel32);
	return c.Address + 4 + c.Value.value();
}

xivres::game_layout::game_layout(std::string_view json, std::span<const uint8_t> exe, uint64_t imageBase)
	: game_layout(json, pe_image::from_file(exe), imageBase) {
}

xivres::game_layout::game_layout(std::string_view json, const pe_image& image, uint64_t imageBase) {
	const auto data = nlohmann::json::parse(json, nullptr, true, true);

	std::vector<signature> signatures;
	for (const auto& [name, s] : data.at("signatures").items()) {
		auto& sig = signatures.emplace_back(name, s.value("resolve", "match"));
		if (const auto& pattern = s.at("pattern"); pattern.is_array()) {
			for (const auto& part : pattern)
				sig.Pattern += part.get<std::string>();
		} else
			sig.Pattern = pattern.get<std::string>();
		if (s.contains("captures")) {
			for (const auto& c : s.at("captures"))
				sig.Captures.emplace_back(c.at("name").get<std::string>(), c.at("type").get<std::string>());
		}
	}

	std::vector<std::optional<match>> found(signatures.size());
	std::vector<std::string> errors(signatures.size());
	std::vector<size_t> indices(signatures.size());
	for (size_t i = 0; i < indices.size(); i++)
		indices[i] = i;
	std::for_each(std::execution::par, indices.begin(), indices.end(), [&](size_t i) {
		try {
			found[i] = find_one(signatures[i], image, imageBase);
		} catch (const std::exception& e) {
			errors[i] = e.what();
		}
	});

	const auto addConflict = [&](const std::string& name, std::string message) {
		m_conflicts.try_emplace(name, std::move(message));
	};

	for (size_t i = 0; i < signatures.size(); i++) {
		if (!found[i]) {
			m_failures.emplace(signatures[i].Name, errors[i]);
			continue;
		}
		m_matches.emplace(signatures[i].Name, std::move(*found[i]));
	}

	for (const auto& [name, m] : m_matches) {
		for (const auto& [captureName, type] : m.Types) {
			if ((type != "offset" && type != "size" && type != "count") || !m.Captures.contains(captureName))
				continue;
			const auto value = m.Captures.at(captureName).Value.value();
			if (const auto it = m_values.find(captureName); it == m_values.end())
				m_values.emplace(captureName, std::make_pair(value, name));
			else if (it->second.first != value)
				addConflict(captureName, std::format("{} is 0x{:X} by {} but 0x{:X} by {}", captureName, it->second.first, it->second.second, value, name));
		}
	}

	for (const auto& [name, constant] : data.at("constants").items()) {
		const auto& value = constant.at("value");
		if (value.is_string())
			m_strings.emplace(name, value.get<std::string>());
		else if (value.is_array())
			m_lists.emplace(name, value.get<std::vector<int32_t>>());
		else if (const auto it = m_values.find(name); it == m_values.end())
			m_values.emplace(name, std::make_pair(value.get<int32_t>(), "the constants"));
		else if (it->second.first != value.get<int32_t>())
			addConflict(name, std::format("{} is 0x{:X} by {} but 0x{:X} by the constants", name, it->second.first, it->second.second, value.get<int32_t>()));
	}

	// A field captured relative to another ("Structure.Field-Structure.Other") is that field where the other is known,
	// and must agree with what captured the field itself.
	for (const auto& [capture, valueAndSource] : std::map(m_values)) {
		const auto& [difference, source] = valueAndSource;
		const auto dash = capture.find('-');
		if (dash == std::string::npos || m_conflicts.contains(capture))
			continue;
		const auto field = capture.substr(0, dash), other = capture.substr(dash + 1);
		const auto basis = m_values.find(other);
		if (m_conflicts.contains(other) || basis == m_values.end())
			continue;
		const auto value = basis->second.first + difference;
		if (const auto it = m_values.find(field); it == m_values.end())
			m_values.emplace(field, std::make_pair(value, std::format("{} (relative to {})", source, other)));
		else if (it->second.first != value)
			addConflict(field, std::format("{} is 0x{:X} by {} but 0x{:X} by {} (relative to {})", field, it->second.first, it->second.second, value, source, other));
	}
}

void xivres::game_layout::resolve(std::string_view part, const std::function<void()>& fn) {
	m_problems.clear();
	fn();
	if (m_problems.empty())
		return;

	std::string message = std::format("{} can't work with this version of the game: ", part);
	std::ranges::sort(m_problems);
	m_problems.erase(std::ranges::unique(m_problems).begin(), m_problems.end());
	for (size_t i = 0; i < m_problems.size(); i++)
		message += (i ? "; " : "") + m_problems[i];
	if (m_failures.empty()) {
		message += ". Every signature matched.";
	} else {
		message += ". Signatures not found:";
		for (const auto& name : m_failures | std::views::keys)
			message += " " + name;
		message += ".";
	}
	m_problems.clear();
	throw std::runtime_error(message);
}

int32_t xivres::game_layout::get(const std::string& name) {
	if (const auto it = m_conflicts.find(name); it != m_conflicts.end())
		m_problems.push_back(it->second);
	else if (const auto it2 = m_values.find(name); it2 != m_values.end())
		return it2->second.first;
	else
		m_problems.push_back(std::format("no signature found says where {} is", name));
	return -1;
}

std::optional<int32_t> xivres::game_layout::try_get(const std::string& name) const {
	if (m_conflicts.contains(name))
		return std::nullopt;
	if (const auto it = m_values.find(name); it != m_values.end())
		return it->second.first;
	return std::nullopt;
}

std::string xivres::game_layout::get_string(const std::string& name) {
	if (const auto it = m_strings.find(name); it != m_strings.end())
		return it->second;
	m_problems.push_back(std::format("there is no constant {}", name));
	return {};
}

std::vector<int32_t> xivres::game_layout::get_list(const std::string& name) {
	if (const auto it = m_lists.find(name); it != m_lists.end())
		return it->second;
	m_problems.push_back(std::format("there is no constant {}", name));
	return {};
}

uint64_t xivres::game_layout::address(const std::string& signature, const std::string& rel32) {
	const auto m = find(signature);
	if (!m)
		return 0;
	if (rel32.empty())
		return m->Address;
	if (!m->Captures.contains(rel32)) {
		m_problems.push_back(std::format("Signature {}: {} didn't capture", signature, rel32));
		return 0;
	}
	return m->target(rel32);
}

uint64_t xivres::game_layout::target(const std::string& rel32) {
	std::vector<std::pair<std::string, uint64_t>> targets;
	for (const auto& [name, m] : m_matches) {
		if (const auto it = m.Types.find(rel32); it != m.Types.end() && it->second == "rel32" && m.Captures.contains(rel32))
			targets.emplace_back(name, m.target(rel32));
	}
	if (targets.empty()) {
		m_problems.push_back(std::format("no signature found says where {} is", rel32));
		return 0;
	}
	for (const auto& t : targets) {
		if (t.second == targets.front().second)
			continue;
		std::string message = std::format("{} differs by signature:", rel32);
		for (const auto& [name, address] : targets)
			message += std::format(" {:X} by {}", address, name);
		m_problems.push_back(std::move(message));
		return 0;
	}
	return targets.front().second;
}

const xivres::game_layout::match* xivres::game_layout::find(const std::string& signature) {
	if (const auto it = m_matches.find(signature); it != m_matches.end())
		return &it->second;
	if (const auto it = m_failures.find(signature); it != m_failures.end())
		m_problems.push_back(it->second);
	else
		m_problems.push_back(std::format("Signature {} wasn't looked for", signature));
	return nullptr;
}
