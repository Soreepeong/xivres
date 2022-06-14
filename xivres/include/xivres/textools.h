#pragma once

#include <srell.hpp>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "equipment_and_gimmick_parameter.h"
#include "equipment_deformer_parameter.h"
#include "ex_skeleton_table.h"
#include "image_change_data.h"

namespace xivres::textools {
	struct mod_pack {
		std::string Name;
		std::string Author;
		std::string Version;
		std::string Url;
	};
	void to_json(nlohmann::json&, const mod_pack&);
	void from_json(const nlohmann::json&, mod_pack&);

	struct mods_json {
		std::string Name;
		std::string Category;
		std::string FullPath;
		uint64_t ModOffset{};
		uint64_t ModSize{};
		std::string DatFile;
		bool IsDefault{};
		std::optional<mod_pack> ModPack;

		[[nodiscard]] bool is_textools_metadata() const;
	};
	void to_json(nlohmann::json&, const mods_json&);
	void from_json(const nlohmann::json&, mods_json&);

	namespace mod_pack_page {
		struct mod_option_json {
			std::string Name;
			std::string Description;
			std::string ImagePath;
			std::vector<mods_json> ModsJsons;
			std::string GroupName;
			std::string SelectionType;
			bool IsChecked;
		};
		void to_json(nlohmann::json&, const mod_option_json&);
		void from_json(const nlohmann::json&, mod_option_json&);

		struct mod_group_json {
			std::string GroupName;
			std::string SelectionType;
			std::vector<mod_option_json> OptionList;
		};
		void to_json(nlohmann::json&, const mod_group_json&);
		void from_json(const nlohmann::json&, mod_group_json&);

		struct mod_pack_page_json {
			int PageIndex{};
			std::vector<mod_group_json> ModGroups;
		};
		void to_json(nlohmann::json&, const mod_pack_page_json&);
		void from_json(const nlohmann::json&, mod_pack_page_json&);

	}

	struct mod_pack_json {
		std::string TTMPVersion;
		std::string Name;
		std::string Author;
		std::string Version;
		std::string Description;
		std::string Url;
		std::string MinimumFrameworkVersion;
		std::vector<mod_pack_page::mod_pack_page_json> ModPackPages;
		std::vector<mods_json> SimpleModsList;

		void for_each(std::function<void(mods_json&)> cb, const nlohmann::json& choices = {});
		void for_each(std::function<void(const mods_json&)> cb, const nlohmann::json& choices = {}) const;
	};
	void to_json(nlohmann::json&, const mod_pack_json&);
	void from_json(const nlohmann::json&, mod_pack_json&);

	class metafile {
	public:
		static constexpr uint32_t Version_Value = 2;
		static const srell::u8cregex CharacterMetaPathTest;
		static const srell::u8cregex HousingMetaPathTest;

		enum class meta_types : uint32_t {
			Invalid,
			Imc,
			Eqdp,
			Eqp,
			Est,
			Gmp,
		};

		enum class est_types {
			Invalid,
			Face,
			Hair,
			Head,
			Body,
		};

		enum class item_types {
			Invalid,
			Equipment,
			Accessory,
			Housing,
		};

#pragma pack(push, 1)
		struct meta_header {
			LE<uint32_t> EntryCount;
			LE<uint32_t> HeaderSize;
			LE<uint32_t> FirstEntryLocatorOffset;
		};

		struct entry_locator {
			LE<meta_types> Type;
			LE<uint32_t> Offset;
			LE<uint32_t> Size;
		};

		struct equipment_deformer_parameter_entry {
			uint32_t RaceCode;
			uint8_t Value : 2;
			uint8_t Padding : 6;
		};
		static_assert(sizeof equipment_deformer_parameter_entry == 5);

		struct equipment_and_gimmick_parameter_entry {
			uint32_t Enabled : 1;
			uint32_t Animated : 1;
			uint32_t RotationA : 10;
			uint32_t RotationB : 10;
			uint32_t RotationC : 10;
			uint8_t UnknownLow : 4;
			uint8_t UnknownHigh : 4;
		};
		static_assert(sizeof equipment_and_gimmick_parameter_entry == 5);

		struct ex_skeleton_table_entry_t {
			uint16_t RaceCode;
			uint16_t SetId;
			uint16_t SkelId;
		};
#pragma pack(pop)

		const std::vector<uint8_t> Data;
		const uint32_t& Version;
		const std::string TargetPath;
		const std::string SourcePath;
		const meta_header& Header;
		const std::span<const entry_locator> AllEntries;

		item_types ItemType = item_types::Invalid;
		est_types EstType = est_types::Invalid;
		std::string PrimaryType;
		std::string SecondaryType;
		std::string TargetImcPath;
		std::string SourceImcPath;
		uint16_t PrimaryId = 0;
		uint16_t SecondaryId = 0;
		size_t SlotIndex = 0;
		size_t EqpEntrySize = 0;
		size_t EqpEntryOffset = 0;

		metafile(std::string gamePath, const stream& stream);

		template<typename T>
		[[nodiscard]] std::span<const T> get_span(meta_types type) const {
			for (const auto& entry : AllEntries) {
				if (entry.Type != type)
					continue;
				const auto spanBytes = std::span(Data).subspan(entry.Offset, entry.Size);
				return { reinterpret_cast<const T*>(spanBytes.data()), spanBytes.size_bytes() / sizeof T };
			}
			return {};
		}

		static std::string equipment_deformer_parameter_path(item_types type, uint32_t race) {
			switch (type) {
				case item_types::Equipment:
					return std::format("chara/xls/charadb/equipmentdeformerparameter/c{:04}.eqdp", race);
				case item_types::Accessory:
					return std::format("chara/xls/charadb/accessorydeformerparameter/c{:04}.eqdp", race);
				default:
					throw std::invalid_argument("only equipment and accessory have valid eqdp");
			}
		}

		static constexpr auto EqpPath = "chara/xls/equipmentparameter/equipmentparameter.eqp";
		static constexpr auto GmpPath = "chara/xls/equipmentparameter/gimmickparameter.gmp";

		static const char* ex_skeleton_table_path(est_types type) {
			switch (type) {
				case est_types::Face:
					return "chara/xls/charadb/faceskeletontemplate.est";
				case est_types::Hair:
					return "chara/xls/charadb/hairskeletontemplate.est";
				case est_types::Head:
					return "chara/xls/charadb/extra_met.est";
				case est_types::Body:
					return "chara/xls/charadb/extra_top.est";
				default:
					return nullptr;
			}
		}

		void apply_image_change_data_edits(std::function<image_change_data::file& ()> reader) const;
		void apply_equipment_deformer_parameter_edits(std::function<equipment_deformer_parameter_file& (item_types, uint32_t)> reader) const;
		[[nodiscard]] bool has_equipment_parameter_edits() const;
		void apply_equipment_parameter_edits(equipment_parameter_file& eqp) const;
		[[nodiscard]] bool has_gimmick_parameter_edits() const;
		void apply_gimmick_parameter_edits(gimmmick_parameter_file& gmp) const;
		void apply_ex_skeleton_table_edits(ex_skeleton_table_file& est) const;
	};
}