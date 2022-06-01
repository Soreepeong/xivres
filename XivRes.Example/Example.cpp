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
std::string Test(std::shared_ptr<xivres::packed_stream> packed, xivres::xiv_path_spec pathSpec) {
	using namespace xivres;

	std::shared_ptr<stream> decoded;

	try {
		const auto packedOriginal = packed->read_vector<char>();
		
		decoded = std::make_shared<PackedFileUnpackingStream>(packed);
		if (decoded->size() == 0)
			return {};
		const auto decodedOriginal = decoded->read_vector<char>();

		packed = std::make_shared<compressing_packed_stream<TCompressingPacker>>(pathSpec, decoded, Z_BEST_COMPRESSION);
		const auto packedCompressed = packed->read_vector<char>();

		decoded = std::make_shared<PackedFileUnpackingStream>(packed);
		const auto decodedCompressed = decoded->read_vector<char>();

		if (decodedOriginal.size() != decodedCompressed.size() || memcmp(&decodedOriginal[0], &decodedCompressed[0], decodedCompressed.size()) != 0)
			return "DIFF(Comp)";

		packed = std::make_shared<passthrough_packed_stream<TPassthroughPacker>>(pathSpec, decoded);
		const auto packedPassthrough = packed->read_vector<char>();

		decoded = std::make_shared<PackedFileUnpackingStream>(packed);
		const auto decodedPassthrough = decoded->read_vector<char>();

		if (decodedOriginal.size() != decodedPassthrough.size() || memcmp(&decodedOriginal[0], &decodedPassthrough[0], decodedPassthrough.size()) != 0)
			return "DIFF(Pass)";

		return {};
	} catch (const std::exception& e) {
		return e.what();
	}
}

int main() {
	constexpr auto UseThreading = false;

	system("chcp 65001 > NUL");

	xivres::installation gameReader(R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");

	const auto& packfile = gameReader.get_sqpack(0x040000);

	xivres::util::thread_pool<const xivres::SqpackReader::EntryInfoType*, std::string> pool;
	for (size_t i = 0; i < packfile.EntryInfo.size(); i++) {
		const auto& entry = packfile.EntryInfo[i];
		const auto cb = [&packfile, pathSpec = entry.path_spec]()->std::string {
			try {
				auto packed = packfile.GetPackedFileStream(pathSpec);
				switch (packed->get_packed_type()) {
					case xivres::packed_type::None: return {};
					case xivres::packed_type::EmptyOrObfuscated: return {};
					// case xivres::packed_type::Binary: return Test<xivres::standard_passthrough_packer, xivres::standard_compressing_packer>(std::move(packed), pathSpec);
					case xivres::packed_type::Model: return Test<xivres::model_passthrough_packer, xivres::model_compressing_packer>(std::move(packed), pathSpec);
					// case xivres::packed_type::Texture: return Test<xivres::texture_passthrough_packer, xivres::texture_compressing_packer>(std::move(packed), pathSpec);
					default: return {};
				}
			} catch (const std::out_of_range&) {
				return {};
			}
		};

		if constexpr (UseThreading)
			pool.Submit(&entry, cb);
		else {
			std::cout << std::format("\r[{:0>6}/{:0>6} {:08x}/{:08x}={:08x}]", i, packfile.EntryInfo.size(), entry.path_spec.PathHash(), entry.path_spec.NameHash(), entry.path_spec.FullPathHash());
			if (auto res = cb(); !res.empty())
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
			const auto& res = resultPair->second;

			if (!res.empty() || i % 500 == 0) {
				std::cout << std::format("\r[{:0>6}/{:0>6} {:08x}/{:08x}={:08x}]", i, packfile.EntryInfo.size(), entry.path_spec.PathHash(), entry.path_spec.NameHash(), entry.path_spec.FullPathHash());
				if (!res.empty())
					std::cout << std::format("\n\t=>{}\n", res);
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
