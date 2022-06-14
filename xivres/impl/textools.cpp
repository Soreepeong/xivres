#include "../include/xivres/textools.h"

#include <set>

using namespace std::string_literals;

template<typename T>
T JsonValueOrDefault(const nlohmann::json& json, const char* key, T defaultValue, T nullDefaultValue) {
	if (const auto it = json.find(key); it != json.end()) {
		if (it->is_null())
			return nullDefaultValue;
		return it->get<T>();
	}
	return defaultValue;
}

void xivres::textools::to_json(nlohmann::json& j, const mod_pack_entry_t& p) {
	j = nlohmann::json::object({
		{"Name", p.Name},
		{"Author", p.Author},
		{"Version", p.Version},
		{"Url", p.Url},
		});
}

void xivres::textools::from_json(const nlohmann::json& j, mod_pack_entry_t& p) {
	if (j.is_null())
		return;
	if (!j.is_object())
		throw bad_data_error("ModPackEntry must be an object");

	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Author = JsonValueOrDefault(j, "Author", ""s, ""s);
	p.Version = JsonValueOrDefault(j, "Version", ""s, ""s);
	p.Url = JsonValueOrDefault(j, "Url", ""s, ""s);
}

void xivres::textools::to_json(nlohmann::json& j, const mod_entry_t& p) {
	j = nlohmann::json::object({
		{"Name", p.Name},
		{"Category", p.Category},
		{"FullPath", p.FullPath},
		{"ModOffset", p.ModOffset},
		{"ModSize", p.ModSize},
		{"DatFile", p.DatFile},
		{"IsDefault", p.IsDefault},
		});
	if (p.ModPack)
		j["ModPackEntry"] = *p.ModPack;
}

void xivres::textools::from_json(const nlohmann::json& j, mod_entry_t& p) {
	if (!j.is_object())
		throw bad_data_error("ModEntry must be an object");

	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Category = JsonValueOrDefault(j, "Category", ""s, ""s);
	p.FullPath = JsonValueOrDefault(j, "FullPath", ""s, ""s);
	p.ModOffset = JsonValueOrDefault(j, "ModOffset", 0ULL, 0ULL);
	p.ModSize = JsonValueOrDefault(j, "ModSize", 0ULL, 0ULL);
	p.DatFile = JsonValueOrDefault(j, "DatFile", ""s, ""s);
	p.IsDefault = JsonValueOrDefault(j, "IsDefault", false, false);
	if (const auto it = j.find("ModPackEntry"); it != j.end() && !it->is_null())
		p.ModPack = it->get<mod_pack_entry_t>();
}

void xivres::textools::mod_pack_page::to_json(nlohmann::json& j, const option_t& p) {
	j = nlohmann::json::object({
		{"Name", p.Name},
		{"Description", p.Description},
		{"ImagePath", p.ImagePath},
		{"ModsJsons", p.ModsJsons},
		{"GroupName", p.GroupName},
		{"SelectionType", p.SelectionType},
		{"IsChecked", p.IsChecked},
		});
}

void xivres::textools::mod_pack_page::from_json(const nlohmann::json& j, option_t& p) {
	if (!j.is_object())
		throw bad_data_error("Option must be an object");

	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Description = JsonValueOrDefault(j, "Description", ""s, ""s);
	p.ImagePath = JsonValueOrDefault(j, "ImagePath", ""s, ""s);
	if (const auto it = j.find("ModsJsons"); it != j.end() && !it->is_null())
		p.ModsJsons = it->get<decltype(p.ModsJsons)>();
	p.GroupName = JsonValueOrDefault(j, "GroupName", ""s, ""s);
	p.SelectionType = JsonValueOrDefault(j, "SelectionType", ""s, ""s);
	p.IsChecked = JsonValueOrDefault(j, "IsChecked", false, false);
}

void xivres::textools::mod_pack_page::to_json(nlohmann::json& j, const mod_group_t& p) {
	j = nlohmann::json::object({
		{"GroupName", p.GroupName},
		{"SelectionType", p.SelectionType},
		{"OptionList", p.OptionList},
		});
}

void xivres::textools::mod_pack_page::from_json(const nlohmann::json& j, mod_group_t& p) {
	if (!j.is_object())
		throw bad_data_error("Option must be an object");

	p.GroupName = JsonValueOrDefault(j, "GroupName", ""s, ""s);
	p.SelectionType = JsonValueOrDefault(j, "SelectionType", ""s, ""s);
	if (const auto it = j.find("OptionList"); it != j.end() && !it->is_null())
		p.OptionList = it->get<decltype(p.OptionList)>();
}

void xivres::textools::mod_pack_page::to_json(nlohmann::json& j, const page_t& p) {
	j = nlohmann::json::object({
		{"PageIndex", p.PageIndex},
		{"ModGroups", p.ModGroups},
		});
}

