#include "../xivres/xivres/include/xivres/packed_stream.standard.h"
#include "../xivres/xivres/include/xivres/util.thread_pool.h"

xivres::standard_passthrough_packer::standard_passthrough_packer(std::shared_ptr<const stream> strm)
	: passthrough_packer(std::move(strm)) {
}

std::streamsize xivres::standard_passthrough_packer::size() {
	ensure_initialized();
	return reinterpret_cast<const PackedFileHeader*>(&m_header[0])->GetTotalPackedFileSize();
}

void xivres::standard_passthrough_packer::ensure_initialized() {
	if (!m_header.empty())
		return;

	const auto lock = std::lock_guard(m_mtx);

	if (!m_header.empty())
		return;

	const auto size = m_stream->size();
	const auto blockAlignment = Align<uint32_t>(static_cast<uint32_t>(size), EntryBlockDataSize);
	const auto headerAlignment = Align(sizeof PackedFileHeader + blockAlignment.Count * sizeof PackedStandardBlockLocator);

	m_header.resize(headerAlignment.Alloc);
	auto& header = *reinterpret_cast<PackedFileHeader*>(&m_header[0]);
	const auto locators = util::span_cast<PackedStandardBlockLocator>(m_header, sizeof header, blockAlignment.Count);

	header = {
		.HeaderSize = static_cast<uint32_t>(headerAlignment),
		.Type = packed_type::Binary,
		.DecompressedSize = static_cast<uint32_t>(size),
		.BlockCountOrVersion = blockAlignment.Count,
	};
	header.SetSpaceUnits((static_cast<size_t>(blockAlignment.Count) - 1) * EntryBlockSize + sizeof PackedBlockHeader + blockAlignment.Last);

	blockAlignment.IterateChunked([&](uint32_t index, uint32_t offset, uint32_t size) {
		locators[index].Offset = index == 0 ? 0 : locators[index - 1].Offset + locators[index - 1].BlockSize;
		locators[index].BlockSize = static_cast<uint16_t>(Align(sizeof PackedBlockHeader + size));
		locators[index].DecompressedDataSize = static_cast<uint16_t>(size);
	});
}

std::streamsize xivres::standard_passthrough_packer::translate_read(std::streamoff offset, void* buf, std::streamsize length) {
	if (!length)
		return 0;

	const auto& header = *reinterpret_cast<const PackedFileHeader*>(&m_header[0]);

	auto relativeOffset = static_cast<uint64_t>(offset);
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	if (relativeOffset < m_header.size()) {
		const auto src = std::span(m_header).subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= m_header.size();

	const auto blockAlignment = Align<uint32_t>(static_cast<uint32_t>(m_stream->size()), EntryBlockDataSize);
	if (static_cast<uint32_t>(relativeOffset) < header.OccupiedSpaceUnitCount * EntryAlignment) {
		const auto i = relativeOffset / EntryBlockSize;
		relativeOffset -= i * EntryBlockSize;

		blockAlignment.IterateChunkedBreakable([&](uint32_t, uint32_t offset, uint32_t size) {
			if (relativeOffset < sizeof PackedBlockHeader) {
				const auto header = PackedBlockHeader{
					.HeaderSize = sizeof PackedBlockHeader,
					.Version = 0,
					.CompressedSize = PackedBlockHeader::CompressedSizeNotCompressed,
					.DecompressedSize = static_cast<uint32_t>(size),
				};
				const auto src = util::span_cast<uint8_t>(1, &header).subspan(static_cast<size_t>(relativeOffset));
				const auto available = (std::min)(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return false;
			} else
				relativeOffset -= sizeof PackedBlockHeader;

			if (relativeOffset < size) {
				const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(size - relativeOffset));
				m_stream->read_fully(offset + relativeOffset, &out[0], available);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return false;
			} else
				relativeOffset -= size;

			if (const auto pad = Align(sizeof PackedBlockHeader + size).Pad; relativeOffset < pad) {
				const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(pad - relativeOffset));
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(static_cast<size_t>(available));
				relativeOffset = 0;

				if (out.empty()) return false;
			} else
				relativeOffset -= pad;

			return true;
		}, 0, static_cast<uint32_t>(i));
	}

	return length - out.size_bytes();
}

