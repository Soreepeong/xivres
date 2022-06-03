#include "../include/xivres/unpacked_stream.texture.h"
#include "../include/xivres/texture.h"

xivres::texture_unpacker::texture_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm)
	: base_unpacker(std::move(strm)) {
	uint64_t readOffset = sizeof packed::file_header;
	const auto locators = m_stream->read_vector<packed::mipmap_block_locator>(readOffset, header.BlockCountOrVersion);
	readOffset += std::span(locators).size_bytes();

	m_head = m_stream->read_vector<uint8_t>(header.HeaderSize, locators[0].CompressedOffset);

	const auto& texHeader = *reinterpret_cast<const texture::header*>(&m_head[0]);
	const auto mipmapOffsets = util::span_cast<uint32_t>(m_head, sizeof texHeader, texHeader.MipmapCount);

	const auto repeatCount = ((mipmapOffsets.size() < 2 ? static_cast<size_t>(header.DecompressedSize) : mipmapOffsets[1]) - mipmapOffsets[0]) / calc_raw_data_length(texHeader, 0);

	m_blocks.reserve(locators.size());
	for (uint32_t i = 0; i < locators.size(); ++i) {
		const auto& locator = locators[i];
		const auto mipmapIndex = i / repeatCount;
		const auto repeatIndex = i % repeatCount;
		const auto mipmapSize = static_cast<uint32_t>(calc_raw_data_length(texHeader, mipmapIndex));

		uint32_t baseRequestOffset = 0;
		if (mipmapIndex < mipmapOffsets.size())
			baseRequestOffset = static_cast<uint32_t>(mipmapOffsets[mipmapIndex] - mipmapOffsets[0] + mipmapSize * repeatIndex);
		else if (!m_blocks.empty()) {
			// This should not happen, but just in case
			baseRequestOffset = m_blocks.back().RequestOffset + m_blocks.back().RemainingDecompressedSize + m_blocks.back().ZeroFillSize;
		}
		m_blocks.emplace_back();
		m_blocks.back().RequestOffset = baseRequestOffset;
		m_blocks.back().BlockOffset = header.HeaderSize + locator.CompressedOffset;
		m_blocks.back().RemainingDecompressedSize = locator.DecompressedSize;
		m_blocks.back().RemainingBlockSizes = m_stream->read_vector<uint16_t>(readOffset, locator.BlockCount);
		m_blocks.back().ZeroFillSize = mipmapSize - locator.DecompressedSize;
		readOffset += std::span(m_blocks.back().RemainingBlockSizes).size_bytes();
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

	info.skip(m_blocks[i].RequestOffset);
	for (; i < m_blocks.size() && !info.complete(); i++) {
		auto& block = m_blocks[i];
		info.forward(block.RequestOffset, *m_stream, block.BlockOffset, block.RemainingBlockSizes.empty() ? 16384 : block.RemainingBlockSizes.front());

		if (block.RemainingBlockSizes.empty()) {
			info.forward_zerofill(block.ZeroFillSize);
			continue;
		}

		const auto& blockHeader = info.block_header();
		block_info_t newBlockInfo = block;
		newBlockInfo.RequestOffset = block.RequestOffset + blockHeader.DecompressedSize;
		newBlockInfo.BlockOffset = block.BlockOffset + block.RemainingBlockSizes.front();
		newBlockInfo.RemainingDecompressedSize = block.RemainingDecompressedSize - blockHeader.DecompressedSize;
		newBlockInfo.RemainingBlockSizes = std::move(block.RemainingBlockSizes);

		if (i + 1 >= m_blocks.size() || (m_blocks[i + 1].RequestOffset != newBlockInfo.RequestOffset && m_blocks[i + 1].BlockOffset != newBlockInfo.BlockOffset)) {
			newBlockInfo.RemainingBlockSizes.erase(newBlockInfo.RemainingBlockSizes.begin());
			if (newBlockInfo.RemainingDecompressedSize) {
				block.ZeroFillSize = 0;
				m_blocks.emplace(m_blocks.begin() + i + 1, std::move(newBlockInfo));
			} else {
				info.forward_zerofill(block.ZeroFillSize);
			}
		}
	}

	return info.filled();
}