void xivres::textools::mod_pack_page::from_json(const nlohmann::json& j, page_t& p) {
	if (!j.is_object())
		throw bad_data_error("Option must be an object");

	p.PageIndex = JsonValueOrDefault(j, "PageIndex", 0, 0);
	if (const auto it = j.find("ModGroups"); it != j.end() && !it->is_null())
		p.ModGroups = it->get<decltype(p.ModGroups)>();
}

void xivres::textools::to_json(nlohmann::json& j, const ttmpl_t& p) {
	j = nlohmann::json::object({
		{"MinimumFrameworkVersion", p.MinimumFrameworkVersion},
		{"FormatVersion", p.FormatVersion},
		{"Name", p.Name},
		{"Author", p.Author},
		{"Version", p.Version},
		{"Description", p.Description},
		{"Url", p.Url},
		{"ModPackPages", p.ModPackPages},
		{"SimpleModsList", p.SimpleModsList},
		});
}

void xivres::textools::from_json(const nlohmann::json& j, ttmpl_t& p) {
	if (!j.is_object())
		throw bad_data_error("TTMPL must be an object");

	p.MinimumFrameworkVersion = JsonValueOrDefault(j, "MinimumFrameworkVersion", ""s, ""s);
	p.FormatVersion = JsonValueOrDefault(j, "FormatVersion", ""s, ""s);
	p.Name = JsonValueOrDefault(j, "Name", ""s, ""s);
	p.Author = JsonValueOrDefault(j, "Author", ""s, ""s);
	p.Version = JsonValueOrDefault(j, "Version", ""s, ""s);
	p.Description = JsonValueOrDefault(j, "Description", ""s, ""s);
	p.Url = JsonValueOrDefault(j, "Url", ""s, ""s);
	if (const auto it = j.find("ModPackPages"); it != j.end() && !it->is_null())
		p.ModPackPages = it->get<decltype(p.ModPackPages)>();
	if (const auto it = j.find("SimpleModsList"); it != j.end() && !it->is_null())
		p.SimpleModsList = it->get<decltype(p.SimpleModsList)>();
}

void xivres::textools::ttmpl_t::for_each(std::function<void(mod_entry_t&)> cb, const nlohmann::json& choices) {
	static const nlohmann::json emptyChoices;

	for (auto& entry : SimpleModsList)
		cb(entry);

	for (size_t pageIndex = 0; pageIndex < ModPackPages.size(); pageIndex++) {
		auto& modPackPage = ModPackPages[pageIndex];
		const auto& pageConf = choices.is_array() && pageIndex < choices.size() ? choices[pageIndex] : emptyChoices;
		for (size_t modGroupIndex = 0; modGroupIndex < modPackPage.ModGroups.size(); modGroupIndex++) {
			auto& modGroup = modPackPage.ModGroups[modGroupIndex];

			std::set<size_t> indices;
			if (!pageConf.is_array() || pageConf.size() <= modGroupIndex) {
				for (size_t i = 0; i < modGroup.OptionList.size(); ++i)
					indices.insert(i);
			} else if (pageConf.at(modGroupIndex).is_array()) {
				const auto tmp = pageConf.at(modGroupIndex).get<std::vector<size_t>>();
				indices.insert(tmp.begin(), tmp.end());
			} else {
				indices.insert(pageConf.at(modGroupIndex).get<size_t>());
			}

			for (const auto k : indices) {
				if (k >= modGroup.OptionList.size())
					continue;

				auto& option = modGroup.OptionList[k];
				for (auto& modJson : option.ModsJsons)
					cb(modJson);
			}
		}
	}
}

void xivres::textools::ttmpl_t::for_each(std::function<void(const mod_entry_t&)> cb, const nlohmann::json& choices) const {
	static const nlohmann::json emptyChoices;

	for (auto& entry : SimpleModsList)
		cb(entry);

	for (size_t pageIndex = 0; pageIndex < ModPackPages.size(); pageIndex++) {
		auto& modPackPage = ModPackPages[pageIndex];
		const auto& pageConf = choices.is_array() && pageIndex < choices.size() ? choices[pageIndex] : emptyChoices;
		for (size_t modGroupIndex = 0; modGroupIndex < modPackPage.ModGroups.size(); modGroupIndex++) {
			auto& modGroup = modPackPage.ModGroups[modGroupIndex];

			std::set<size_t> indices;
			if (!pageConf.is_array() || pageConf.size() <= modGroupIndex) {
				for (size_t i = 0; i < modGroup.OptionList.size(); ++i)
					indices.insert(i);
			} else if (pageConf.at(modGroupIndex).is_array()) {
				const auto tmp = pageConf.at(modGroupIndex).get<std::vector<size_t>>();
				indices.insert(tmp.begin(), tmp.end());
			} else {
				indices.insert(pageConf.at(modGroupIndex).get<size_t>());
			}

			for (const auto k : indices) {
				if (k >= modGroup.OptionList.size())
					continue;

				auto& option = modGroup.OptionList[k];
				for (auto& modJson : option.ModsJsons)
					cb(modJson);
			}
		}
	}
}

