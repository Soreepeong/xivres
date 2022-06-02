#ifndef _XIVRES_MODELPACKEDFILESTREAMDECODER_H_
#define _XIVRES_MODELPACKEDFILESTREAMDECODER_H_

#include "unpacked_stream.h"

namespace xivres {
	class model_unpacker : public base_unpacker {
		struct BlockInfo {
			uint32_t RequestOffset;
			uint32_t BlockOffset;
			uint16_t PaddedChunkSize;
			uint16_t DecompressedSize;
			uint16_t GroupIndex;
			uint16_t GroupBlockIndex;
		};

		std::vector<uint8_t> m_head;
		std::vector<BlockInfo> m_blocks;

	public:
		model_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm);

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) override;
	};
}

#endif
