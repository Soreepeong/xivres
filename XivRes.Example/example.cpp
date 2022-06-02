#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <iostream>
#include <Windows.h>
#include <windowsx.h>

#include "xivres/installation.h"
#include "xivres/packed_stream.standard.h"
#include "xivres/packed_stream.texture.h"
#include "xivres/packed_stream.model.h"
#include "xivres/sound.h"
#include "xivres/sqpack.generator.h"
#include "xivres/texture.preview.h"
#include "xivres/texture.stream.h"
#include "xivres/unpacked_stream.h"
#include "xivres/util.thread_pool.h"
#include "xivres/util.unicode.h"

constexpr auto UseThreading = false;

template<typename TPassthroughPacker, typename TCompressingPacker>
static auto test_pack_unpack_file(std::shared_ptr<xivres::packed_stream> packed, xivres::path_spec pathSpec) {
	using namespace xivres;

	std::shared_ptr<stream> decoded;
	const char* pcszLastStep = "Read packed original";
	try {
		const auto packedOriginal = packed->read_vector<char>();

		pcszLastStep = "Decode packed original";
		decoded = std::make_shared<unpacked_stream>(packed);
		if (decoded->size() == 0)
			return std::make_pair(packed->get_packed_type(), std::string());
		const auto decodedOriginal = decoded->read_vector<char>();

		pcszLastStep = "Compress-pack decoded original";
		packed = std::make_shared<compressing_packed_stream<TCompressingPacker>>(pathSpec, decoded, Z_BEST_COMPRESSION);
		const auto packedCompressed = packed->read_vector<char>();

		pcszLastStep = "Decode Compress-packed";
		decoded = std::make_shared<unpacked_stream>(packed);
		const auto decodedCompressed = decoded->read_vector<char>();

		if (decodedOriginal.size() != decodedCompressed.size() || memcmp(&decodedOriginal[0], &decodedCompressed[0], decodedCompressed.size()) != 0)
			return std::make_pair(packed->get_packed_type(), std::string("DIFF(Comp)"));

		pcszLastStep = "Passthrough-pack decoded original";
		packed = std::make_shared<passthrough_packed_stream<TPassthroughPacker>>(pathSpec, decoded);
		const auto packedPassthrough = packed->read_vector<char>();

		pcszLastStep = "Decode Passthrough-packed";
		decoded = std::make_shared<unpacked_stream>(packed);
		const auto decodedPassthrough = decoded->read_vector<char>();

		if (decodedOriginal.size() != decodedPassthrough.size() || memcmp(&decodedOriginal[0], &decodedPassthrough[0], decodedPassthrough.size()) != 0)
			return std::make_pair(packed->get_packed_type(), std::string("DIFF(Pass)"));

		return std::make_pair(packed->get_packed_type(), std::string());
	} catch (const std::exception& e) {
		return std::make_pair(packed->get_packed_type(), std::format("{}: {}", pcszLastStep, e.what()));
	}
}

static void test_pack_unpack(const xivres::installation& gameReader) {
	for (const auto packId : gameReader.get_sqpack_ids()) {
		const auto& packfile = gameReader.get_sqpack(packId);

		xivres::util::thread_pool<const xivres::sqpack::reader::entry_info*, std::pair<xivres::packed::type, std::string>> pool;
		for (size_t i = 0; i < packfile.Entries.size(); i++) {
			const auto& entry = packfile.Entries[i];
			const auto cb = [&packfile, pathSpec = entry.path_spec]() {
				try {
					auto packed = packfile.packed_at(pathSpec);
					switch (packed->get_packed_type()) {
						case xivres::packed::type::none: return std::make_pair(packed->get_packed_type(), std::string());
						case xivres::packed::type::placeholder: return std::make_pair(packed->get_packed_type(), std::string());
						case xivres::packed::type::standard: return test_pack_unpack_file<xivres::standard_passthrough_packer, xivres::standard_compressing_packer>(std::move(packed), pathSpec);
						case xivres::packed::type::model: return test_pack_unpack_file<xivres::model_passthrough_packer, xivres::model_compressing_packer>(std::move(packed), pathSpec);
						case xivres::packed::type::texture: return test_pack_unpack_file<xivres::texture_passthrough_packer, xivres::texture_compressing_packer>(std::move(packed), pathSpec);
						default: return std::make_pair(packed->get_packed_type(), std::string());
					}
				} catch (const std::out_of_range&) {
					return std::make_pair(xivres::packed::type::none, std::string());
				}
			};

			if constexpr (UseThreading)
				pool.Submit(&entry, cb);
			else {
				auto [pack_type, res] = cb();
				std::cout << std::format("\r[{:0>6X}:{:0>6}/{:0>6} {:08x}/{:08x}={:08x}]", packId, i, packfile.Entries.size(), entry.path_spec.PathHash(), entry.path_spec.NameHash(), entry.path_spec.FullPathHash());
				switch (pack_type) {
					case xivres::packed::type::model: std::cout << " Model   "; break;
					case xivres::packed::type::standard: std::cout << " Standard"; break;
					case xivres::packed::type::texture: std::cout << " Texture "; break;
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
					std::cout << std::format("\r[{:0>6X}:{:0>6}/{:0>6} {:08x}/{:08x}={:08x}]", packId, i, packfile.Entries.size(), entry.path_spec.PathHash(), entry.path_spec.NameHash(), entry.path_spec.FullPathHash());
					switch (pack_type) {
						case xivres::packed::type::model: std::cout << " Model   "; break;
						case xivres::packed::type::standard: std::cout << " Standard"; break;
						case xivres::packed::type::texture: std::cout << " Texture "; break;
					}
					if (!res.empty())
						std::cout << std::format("\n\t=>{}\n", res);
				}
			}
		}
	}

	std::cout << std::endl;
}

