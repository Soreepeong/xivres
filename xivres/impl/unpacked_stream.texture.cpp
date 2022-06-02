#include "../include/xivres/unpacked_stream.texture.h"
#include "../include/xivres/Texture.h"

xivres::texture_unpacker::texture_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm) : base_unpacker(std::move(strm)) {
	uint64_t readOffset = sizeof packed::file_header;
	const auto locators = m_stream->read_vector<packed::mipmap_block_locator>(readOffset, header.BlockCountOrVersion);
	readOffset += std::span(locators).size_bytes();

	m_head = m_stream->read_vector<uint8_t>(header.HeaderSize, locators[0].CompressedOffset);

	const auto& texHeader = *reinterpret_cast<const TextureHeader*>(&m_head[0]);
	const auto mipmapOffsets = util::span_cast<uint32_t>(m_head, sizeof texHeader, texHeader.MipmapCount);

	const auto repeatCount = mipmapOffsets.size() < 2 ? 1 : (mipmapOffsets[1] - mipmapOffsets[0]) / static_cast<uint32_t>(TextureRawDataLength(texHeader, 0));

	for (uint32_t i = 0; i < locators.size(); ++i) {
		const auto& locator = locators[i];
		const auto mipmapIndex = i / repeatCount;
		const auto mipmapPlaneIndex = i % repeatCount;
		const auto mipmapPlaneSize = static_cast<uint32_t>(TextureRawDataLength(texHeader, mipmapIndex));
		uint32_t baseRequestOffset = 0;
		if (mipmapIndex < mipmapOffsets.size())
			baseRequestOffset = mipmapOffsets[mipmapIndex] - mipmapOffsets[0] + mipmapPlaneSize * mipmapPlaneIndex;
		else if (!m_blocks.empty())
			baseRequestOffset = m_blocks.back().RequestOffset + m_blocks.back().RemainingDecompressedSize;
		else
			baseRequestOffset = 0;
		m_blocks.emplace_back(BlockInfo{
			.RequestOffset = baseRequestOffset,
			.BlockOffset = header.HeaderSize + locator.CompressedOffset,
			.RemainingDecompressedSize = locator.DecompressedSize,
			.RemainingBlockSizes = m_stream->read_vector<uint16_t>(readOffset, locator.BlockCount),
			});
		readOffset += std::span(m_blocks.back().RemainingBlockSizes).size_bytes();
		baseRequestOffset += mipmapPlaneSize;
	}
}

std::streamsize xivres::texture_unpacker::read(std::streamoff offset, void* buf, std::streamsize length) {
	if (!length)
		return 0;

	block_decoder info(buf, length, offset);
	info.forward(m_head);
	if (info.complete() || m_blocks.empty())
		return info.filled();

	const auto lock = std::lock_guard(m_mtx);
	const auto streamSize = m_blocks.back().RequestOffset + m_blocks.back().RemainingDecompressedSize;
	if (info.relative_offset() >= streamSize)
		return 0;

	size_t i = std::lower_bound(m_blocks.begin(), m_blocks.end(), static_cast<uint32_t>(info.relative_offset())) - m_blocks.begin();
	if (i != 0 && (i == m_blocks.size() || info.relative_offset() < m_blocks[i].RequestOffset))
		i--;

	for (; i < m_blocks.size() && !info.complete(); i++) {
		auto& block = m_blocks[i];
		info.forward(block.RequestOffset, *m_stream, block.BlockOffset, block.RemainingBlockSizes.empty() ? 16384 : block.RemainingBlockSizes.front());

		if (block.RemainingBlockSizes.empty())
			continue;

		const auto& blockHeader = info.block_header();
		auto newBlockInfo = BlockInfo{
			.RequestOffset = block.RequestOffset + blockHeader.DecompressedSize,
			.BlockOffset = block.BlockOffset + block.RemainingBlockSizes.front(),
			.RemainingDecompressedSize = block.RemainingDecompressedSize - blockHeader.DecompressedSize,
			.RemainingBlockSizes = std::move(block.RemainingBlockSizes),
		};

		if (i + 1 >= m_blocks.size() || (m_blocks[i + 1].RequestOffset != newBlockInfo.RequestOffset && m_blocks[i + 1].BlockOffset != newBlockInfo.BlockOffset)) {
			newBlockInfo.RemainingBlockSizes.erase(newBlockInfo.RemainingBlockSizes.begin());
			m_blocks.emplace(m_blocks.begin() + i + 1, std::move(newBlockInfo));
		}
	}

	return info.filled();
}
