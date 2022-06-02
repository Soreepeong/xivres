#ifndef _XIVRES_BINARYPACKEDFILESTREAMDECODER_H_
#define _XIVRES_BINARYPACKEDFILESTREAMDECODER_H_

#include <ranges>

#include "packed_stream.h"
#include "unpacked_stream.h"
#include "sqpack.h"

namespace xivres {
	class standard_unpacker : public base_unpacker {
		const std::vector<packed::standard_block_locator> m_locators;
		const uint32_t m_headerSize;
		std::vector<uint32_t> m_offsets;

	public:
		standard_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm);

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) override;
	};
}

#endif
