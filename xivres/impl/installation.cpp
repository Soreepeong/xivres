#include "../include/xivres/installation.h"

xivres::installation::installation(std::filesystem::path gamePath)
	: m_gamePath(std::move(gamePath)) {
	for (const auto& iter : std::filesystem::recursive_directory_iterator(m_gamePath / "sqpack")) {
		if (iter.is_directory() || !iter.path().wstring().ends_with(L".win32.index"))
			continue;

		auto packFileName = std::filesystem::path{ iter.path().filename() }.replace_extension("").replace_extension("").string();
		if (packFileName.size() < 6)
			continue;

		packFileName.resize(6);

		const auto packFileId = std::strtol(&packFileName[0], nullptr, 16);
		m_readers.emplace(packFileId, std::optional<sqpack::reader>());
	}
}

std::shared_ptr<xivres::packed_stream> xivres::installation::get_file_packed(const path_spec& pathSpec) const {
	return get_sqpack(pathSpec).packed_at(pathSpec);
}

std::shared_ptr<xivres::unpacked_stream> xivres::installation::get_file(const path_spec& pathSpec, std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return std::make_shared<unpacked_stream>(get_sqpack(pathSpec).packed_at(pathSpec), obfuscatedHeaderRewrite);
}

const std::vector<uint32_t> xivres::installation::get_sqpack_ids() const {
	std::vector<uint32_t> res;
	res.reserve(m_readers.size());
	for (const auto& key : m_readers | std::views::keys)
		res.emplace_back(key);
	return res;
}

const xivres::sqpack::reader& xivres::installation::get_sqpack(uint8_t categoryId, uint8_t expacId, uint8_t partId) const {
	return get_sqpack((categoryId << 16) | (expacId << 8) | partId);
}

const xivres::sqpack::reader& xivres::installation::get_sqpack(const path_spec& rawpath_spec) const {
	return get_sqpack(rawpath_spec.PackNameValue());
}

const xivres::sqpack::reader& xivres::installation::get_sqpack(uint32_t packId) const {
	auto& item = m_readers.at(packId);
	if (item)
		return *item;

	const auto lock = std::lock_guard(m_populateMtx);
	if (item)
		return *item;

	const auto expacId = (packId >> 8) & 0xFF;
	if (expacId == 0)
		return item.emplace(sqpack::reader::from_path(m_gamePath / std::format("sqpack/ffxiv/{:0>6x}.win32.index", packId)));
	else
		return item.emplace(sqpack::reader::from_path(m_gamePath / std::format("sqpack/ex{}/{:0>6x}.win32.index", expacId, packId)));
}

void xivres::installation::preload_all_sqpacks() const {
	const auto lock = std::lock_guard(m_populateMtx);
	for (const auto& key : m_readers | std::views::keys)
		void(get_sqpack(key));
}