std::unique_ptr<xivres::stream> xivres::standard_compressing_packer::pack(const stream& strm, int compressionLevel) const {
	const auto rawStreamSize = static_cast<uint32_t>(strm.size());

	const auto blockAlignment = Align<uint32_t>(rawStreamSize, EntryBlockDataSize);
	std::vector<std::pair<bool, std::vector<uint8_t>>> blockDataList(blockAlignment.Count);

	{
		util::thread_pool<> threadPool;
		std::vector<std::optional<util::zlib_deflater>> deflaters(threadPool.GetThreadCount());
		std::vector<std::vector<uint8_t>> readBuffers(threadPool.GetThreadCount());

		try {
			blockAlignment.IterateChunkedBreakable([&](const uint32_t index, const uint32_t offset, const uint32_t length) {
				if (is_cancelled())
					return false;

				threadPool.Submit([this, compressionLevel, offset, length, &strm, &readBuffers, &deflaters, &blockData = blockDataList[index]](size_t threadIndex) {
					if (is_cancelled())
						return;

					auto& readBuffer = readBuffers[threadIndex];
					auto& deflater = deflaters[threadIndex];
					if (compressionLevel && !deflater)
						deflater.emplace(compressionLevel, Z_DEFLATED, -15);

					readBuffer.clear();
					readBuffer.resize(length);
					strm.read_fully(offset, std::span(readBuffer));
					if (deflater)
						deflater->deflate(readBuffer);

					if ((blockData.first = deflater && deflater->deflate(std::span(readBuffer)).size() < readBuffer.size()))
						blockData.second = std::move(deflater->result());
					else
						blockData.second = std::move(readBuffer);
				});

				return true;
			});
		} catch (...) {
			// pass
		}
		threadPool.SubmitDoneAndWait();
	}

	if (is_cancelled())
		return nullptr;

	const auto entryHeaderLength = static_cast<uint16_t>(Align(0
		+ sizeof PackedFileHeader
		+ sizeof PackedStandardBlockLocator * blockAlignment.Count
	));
	size_t entryBodyLength = 0;
	for (const auto& blockItem : blockDataList)
		entryBodyLength += Align(sizeof PackedBlockHeader + blockItem.second.size());

	std::vector<uint8_t> result(entryHeaderLength + entryBodyLength);

	auto& entryHeader = *reinterpret_cast<PackedFileHeader*>(&result[0]);
	entryHeader.Type = packed_type::Binary;
	entryHeader.DecompressedSize = rawStreamSize;
	entryHeader.BlockCountOrVersion = static_cast<uint32_t>(blockAlignment.Count);
	entryHeader.HeaderSize = entryHeaderLength;
	entryHeader.SetSpaceUnits(entryBodyLength);

	const auto locators = util::span_cast<PackedStandardBlockLocator>(result, sizeof entryHeader, blockAlignment.Count);
	auto resultDataPtr = result.begin() + entryHeaderLength;

	blockAlignment.IterateChunkedBreakable([&](const uint32_t index, const uint32_t offset, const uint32_t length) {
		if (is_cancelled())
			return false;
		auto& [useCompressed, targetBuf] = blockDataList[index];

		auto& header = *reinterpret_cast<PackedBlockHeader*>(&*resultDataPtr);
		header.HeaderSize = sizeof PackedBlockHeader;
		header.Version = 0;
		header.CompressedSize = useCompressed ? static_cast<uint32_t>(targetBuf.size()) : PackedBlockHeader::CompressedSizeNotCompressed;
		header.DecompressedSize = length;

		std::copy(targetBuf.begin(), targetBuf.end(), resultDataPtr + sizeof header);

		locators[index].Offset = index == 0 ? 0 : locators[index - 1].BlockSize + locators[index - 1].Offset;
		locators[index].BlockSize = static_cast<uint16_t>(Align(sizeof header + targetBuf.size()));
		locators[index].DecompressedDataSize = static_cast<uint16_t>(length);

		resultDataPtr += locators[index].BlockSize;

		std::vector<uint8_t>().swap(targetBuf);
		return true;
	});

	return std::make_unique<memory_stream>(std::move(result));
}