static void test_sqpack_generator(const xivres::installation& gameReader) {
	xivres::sqpack::generator generator("ffxiv", "000000");
	const auto& packfile = gameReader.get_sqpack(0x000000);
	for (const auto& entry : packfile.Entries) {
		generator.add(packfile.packed_at(entry));
	}
	generator.export_to_files("Z:/");
}

static void test_ogg_decode_encode(const xivres::installation& gameReader) {
	for (const auto& fileName : {
		"music/ex1/BGM_EX1_Alex01.scd",
		"music/ex1/BGM_EX1_Alex02.scd",
		"music/ex1/BGM_EX1_Alex03.scd",
		"music/ex1/BGM_EX1_Alex04.scd",
		"music/ex1/BGM_EX1_Alex05.scd",
		"music/ex1/BGM_EX1_Alex06.scd",
		"music/ex1/BGM_EX1_Alex07.scd",
		"music/ex1/BGM_EX1_Alex08.scd",
		"music/ex1/BGM_EX1_Alex09.scd",
		}) {
		const auto scdstream = gameReader.get_file(fileName);
		const auto scd = xivres::sound::reader(scdstream);
		const auto& item = scd.read_sound_item(0);
		const auto marks = item.marked_sample_block_indices();
		const auto oggf = item.get_ogg_decoded();

		auto newentry = xivres::sound::writer::sound_item::make_from_ogg_encode(
			oggf.Channels, oggf.SamplingRate, oggf.LoopStartBlockIndex, oggf.LoopEndBlockIndex,
			xivres::memory_stream(std::span(oggf.Data)).as_linear_reader<uint8_t>(),
			[&](size_t blockIndex) { std::cout << std::format("\r{}: Block {} out of {}", fileName, blockIndex, oggf.Data.size() / 4 / oggf.Channels); return true; },
			std::span(marks), 0.f
		);

		auto newscd = xivres::sound::writer();
		newscd.set_table_1(scd.read_table_1());
		newscd.set_table_2(scd.read_table_2());
		newscd.set_table_4(scd.read_table_4());
		newscd.set_table_5(scd.read_table_5());
		newscd.set_sound_item(0, newentry);

		auto scdd2 = newscd.Export();

		const auto newFilePath = std::filesystem::path("Z:/") / (std::filesystem::path(fileName).filename());
		std::cout << std::format("\nExported to {}\n", xivres::util::unicode::convert<std::string>(newFilePath.wstring()));
		std::ofstream(newFilePath, std::ios::binary).write(reinterpret_cast<const char*>(&scdd2[0]), scdd2.size());
	}
}

int main() {
	SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
	system("chcp 65001 > NUL");

	xivres::installation gameReader(R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");

	// test_pack_unpack(gameReader);
	// test_sqpack_generator(gameReader);
	test_ogg_decode_encode(gameReader);

	// const auto pathSpec = SqpackPathSpec("common/font/font1.tex");
	// const auto pathSpec = SqpackPathSpec("common/graphics/texture/-caustics.tex");
	// const auto pathSpec = SqpackPathSpec("common/graphics/texture/-omni_shadow_index_table.tex");
	// internal::ShowTextureStream(TextureStream(decoded));

	return 0;
}
