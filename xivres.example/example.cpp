#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <fstream>
#include <iostream>
#include <Windows.h>
#include <windowsx.h>

#include "xivres/excel.h"
#include "xivres/installation.h"
#include "xivres/packed_stream.model.h"
#include "xivres/packed_stream.standard.h"
#include "xivres/packed_stream.texture.h"
#include "xivres/sound.h"
#include "xivres/sqpack.generator.h"
#include "xivres/texture.preview.h"
#include "xivres/texture.stream.h"
#include "xivres/unpacked_stream.h"
#include "xivres/util.thread_pool.h"
#include "xivres/util.unicode.h"

template<typename TPassthroughPacker, typename TCompressingPacker>
static std::string test_pack_unpack_file(std::shared_ptr<xivres::packed_stream> packed, xivres::path_spec pathSpec) {
	using namespace xivres;

	std::shared_ptr<stream> decoded;
	const char* pcszLastStep = "Read packed original";
	try {
		pcszLastStep = "Decode packed original";
		decoded = std::make_shared<unpacked_stream>(std::make_shared<stream_as_packed_stream>(pathSpec, packed));
		const auto decodedOriginal = decoded->read_vector<uint8_t>();

		pcszLastStep = "Compress-pack decoded original";
		packed = std::make_shared<compressing_packed_stream<TCompressingPacker>>(pathSpec, decoded, Z_BEST_COMPRESSION, true);

		pcszLastStep = "Decode Compress-packed";
		decoded = std::make_shared<unpacked_stream>(std::make_shared<stream_as_packed_stream>(pathSpec, packed));

		pcszLastStep = "Passthrough-pack decoded original";
		packed = std::make_shared<passthrough_packed_stream<TPassthroughPacker>>(pathSpec, decoded);

		pcszLastStep = "Decode Passthrough-packed";
		decoded = std::make_shared<unpacked_stream>(std::make_shared<stream_as_packed_stream>(pathSpec, packed));
		
		const auto decodedPassthrough = decoded->read_vector<uint8_t>();
		if (decodedOriginal.empty() && decodedPassthrough.empty())
			void();
		else if (decodedOriginal.size() != decodedPassthrough.size() || memcmp(&decodedOriginal[0], &decodedPassthrough[0], decodedPassthrough.size()) != 0)
			return "DIFF";

		return {};
	} catch (const std::exception& e) {
		return std::format("{}: {}", pcszLastStep, e.what());
	}
}

static void test_pack_unpack(const xivres::installation& gameReader, bool decodeOnly) {
	struct task_t {
		const xivres::sqpack::reader::entry_info& EntryInfo;
		xivres::packed::type Type;
		std::string Result;
	};

	xivres::util::thread_pool::task_waiter<task_t> waiter;
	uint64_t nextPrintTickCount = 0;

	for (const auto packId : gameReader.get_sqpack_ids()) {
		const auto& packfile = gameReader.get_sqpack(packId);

		for (size_t i = 0;;) {
			for (; waiter.pending() < std::max<size_t>(8, waiter.pool().concurrency()) && i < packfile.Entries.size(); i++) {
				const auto& entry = packfile.Entries[i];

				waiter.submit([i, decodeOnly, &entry, &packfile, pathSpec = entry.PathSpec](auto&) {
					task_t res{.EntryInfo = entry};
					try {
						auto packed = packfile.packed_at(pathSpec);
						if (decodeOnly) {
							auto unpacked = std::make_shared<xivres::unpacked_stream>(packed);
							unpacked->read_vector<char>();
							return res;
						} else {
							switch (res.Type = packed->get_packed_type()) {
								case xivres::packed::type::none: break;
								case xivres::packed::type::placeholder: break;
								case xivres::packed::type::standard: res.Result = test_pack_unpack_file<xivres::standard_passthrough_packer, xivres::standard_compressing_packer>(std::move(packed), pathSpec); break;
								case xivres::packed::type::model: res.Result = test_pack_unpack_file<xivres::model_passthrough_packer, xivres::model_compressing_packer>(std::move(packed), pathSpec); break;
								case xivres::packed::type::texture: res.Result = test_pack_unpack_file<xivres::texture_passthrough_packer, xivres::texture_compressing_packer>(std::move(packed), pathSpec); break;
								default: break;
							}
						}
					} catch (const std::out_of_range& e) {
						res.Result = e.what();
					}
					return res;
				});
			}

			const auto r = waiter.get();
			if (!r)
				break;

			const auto& entry = r->EntryInfo;

			if (!r->Result.empty() || GetTickCount64() > nextPrintTickCount) {
				nextPrintTickCount = GetTickCount64() + 200;
				std::cout << std::format("\r[{:0>6X}:{:0>6}/{:0>6} {:08x}/{:08x}={:08x}]", packId, i, packfile.Entries.size(), entry.PathSpec.path_hash(), entry.PathSpec.name_hash(), entry.PathSpec.full_path_hash());
				switch (r->Type) {
					case xivres::packed::type::model: std::cout << " Model   ";
						break;
					case xivres::packed::type::standard: std::cout << " Standard";
						break;
					case xivres::packed::type::texture: std::cout << " Texture ";
						break;
				}
				if (!r->Result.empty())
					std::cout << std::format("\n\t=>{}", r->Result) << std::endl;
			}
		}
	}

	std::cout << std::endl;
}

