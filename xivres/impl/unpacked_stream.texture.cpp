#include "../include/xivres/unpacked_stream.texture.h"

xivres::texture_unpacker::texture_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm)
	: base_unpacker(header, std::move(strm)) {
	uint64_t readOffset = sizeof packed::file_header;
	const auto locators = m_stream->read_vector<packed::mipmap_block_locator>(readOffset, header.BlockCountOrVersion);
	readOffset += std::span(locators).size_bytes();

	m_head = m_stream->read_vector<uint8_t>(header.HeaderSize, locators[0].CompressedOffset);

	m_blocks.reserve(locators.size());
	for (uint32_t i = 0; i < locators.size(); ++i) {
		const auto& locator = locators[i];

		const auto baseRequestOffset = m_blocks.empty() ? static_cast<uint32_t>(m_head.size()) : m_blocks.back().request_offset_end();
		const auto blockSizes = m_stream->read_vector<uint16_t>(readOffset, locator.BlockCount);
		
		auto& block = m_blocks.emplace_back();
		block.DecompressedSize = locator.DecompressedSize;
		block.Subblocks.reserve(blockSizes.size());
		for (const auto& blockSize : blockSizes) {
			auto& subblock = block.Subblocks.emplace_back();
			subblock.BlockSize = blockSize;
			subblock.RequestOffset = subblock.BlockOffset = (std::numeric_limits<uint32_t>::max)();
		}
		block.Subblocks.front().RequestOffset = baseRequestOffset;
		block.Subblocks.front().BlockOffset = header.HeaderSize + locator.CompressedOffset;
		readOffset += std::span(blockSizes).size_bytes();
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
	const auto streamSize = m_blocks.back().request_offset_end();
	if (info.current_offset() >= streamSize)
		return 0;

	auto i = std::upper_bound(m_blocks.begin(), m_blocks.end(), info.current_offset());
	if (i != m_blocks.begin())
		--i;

	for (; i != m_blocks.end() && !info.complete(); ++i) {
		auto j = std::upper_bound(i->Subblocks.begin(), i->Subblocks.end(), info.current_offset());
		if (j != i->Subblocks.begin())
			--j;

		while (j != i->Subblocks.end() && !info.complete()) {
			if (!*j)
				throw std::runtime_error("Offset error");
			
			if (info.skip_to(j->RequestOffset))
				break;
			info.forward(*m_stream, j->BlockOffset, j->BlockSize);
			j->DecompressedSize = info.block_header().DecompressedSize;

			auto prev = j++;
			if (j == i->Subblocks.end()) {
				info.skip_to(i->request_offset_end());
				break;
			}
			j->RequestOffset = prev->RequestOffset + prev->DecompressedSize;
			j->BlockOffset = prev->BlockOffset + prev->BlockSize;
		}
	}

	info.skip_to(size());
	return info.filled();
}
