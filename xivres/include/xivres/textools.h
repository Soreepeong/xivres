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
	template<typename T>
	using LE = LE<T>;

	struct mod_pack_entry_t {
		std::string Name;
		std::string Author;
		std::string Version;
		std::string Url;
	};
	void to_json(nlohmann::json&, const mod_pack_entry_t&);
	void from_json(const nlohmann::json&, mod_pack_entry_t&);

	struct mod_entry_t {
		std::string Name;
		std::string Category;
		std::string FullPath;
		uint64_t ModOffset{};
		uint64_t ModSize{};
		std::string DatFile;
		bool IsDefault{};
		std::optional<mod_pack_entry_t> ModPack;

		[[nodiscard]] bool is_textools_metadata() const;
	};
	void to_json(nlohmann::json&, const mod_entry_t&);
	void from_json(const nlohmann::json&, mod_entry_t&);

	namespace mod_pack_page {
		struct option_t {
			std::string Name;
			std::string Description;
			std::string ImagePath;
			std::vector<mod_entry_t> ModsJsons;
			std::string GroupName;
			std::string SelectionType;
			bool IsChecked;
		};
		void to_json(nlohmann::json&, const option_t&);
		void from_json(const nlohmann::json&, option_t&);

		struct mod_group_t {
			std::string GroupName;
			std::string SelectionType;
			std::vector<option_t> OptionList;
		};
		void to_json(nlohmann::json&, const mod_group_t&);
		void from_json(const nlohmann::json&, mod_group_t&);

		struct page_t {
			int PageIndex{};
			std::vector<mod_group_t> ModGroups;
		};
		void to_json(nlohmann::json&, const page_t&);
		void from_json(const nlohmann::json&, page_t&);

	}

	struct ttmpl_t {
		std::string MinimumFrameworkVersion;
		std::string FormatVersion;
		std::string Name;
		std::string Author;
		std::string Version;
		std::string Description;
		std::string Url;
		std::vector<mod_pack_page::page_t> ModPackPages;
		std::vector<mod_entry_t> SimpleModsList;

		void for_each(std::function<void(textools::mod_entry_t&)> cb, const nlohmann::json& choices = {});
		void for_each(std::function<void(const textools::mod_entry_t&)> cb, const nlohmann::json& choices = {}) const;
	};
	void to_json(nlohmann::json&, const ttmpl_t&);
	void from_json(const nlohmann::json&, ttmpl_t&);

	class metafile {
	public:
		static constexpr uint32_t Version_Value = 2;
		static const srell::u8cregex CharacterMetaPathTest;
		static const srell::u8cregex HousingMetaPathTest;

		enum class meta_data_type_t : uint32_t {
			Invalid,
			Imc,
			Eqdp,
			Eqp,
			Est,
			Gmp,
		};

		enum class est_type_t {
			Invalid,
			Face,
			Hair,
			Head,
			Body,
		};

		enum class item_type_t {
			Invalid,
			Equipment,
			Accessory,
			Housing,
		};

#pragma pack(push, 1)
		struct header_t {
			LE<uint32_t> EntryCount;
			LE<uint32_t> HeaderSize;
			LE<uint32_t> FirstEntryLocatorOffset;
		};

		struct entry_locator_t {
			LE<meta_data_type_t> Type;
			LE<uint32_t> Offset;
			LE<uint32_t> Size;
		};

		struct equipment_deformer_parameter_entry_t {
			uint32_t RaceCode;
			uint8_t Value : 2;
			uint8_t Padding : 6;
		};
		static_assert(sizeof equipment_deformer_parameter_entry_t == 5);

		struct equipment_and_gimmick_parameter_entry_t {
			uint32_t Enabled : 1;
			uint32_t Animated : 1;
			uint32_t RotationA : 10;
			uint32_t RotationB : 10;
			uint32_t RotationC : 10;
			uint8_t UnknownLow : 4;
			uint8_t UnknownHigh : 4;
		};
		static_assert(sizeof equipment_and_gimmick_parameter_entry_t == 5);

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
		const header_t& Header;
		const std::span<const entry_locator_t> AllEntries;

		item_type_t ItemType = item_type_t::Invalid;
		est_type_t EstType = est_type_t::Invalid;
		std::string PrimaryType;
		std::string SecondaryType;
		std::string TargetImcPath;
		std::string SourceImcPath;
		uint16_t PrimaryId = 0;
		uint16_t SecondaryId = 0;
		size_t SlotIndex = 0;
		size_t EqpEntrySize = 0;
		size_t EqpEntryOffset = 0;

		metafile(std::string gamePath, const xivres::stream& stream);

		template<typename T>
		[[nodiscard]] std::span<const T> get_span(meta_data_type_t type) const {
			for (const auto& entry : AllEntries) {
				if (entry.Type != type)
					continue;
				const auto spanBytes = std::span(Data).subspan(entry.Offset, entry.Size);
				return { reinterpret_cast<const T*>(spanBytes.data()), spanBytes.size_bytes() / sizeof T };
			}
			return {};
		}

		static std::string equipment_deformer_parameter_path(item_type_t type, uint32_t race) {
			switch (type) {
				case item_type_t::Equipment:
					return std::format("chara/xls/charadb/equipmentdeformerparameter/c{:04}.eqdp", race);
				case item_type_t::Accessory:
					return std::format("chara/xls/charadb/accessorydeformerparameter/c{:04}.eqdp", race);
				default:
					throw std::invalid_argument("only equipment and accessory have valid eqdp");
			}
		}

		static constexpr auto EqpPath = "chara/xls/equipmentparameter/equipmentparameter.eqp";
		static constexpr auto GmpPath = "chara/xls/equipmentparameter/gimmickparameter.gmp";

		static const char* ex_skeleton_table_path(est_type_t type) {
			switch (type) {
				case est_type_t::Face:
					return "chara/xls/charadb/faceskeletontemplate.est";
				case est_type_t::Hair:
					return "chara/xls/charadb/hairskeletontemplate.est";
				case est_type_t::Head:
					return "chara/xls/charadb/extra_met.est";
				case est_type_t::Body:
					return "chara/xls/charadb/extra_top.est";
				default:
					return nullptr;
			}
		}

		void apply_image_change_data_edits(std::function<xivres::image_change_data::file& ()> reader) const;
		void apply_equipment_deformer_parameter_edits(std::function<xivres::equipment_deformer_parameter_file& (item_type_t, uint32_t)> reader) const;
		[[nodiscard]] bool has_equipment_parameter_edits() const;
		void apply_equipment_parameter_edits(xivres::equipment_parameter_file& eqp) const;
		[[nodiscard]] bool has_gimmick_parameter_edits() const;
		void apply_gimmick_parameter_edits(xivres::gimmmick_parameter_file& gmp) const;
		void apply_ex_skeleton_table_edits(xivres::ex_skeleton_table_file& est) const;
	};
}