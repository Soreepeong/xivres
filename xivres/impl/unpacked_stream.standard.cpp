#include "../include/xivres/unpacked_stream.standard.h"

#include <vector>

xivres::standard_unpacker::standard_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm)
	: base_unpacker(header, std::move(strm))
	, m_headerSize(header.HeaderSize) {

	const auto locators = m_stream->read_vector<packed::standard_block_locator>(sizeof packed::file_header, header.BlockCountOrVersion);
	m_blocks.reserve(locators.size());
	auto requestOffset = 0U;
	for (const auto& locator : locators) {
		auto& block = m_blocks.emplace_back();
		block.RequestOffset = requestOffset;
		block.BlockSize = locator.BlockSize;
		block.BlockOffset = header.HeaderSize + locator.Offset;
		requestOffset += locator.DecompressedDataSize;
	}
}

std::streamsize xivres::standard_unpacker::read(std::streamoff offset, void* buf, std::streamsize length) {
	if (!length || m_blocks.empty())
		return 0;

	block_decoder info(buf, length, offset);
	
	auto it = std::upper_bound(m_blocks.begin(), m_blocks.end(), static_cast<uint32_t>(offset));
	if (it != m_blocks.begin())
		--it;
	for (; it < m_blocks.end() && !info.complete(); ++it) {
		info.skip_to(it->RequestOffset);
		info.forward(*m_stream, it->BlockOffset, it->BlockSize);
	}
	
	info.skip_to(size());
	return info.filled();
}