bool xivres::textools::mod_entry_t::is_textools_metadata() const {
	if (FullPath.length() < 5)
		return false;
	auto metaExt = FullPath.substr(FullPath.length() - 5);
	for (auto& c : metaExt) {
		if (c < 128)
			c = std::tolower(c);
	}
	return metaExt == ".meta";
}

const srell::u8cregex xivres::textools::metafile::CharacterMetaPathTest(
	"^(?<FullPathPrefix>chara"
	"/(?<PrimaryType>[a-z]+)"
	"/(?<PrimaryCode>[a-z])(?<PrimaryId>[0-9]+)"
	"(?:/obj"
	"/(?<SecondaryType>[a-z]+)"
	"/(?<SecondaryCode>[a-z])(?<SecondaryId>[0-9]+))?"
	"/).*?(?:_(?<Slot>[a-z]{3}))?\\.meta$"
	, srell::u8cregex::icase);
const srell::u8cregex xivres::textools::metafile::HousingMetaPathTest(
	"^(?<FullPathPrefix>bgcommon"
	"/hou"
	"/(?<PrimaryType>[a-z]+)"
	"/general"
	"/(?<PrimaryId>[0-9]+)"
	"/).*?\\.meta$"
	, srell::u8cregex::icase);

xivres::textools::metafile::metafile(std::string gamePath, const stream& stream)
	: Data(stream.read_vector<uint8_t>())
	, Version(*reinterpret_cast<const uint32_t*>(&Data[0]))
	, TargetPath(std::move(gamePath))
	, SourcePath(reinterpret_cast<const char*>(&Data[sizeof Version]))
	, Header(*reinterpret_cast<const header_t*>(&Data[sizeof Version + SourcePath.size() + 1]))
	, AllEntries(span_cast<entry_locator_t>(Data, Header.FirstEntryLocatorOffset, Header.EntryCount)) {
	if (srell::u8csmatch matches;
		regex_search(TargetPath, matches, CharacterMetaPathTest)) {
		PrimaryType = matches["PrimaryType"].str();
		PrimaryId = static_cast<uint16_t>(std::strtol(matches["PrimaryId"].str().c_str(), nullptr, 10));
		SecondaryType = matches["SecondaryType"].str();
		SecondaryId = static_cast<uint16_t>(std::strtol(matches["SecondaryId"].str().c_str(), nullptr, 10));
		if (SecondaryType.empty())
			TargetImcPath = matches["FullPathPrefix"].str() + matches["PrimaryCode"].str() + matches["PrimaryId"].str() + ".imc";
		else
			TargetImcPath = matches["FullPathPrefix"].str() + matches["SecondaryCode"].str() + matches["SecondaryId"].str() + ".imc";
		for (auto& c : PrimaryType) {
			if (c < 128)
				c = std::tolower(c);
		}
		for (auto& c : SecondaryType) {
			if (c < 128)
				c = std::tolower(c);
		}
		if (PrimaryType == "equipment") {
			auto slot = matches["Slot"].str();
			for (auto& c : slot) {
				if (c < 128)
					c = std::tolower(c);
			}
			if (0 == slot.compare("met"))
				ItemType = item_type_t::Equipment, SlotIndex = 0, EqpEntrySize = 3, EqpEntryOffset = 5, EstType = est_type_t::Head;
			else if (0 == slot.compare("top"))
				ItemType = item_type_t::Equipment, SlotIndex = 1, EqpEntrySize = 2, EqpEntryOffset = 0, EstType = est_type_t::Body;
			else if (0 == slot.compare("glv"))
				ItemType = item_type_t::Equipment, SlotIndex = 2, EqpEntrySize = 1, EqpEntryOffset = 3;
			else if (0 == slot.compare("dwn"))
				ItemType = item_type_t::Equipment, SlotIndex = 3, EqpEntrySize = 1, EqpEntryOffset = 2;
			else if (0 == slot.compare("sho"))
				ItemType = item_type_t::Equipment, SlotIndex = 4, EqpEntrySize = 1, EqpEntryOffset = 4;
			else if (0 == slot.compare("ear"))
				ItemType = item_type_t::Accessory, SlotIndex = 0;
			else if (0 == slot.compare("nek"))
				ItemType = item_type_t::Accessory, SlotIndex = 1;
			else if (0 == slot.compare("wrs"))
				ItemType = item_type_t::Accessory, SlotIndex = 2;
			else if (0 == slot.compare("rir"))
				ItemType = item_type_t::Accessory, SlotIndex = 3;
			else if (0 == slot.compare("ril"))
				ItemType = item_type_t::Accessory, SlotIndex = 4;
		} else if (PrimaryType == "human") {
			if (SecondaryType == "hair")
				EstType = est_type_t::Hair;
			else if (SecondaryType == "face")
				EstType = est_type_t::Face;
		}

	} else if (regex_search(TargetPath, matches, HousingMetaPathTest)) {
		PrimaryType = matches["PrimaryType"].str();
		PrimaryId = static_cast<uint16_t>(std::strtol(matches["PrimaryId"].str().c_str(), nullptr, 10));
		ItemType = item_type_t::Housing;

	} else {
		throw bad_data_error("Unsupported meta file");
	}

	if (srell::u8csmatch matches;
		regex_search(SourcePath, matches, CharacterMetaPathTest)) {
		if (SecondaryType.empty())
			SourceImcPath = matches["FullPathPrefix"].str() + matches["PrimaryCode"].str() + matches["PrimaryId"].str() + ".imc";
		else
			SourceImcPath = matches["FullPathPrefix"].str() + matches["SecondaryCode"].str() + matches["SecondaryId"].str() + ".imc";

	} else {
		throw bad_data_error("Unsupported meta file");
	}
}