static std::shared_ptr<xivres::stream> make_ogg_crispy(std::shared_ptr<xivres::stream> scdstream) {
	const auto scd = xivres::sound::reader(scdstream);
	const auto& item = scd.read_sound_item(0);
	const auto marks = item.marked_sample_block_indices();
	const auto oggf = item.get_ogg_decoded();

	auto newentry = xivres::sound::writer::sound_item::make_from_ogg_encode(
		oggf.Channels, oggf.SamplingRate, oggf.LoopStartBlockIndex, oggf.LoopEndBlockIndex,
		xivres::memory_stream(std::span(oggf.Data)).as_linear_reader<uint8_t>(),
		[&](size_t blockIndex) {
			std::cout << std::format("\rBlock {} out of {}", blockIndex, oggf.Data.size() / 4 / oggf.Channels);
			return true;
		},
		std::span(marks), 0.f
	);

	auto newscd = xivres::sound::writer();
	newscd.set_table_1(scd.read_table_1());
	newscd.set_table_2(scd.read_table_2());
	newscd.set_table_4(scd.read_table_4());
	newscd.set_table_5(scd.read_table_5());
	newscd.set_sound_item(0, newentry);

	return std::make_shared<xivres::memory_stream>(newscd.Export());
}

static void test_sqpack_generator(const xivres::installation& gameReader) {
	auto sqpackIds = gameReader.get_sqpack_ids();
	// std::ranges::sort(sqpackIds, [&gameReader](const uint32_t l, const uint32_t r) { return gameReader.get_sqpack(l).TotalDataSize > gameReader.get_sqpack(r).TotalDataSize; });
	xivres::util::thread_pool::task_waiter waiter;
	for (const auto p : sqpackIds) {
		waiter.submit([&gameReader, p](auto&) {
			try {
				const auto& packfile = gameReader.get_sqpack(p);
				xivres::sqpack::generator generator(((p >> 8) & 0xff) ? std::format("ex{}", (p >> 8) & 0xff) : "ffxiv", std::format("{:0>6x}", p), xivres::sqdata::header::MaxFileSize_Value * 4);
				std::cout << std::format("Working on {:06x} ({} entries)", p, packfile.Entries.size()) << std::endl;
				for (size_t i = 0; i < packfile.Entries.size(); i++) {
					if (i % 1000 == 0)
						std::cout << std::format("Read: {:06x}: {}/{}", p, i, packfile.Entries.size()) << std::endl;
					const auto& entry = packfile.Entries[i];
					try {
						auto packed = packfile.packed_at(entry);
						std::shared_ptr<xivres::stream> unpacked = std::make_shared<xivres::unpacked_stream>(packed);
						switch (packed->get_packed_type()) {
							case xivres::packed::type::standard:
								packed = std::make_shared<xivres::compressing_packed_stream<xivres::standard_compressing_packer>>(entry.PathSpec, std::move(unpacked), Z_BEST_COMPRESSION, 1);
								break;
							case xivres::packed::type::model:
								packed = std::make_shared<xivres::compressing_packed_stream<xivres::model_compressing_packer>>(entry.PathSpec, std::move(unpacked), Z_BEST_COMPRESSION, 1);
								break;
							case xivres::packed::type::texture:
								packed = std::make_shared<xivres::compressing_packed_stream<xivres::texture_compressing_packer>>(entry.PathSpec, std::move(unpacked), Z_BEST_COMPRESSION, 1);
								break;
							default: break;
						}
						generator.add(packed);
					} catch (const std::out_of_range&) {
						// pass
					}
				}
				const auto dir = std::filesystem::path(std::format("C:/ffxiv/game/sqpack/{}", generator.DatExpac));
				create_directories(dir);
				const auto callbackHolder = generator.ProgressCallback([p](size_t progress, size_t progressMax) {
					if (progress % 1000 == 0)
						std::cout << std::format("Export: {:06x}: {}/{}", p, progress, progressMax) << std::endl;
				});
				generator.export_to_files(dir);
				std::cout << std::format("Complete: {:06x}", p) << std::endl;
			} catch (const std::exception& e) {
				std::cout << std::format("Error: {:06x}: {}", p, e.what()) << std::endl;
			}
		});
	}
	waiter.wait_all();
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
		auto scdd2 = make_ogg_crispy(scdstream)->read_vector<char>();
		const auto newFilePath = std::filesystem::path("Z:/") / (std::filesystem::path(fileName).filename());
		std::cout << std::format("\nExported to {}\n", xivres::util::unicode::convert<std::string>(newFilePath.wstring()));
		std::ofstream(newFilePath, std::ios::binary).write(&scdd2[0], scdd2.size());
	}
}

