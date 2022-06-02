#include "../include/xivres/unpacked_stream.standard.h"

#include <vector>

xivres::standard_unpacker::standard_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm)
	: base_unpacker(std::move(strm))
	, m_headerSize(header.HeaderSize)
	, m_locators(m_stream->read_vector<packed::standard_block_locator>(sizeof packed::file_header, header.BlockCountOrVersion)) {

	// +1 is intentional, to indicate that lower_bound resulting in the last entry would 100% mean that the read has been requested past the file size.
	m_offsets.resize(m_locators.size() + 1);
	for (size_t i = 1; i < m_offsets.size(); ++i)
		m_offsets[i] = m_offsets[i - 1] + m_locators[i - 1].DecompressedDataSize;
}

std::streamsize xivres::standard_unpacker::read(std::streamoff offset, void* buf, std::streamsize length) {
	if (!length || m_offsets.empty())
		return 0;

	auto from = static_cast<size_t>(std::distance(m_offsets.begin(), std::ranges::lower_bound(m_offsets, static_cast<uint32_t>(offset))));
	if (from == m_offsets.size())
		return 0;
	if (m_offsets[from] > offset)
		from -= 1;

	block_decoder info(buf, length, offset);
	info.forward(m_offsets[from]);
	for (auto it = from; it < m_locators.size() && !info.complete(); ++it)
		info.forward(m_offsets[it], *m_stream, m_headerSize + m_locators[it].Offset, m_locators[it].BlockSize);
	return info.filled();
}
