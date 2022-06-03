#include "../include/xivres/unpacked_stream.model.h"
#include "../include/xivres/model.h"

xivres::model_unpacker::model_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm)
	: base_unpacker(std::move(strm)) {
	const auto AsHeader = [this]() -> model::header& { return *reinterpret_cast<model::header*>(&m_head[0]); };

	const auto underlyingSize = m_stream->size();
	uint64_t readOffset = sizeof packed::file_header;
	const auto locator = m_stream->read_fully<packed::model_block_locator>(static_cast<std::streamoff>(readOffset));
	const auto blockCount = static_cast<size_t>(locator.FirstBlockIndices.Index[2]) + locator.BlockCount.Index[2];

	readOffset += sizeof locator;
	for (const auto blockSize : m_stream->read_vector<uint16_t>(readOffset, blockCount)) {
		m_blocks.emplace_back(BlockInfo{
			.RequestOffset = 0,
			.BlockOffset = m_blocks.empty() ? *header.HeaderSize : m_blocks.back().BlockOffset + m_blocks.back().PaddedChunkSize,
			.PaddedChunkSize = blockSize,
			.GroupIndex = UINT16_MAX,
			.GroupBlockIndex = 0,
		});
	}

	m_head.resize(sizeof model::header);
	AsHeader() = {
		.Version = header.BlockCountOrVersion,
		.VertexDeclarationCount = locator.VertexDeclarationCount,
		.MaterialCount = locator.MaterialCount,
		.LodCount = locator.LodCount,
		.EnableIndexBufferStreaming = locator.EnableIndexBufferStreaming,
		.EnableEdgeGeometry = locator.EnableEdgeGeometry,
		.Padding = locator.Padding,
	};

	if (m_blocks.empty())
		return;

	for (uint16_t i = 0; i < 11; ++i) {
		if (!locator.BlockCount.at(i))
			continue;

		const size_t blockIndex = *locator.FirstBlockIndices.at(i);
		auto& firstBlock = m_blocks[blockIndex];
		firstBlock.GroupIndex = i;
		firstBlock.GroupBlockIndex = 0;

		for (uint16_t j = 1, j_ = locator.BlockCount.at(i); j < j_; ++j) {
			if (blockIndex + j >= blockCount)
				throw bad_data_error("Out of bounds index information detected");

			auto& block = m_blocks[blockIndex + j];
			if (block.GroupIndex != UINT16_MAX)
				throw bad_data_error("Overlapping index information detected");
			block.GroupIndex = i;
			block.GroupBlockIndex = j;
		}
	}

	auto lastOffset = 0;
	for (auto& block : m_blocks) {
		packed::block_header blockHeader;

		if (block.BlockOffset == underlyingSize)
			blockHeader.DecompressedSize = blockHeader.CompressedSize = 0;
		else
			m_stream->read_fully(block.BlockOffset, &blockHeader, sizeof blockHeader);

		block.DecompressedSize = static_cast<uint16_t>(blockHeader.DecompressedSize);
		block.RequestOffset = lastOffset;
		lastOffset += block.DecompressedSize;
	}

	for (size_t blkI = locator.FirstBlockIndices.Stack, i_ = blkI + locator.BlockCount.Stack; blkI < i_; ++blkI)
		AsHeader().StackSize += m_blocks[blkI].DecompressedSize;
	for (size_t blkI = locator.FirstBlockIndices.Runtime, i_ = blkI + locator.BlockCount.Runtime; blkI < i_; ++blkI)
		AsHeader().RuntimeSize += m_blocks[blkI].DecompressedSize;
	for (size_t lodI = 0; lodI < 3; ++lodI) {
		for (size_t blkI = locator.FirstBlockIndices.Vertex[lodI], i_ = blkI + locator.BlockCount.Vertex[lodI]; blkI < i_; ++blkI)
			AsHeader().VertexSize[lodI] += m_blocks[blkI].DecompressedSize;
		for (size_t blkI = locator.FirstBlockIndices.Index[lodI], i_ = blkI + locator.BlockCount.Index[lodI]; blkI < i_; ++blkI)
			AsHeader().IndexSize[lodI] += m_blocks[blkI].DecompressedSize;
		AsHeader().VertexOffset[lodI] = static_cast<uint32_t>(m_head.size() + (locator.FirstBlockIndices.Vertex[lodI] == m_blocks.size() ? lastOffset : m_blocks[locator.FirstBlockIndices.Vertex[lodI]].RequestOffset));
		AsHeader().IndexOffset[lodI] = static_cast<uint32_t>(m_head.size() + (locator.FirstBlockIndices.Index[lodI] == m_blocks.size() ? lastOffset : m_blocks[locator.FirstBlockIndices.Index[lodI]].RequestOffset));
	}
}

std::streamsize xivres::model_unpacker::read(std::streamoff offset, void* buf, std::streamsize length) {
	if (!length)
		return 0;

	block_decoder info(buf, length, offset);
	info.forward(m_head);
	if (info.complete() || m_blocks.empty())
		return info.filled();

	auto it = std::lower_bound(m_blocks.begin(), m_blocks.end(), static_cast<uint32_t>(info.relative_offset()), [&](const BlockInfo& l, uint32_t r) {
		return l.RequestOffset < r;
	});
	if (it == m_blocks.end() || (it != m_blocks.end() && it != m_blocks.begin() && it->RequestOffset > info.relative_offset()))
		--it;

	info.skip(it->RequestOffset);
	for (; it != m_blocks.end() && !info.complete(); ++it)
		info.forward(it->RequestOffset, *m_stream, it->BlockOffset, it->PaddedChunkSize);
	return info.filled();
}