static void test_excel(const xivres::installation& gameReader) {
	auto sheet = gameReader.get_excel("Addon").new_with_language(xivres::game_language::English);
	for (auto i = 0; i < sheet.get_exh_reader().get_pages().size(); i++) {
		for (const auto& row : sheet.get_exd_reader(i)) {
			for (const auto& subrow : row) {
				std::cout << std::format("{}: {}\n", row.row_id(), subrow[0].String.repr());
			}
		}
	}
}

void test_range_read(const xivres::installation& gameReader) {
	std::vector<xivres::path_spec> specs;
	// specs.emplace_back("common/graphics/texture/-omni_shadow_index_table.tex");
	// specs.emplace_back(0x602a1840, 0xf2925549, 0x85c56562, 0x01, 0x00, 0x00);
	// specs.emplace_back(0x2428c8c9, 0xcff27cf9, 0xd971bea2, 0x01, 0x00, 0x00);
	// specs.emplace_back(0xb9ae4029, 0x2049ae1c, 0x01c3d0ca, 0x01, 0x00, 0x00);
	// specs.emplace_back(0x210fc65d, 0x1442c7c8, 0x0a303bca, 0x02, 0x00, 0x00);
	// specs.emplace_back(0x210fc65d, 0x1442c7c8, 0x0a303bca, 0x02, 0x00, 0x00);
	// specs.emplace_back(0xa59934f6, 0x83234281, 0xdadc46dd, 0x02, 0x00, 0x00);
	// specs.emplace_back(0xdbe71b5b, 0xbaa24aef, 0x4a506bf6, 0x02, 0x03, 0x01);
	// for (const auto& p : gameReader.get_sqpack(0x040000).Entries)
	// 	if (p.PathSpec.path_hash() != 0xffffffff)
	// 		specs.emplace_back(p.PathSpec);
	// specs.emplace_back(0x07e929a2, 0x52438a44, 0xc3e25beb, 0x02, 0x00, 0x00);
	// specs.emplace_back(0x07e929a2, 0xdaafee72, 0x9c57f1da, 0x02, 0x00, 0x00);
	// specs.emplace_back("bg/ffxiv/air_a1/evt/a1e2/bgparts/_a1e2_t1_vfog1.mdl");
	specs.emplace_back(0x8813b068, 0xfd63781b, 0x5d4fda62, 0x01, 0x00, 0x00);

	xivres::util::thread_pool::task_waiter waiter;
	xivres::util::thread_pool::object_pool<std::vector<char>> bufs;

	for (const auto& spec : specs) {
		waiter.submit([&gameReader, &spec, &bufs](auto&) {
			auto pooledBuf = *bufs;
			if (!pooledBuf)
				pooledBuf.emplace(1048576);
			auto& buf = *pooledBuf;

			const auto packed0 = gameReader.get_file_packed(spec);
			if (packed0->get_packed_type() != xivres::packed::type::model)
				return;

			const auto packeddata0 = packed0->read_vector<char>();
			const auto decoded0 = std::make_shared<xivres::unpacked_stream>(packed0);
			const auto data0 = decoded0->read_vector<char>();
			const auto packed1 = std::make_shared<xivres::passthrough_packed_stream<xivres::model_passthrough_packer>>(spec, decoded0);
			const auto packeddata1 = packed1->read_vector<char>();
			const auto decoded1 = std::make_shared<xivres::unpacked_stream>(packed1);
			const auto data1 = decoded1->read_vector<char>();
			const auto data11 = decoded1->read_vector<char>();
			const auto packed2 = std::make_shared<xivres::compressing_packed_stream<xivres::model_compressing_packer>>(spec, decoded0, Z_BEST_COMPRESSION);
			const auto packeddata2 = packed2->read_vector<char>();
			const auto decoded2 = std::make_shared<xivres::unpacked_stream>(packed2);
			const auto data2 = decoded2->read_vector<char>();
			const auto data22 = decoded2->read_vector<char>();
			if (const auto m = std::ranges::mismatch(data1, data0).in1 - data1.begin(); m != data0.size()) {
				__debugbreak();
				__debugbreak();
			}
			if (const auto m = std::ranges::mismatch(data1, data11).in1 - data1.begin(); m != data0.size()) {
				__debugbreak();
				__debugbreak();
			}
			if (const auto m = std::ranges::mismatch(data2, data0).in1 - data2.begin(); m != data0.size()) {
				__debugbreak();
				__debugbreak();
			}
			if (const auto m = std::ranges::mismatch(data2, data22).in1 - data2.begin(); m != data0.size()) {
				__debugbreak();
				__debugbreak();
			}

			xivres::align<uint64_t>(decoded0->size(), buf.size()).iterate_chunks([&](uint64_t, uint64_t offset, uint64_t size) {
				const auto bufSpan = std::span(buf).subspan(0, static_cast<size_t>(size));
				decoded0->read_fully(static_cast<std::streamoff>(offset), bufSpan);
				if (const auto m = std::ranges::mismatch(bufSpan, std::span(data0).subspan(offset, size)).in1 - bufSpan.begin(); m != bufSpan.size()) {
					__debugbreak();
					__debugbreak();
				}
			});
			xivres::align<uint64_t>(decoded1->size(), buf.size()).iterate_chunks([&](uint64_t, uint64_t offset, uint64_t size) {
				const auto bufSpan = std::span(buf).subspan(0, static_cast<size_t>(size));
				decoded1->read_fully(static_cast<std::streamoff>(offset), bufSpan);
				if (const auto m = std::ranges::mismatch(bufSpan, std::span(data1).subspan(offset, size)).in1 - bufSpan.begin(); m != bufSpan.size()) {
					__debugbreak();
					__debugbreak();
				}
			});
			xivres::align<uint64_t>(decoded2->size(), buf.size()).iterate_chunks([&](uint64_t, uint64_t offset, uint64_t size) {
				const auto bufSpan = std::span(buf).subspan(0, static_cast<size_t>(size));
				decoded2->read_fully(static_cast<std::streamoff>(offset), bufSpan);
				if (const auto m = std::ranges::mismatch(bufSpan, std::span(data2).subspan(offset, size)).in1 - bufSpan.begin(); m != bufSpan.size()) {
					__debugbreak();
					__debugbreak();
				}
			});

			// std::thread t1([&]() { preview(xivres::texture::stream(decoded0), L"Source"); });
			// std::thread t2([&]() { preview(xivres::texture::stream(decoded1), L"Dec1"); });
			// std::thread t3([&]() { preview(xivres::texture::stream(decoded2), L"Dec2"); });
			// t1.join();
			// t2.join();
			// t3.join();
		});
	}

	waiter.wait_all();
}

int main() {
	const auto tend = GetTickCount64();
	try {
		SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
		system("chcp 65001 > NUL");

		xivres::installation gameReader(R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");

		// xivres::installation gameReader(R"(Z:\XIV\JP\game)");

		// preview(xivres::texture::stream(gameReader.get_file("common/graphics/texture/-omni_shadow_index_table.tex")));
		// preview(xivres::texture::stream(gameReader.get_file("ui/uld/Title_Logo300.tex")));

		// test_range_read(gameReader);
		test_pack_unpack(gameReader, true);
		// test_sqpack_generator(gameReader);
		// test_ogg_decode_encode(gameReader);
		// test_excel(gameReader);

		std::cout << "Success: took " << (GetTickCount64() - tend) << "ms" << std::endl;
	} catch (const std::exception& e) {
		std::cout << e.what() << std::endl;
		throw;
	}
	return 0;
}