void xivres::textools::metafile::apply_image_change_data_edits(std::function<image_change_data::file& ()> reader) const {
	if (const auto imcedit = get_span<image_change_data::entry>(meta_data_type_t::Imc); !imcedit.empty()) {
		auto& imc = reader();
		using imc_t = image_change_data::image_change_data_type;
		if (imc.header().Type == imc_t::Unknown) {
			const auto& typeStr = SecondaryType.empty() ? PrimaryType : SecondaryType;
			imc.header().Type = typeStr == "equipment" || typeStr == "accessory" ? imc_t::Set : imc_t::NonSet;
		}
		imc.resize_if_needed(imcedit.size() - 1);
		for (size_t i = 0; i < imcedit.size(); ++i) {
			imc.entry(i * imc.entry_count_per_set() + SlotIndex) = imcedit[i];
		}
	}
}

void xivres::textools::metafile::apply_equipment_deformer_parameter_edits(std::function<equipment_deformer_parameter_file& (item_type_t, uint32_t)> reader) const {
	if (const auto eqdpedit = get_span<equipment_deformer_parameter_entry_t>(meta_data_type_t::Eqdp); !eqdpedit.empty()) {
		for (const auto& v : eqdpedit) {
			auto& eqdp = reader(ItemType, v.RaceCode);
			auto& target = eqdp.setinfo(PrimaryId);
			target &= ~(0b11 << (SlotIndex * 2));
			target |= v.Value << (SlotIndex * 2);
		}
	}
}

bool xivres::textools::metafile::has_equipment_parameter_edits() const {
	return !get_span<uint8_t>(meta_data_type_t::Eqp).empty();
}

void xivres::textools::metafile::apply_equipment_parameter_edits(equipment_parameter_file& eqp) const {
	if (const auto eqpedit = get_span<uint8_t>(meta_data_type_t::Eqp); !eqpedit.empty()) {
		if (eqpedit.size() != EqpEntrySize)
			throw bad_data_error(std::format("expected {}b for eqp; got {}b", EqpEntrySize, eqpedit.size()));
		std::copy_n(&eqpedit[0], EqpEntrySize, &eqp.paramter_bytes(PrimaryId)[EqpEntryOffset]);
	}
}

bool xivres::textools::metafile::has_gimmick_parameter_edits() const {
	return !get_span<uint8_t>(meta_data_type_t::Gmp).empty();
}

void xivres::textools::metafile::apply_gimmick_parameter_edits(gimmmick_parameter_file& gmp) const {
	if (const auto gmpedit = get_span<uint8_t>(meta_data_type_t::Gmp); !gmpedit.empty()) {
		if (gmpedit.size() != sizeof uint64_t)
			throw bad_data_error(std::format("gmp data must be 8 bytes; {} byte(s) given", gmpedit.size()));
		std::copy_n(&gmpedit[0], gmpedit.size(), &gmp.paramter_bytes(PrimaryId)[0]);
	}
}

void xivres::textools::metafile::apply_ex_skeleton_table_edits(ex_skeleton_table_file& est) const {
	if (const auto estedit = get_span<ex_skeleton_table_entry_t>(meta_data_type_t::Est); !estedit.empty()) {
		auto estpairs = est.to_pairs();
		for (const auto& v : estedit) {
			const auto key = ex_skeleton_table_file::descriptor_t{ .SetId = v.SetId, .RaceCode = v.RaceCode };
			if (v.SkelId == 0)
				estpairs.erase(key);
			else
				estpairs.insert_or_assign(key, v.SkelId);
		}
		est.from_pairs(estpairs);
	}
}
