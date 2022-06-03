#ifndef XIVRES_TEXTUREPACKEDFILESTREAMDECODER_H_
#define XIVRES_TEXTUREPACKEDFILESTREAMDECODER_H_

#include <mutex>

#include "unpacked_stream.h"

namespace xivres {
	class texture_unpacker : public base_unpacker {
		struct block_info_t {
			uint32_t RequestOffset;
			uint32_t BlockOffset;
			uint32_t RemainingDecompressedSize;
			uint32_t ZeroFillSize;
			std::vector<uint16_t> RemainingBlockSizes;

			bool operator<(uint32_t r) const {
				return RequestOffset < r;
			}
		};

		std::mutex m_mtx;

		std::vector<uint8_t> m_head;
		std::vector<block_info_t> m_blocks;

	public:
		texture_unpacker(const packed::file_header& header, std::shared_ptr<const packed_stream> strm);

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) override;
	};
}

#endif
