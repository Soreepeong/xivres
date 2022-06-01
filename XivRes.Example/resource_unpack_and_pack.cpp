#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <iostream>
#include <Windows.h>
#include <windowsx.h>

#include "xivres/installation.h"
#include "xivres/PackedFileUnpackingStream.h"
#include "xivres/TextureStream.h"
#include "xivres/packed_stream.standard.h"
#include "xivres/packed_stream.texture.h"
#include "xivres/packed_stream.model.h"
#include "xivres/TexturePreview.Windows.h"
#include "xivres/util.thread_pool.h"

template<typename TPassthroughPacker, typename TCompressingPacker>
auto Test(std::shared_ptr<xivres::packed_stream> packed, xivres::xiv_path_spec pathSpec) {
	using namespace xivres;

	std::shared_ptr<stream> decoded;
	const char* pcszLastStep = "Read packed original";
	try {
		const auto packedOriginal = packed->read_vector<char>();
		
		pcszLastStep = "Decode packed original";
		decoded = std::make_shared<PackedFileUnpackingStream>(packed);
		if (decoded->size() == 0)
			return std::make_pair(packed->get_packed_type(), std::string());
		const auto decodedOriginal = decoded->read_vector<char>();

		pcszLastStep = "Compress-pack decoded original";
		packed = std::make_shared<compressing_packed_stream<TCompressingPacker>>(pathSpec, decoded, Z_BEST_COMPRESSION);
		const auto packedCompressed = packed->read_vector<char>();

		pcszLastStep = "Decode Compress-packed";
		decoded = std::make_shared<PackedFileUnpackingStream>(packed);
		const auto decodedCompressed = decoded->read_vector<char>();

		if (decodedOriginal.size() != decodedCompressed.size() || memcmp(&decodedOriginal[0], &decodedCompressed[0], decodedCompressed.size()) != 0)
			return std::make_pair(packed->get_packed_type(), std::string("DIFF(Comp)"));

		pcszLastStep = "Passthrough-pack decoded original";
		packed = std::make_shared<passthrough_packed_stream<TPassthroughPacker>>(pathSpec, decoded);
		const auto packedPassthrough = packed->read_vector<char>();

		pcszLastStep = "Decode Passthrough-packed";
		decoded = std::make_shared<PackedFileUnpackingStream>(packed);
		const auto decodedPassthrough = decoded->read_vector<char>();

		if (decodedOriginal.size() != decodedPassthrough.size() || memcmp(&decodedOriginal[0], &decodedPassthrough[0], decodedPassthrough.size()) != 0)
			return std::make_pair(packed->get_packed_type(), std::string("DIFF(Pass)"));

		return std::make_pair(packed->get_packed_type(), std::string());
	} catch (const std::exception& e) {
		return std::make_pair(packed->get_packed_type(), std::format("{}: {}", pcszLastStep, e.what()));
	}
}

int main() {
	constexpr auto UseThreading = true;

	SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);

	system("chcp 65001 > NUL");

	xivres::installation gameReader(R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");

	for (const auto packId : gameReader.get_sqpack_ids()) {
		const auto& packfile = gameReader.get_sqpack(packId);

		xivres::util::thread_pool<const xivres::SqpackReader::EntryInfoType*, std::pair<xivres::packed_type, std::string>> pool;
		for (size_t i = 0; i < packfile.EntryInfo.size(); i++) {
			const auto& entry = packfile.EntryInfo[i];
			const auto cb = [&packfile, pathSpec = entry.path_spec]() {
				try {
					auto packed = packfile.GetPackedFileStream(pathSpec);
					switch (packed->get_packed_type()) {
						case xivres::packed_type::none: return std::make_pair(packed->get_packed_type(), std::string());
						case xivres::packed_type::empty_or_hidden: return std::make_pair(packed->get_packed_type(), std::string());
						case xivres::packed_type::standard: return Test<xivres::standard_passthrough_packer, xivres::standard_compressing_packer>(std::move(packed), pathSpec);
						case xivres::packed_type::model: return Test<xivres::model_passthrough_packer, xivres::model_compressing_packer>(std::move(packed), pathSpec);
						case xivres::packed_type::texture: return Test<xivres::texture_passthrough_packer, xivres::texture_compressing_packer>(std::move(packed), pathSpec);
						default: return std::make_pair(packed->get_packed_type(), std::string());
					}
				} catch (const std::out_of_range&) {
					return std::make_pair(xivres::packed_type::none, std::string());
				}
			};

			if constexpr (UseThreading)
				pool.Submit(&entry, cb);
			else {
				auto [pack_type, res] = cb();
				std::cout << std::format("\r[{0>6X}:{:0>6}/{:0>6} {:08x}/{:08x}={:08x}]", packId, i, packfile.EntryInfo.size(), entry.path_spec.PathHash(), entry.path_spec.NameHash(), entry.path_spec.FullPathHash());
				switch (pack_type) {
					case xivres::packed_type::model: std::cout << " Model   "; break;
					case xivres::packed_type::standard: std::cout << " Standard"; break;
					case xivres::packed_type::texture: std::cout << " Texture "; break;
				}
				if (!res.empty())
					std::cout << std::format("\n\t=>{}\n", res);
			}
		}

		pool.SubmitDone();

		if constexpr (UseThreading) {
			for (size_t i = 0;; i++) {
				const auto resultPair = pool.GetResult();
				if (!resultPair)
					break;

				const auto& entry = *resultPair->first;
				const auto& [pack_type, res] = resultPair->second;

				if (!res.empty() || i % 500 == 0) {
					std::cout << std::format("\r[{:0>6X}:{:0>6}/{:0>6} {:08x}/{:08x}={:08x}]", packId, i, packfile.EntryInfo.size(), entry.path_spec.PathHash(), entry.path_spec.NameHash(), entry.path_spec.FullPathHash());
					switch (pack_type) {
						case xivres::packed_type::model: std::cout << " Model   "; break;
						case xivres::packed_type::standard: std::cout << " Standard"; break;
						case xivres::packed_type::texture: std::cout << " Texture "; break;
					}
					if (!res.empty())
						std::cout << std::format("\n\t=>{}\n", res);
				}
			}
		}
	}

	std::cout << std::endl;

	// const auto pathSpec = SqpackPathSpec("common/font/font1.tex");
	// const auto pathSpec = SqpackPathSpec("common/graphics/texture/-caustics.tex");
	// const auto pathSpec = SqpackPathSpec("common/graphics/texture/-omni_shadow_index_table.tex");

	// internal::ShowTextureStream(TextureStream(decoded));

	return 0;
}
