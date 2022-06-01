#ifndef _XIVRES_MODELPACKEDFILESTREAM_H_
#define _XIVRES_MODELPACKEDFILESTREAM_H_

#include "util.zlib_wrapper.h"

#include "packed_stream.h"
#include "Model.h"

namespace xivres {
	class model_passthrough_packer : public passthrough_packer<packed_type::model> {
		struct ModelEntryHeader {
			PackedFileHeader Entry;
			SqpackModelPackedFileBlockLocator Model;
		} m_header{};
		std::vector<uint32_t> m_blockOffsets;
		std::vector<uint16_t> m_blockDataSizes;
		std::vector<uint16_t> m_paddedBlockSizes;
		std::vector<uint32_t> m_actualFileOffsets;
		std::mutex m_mtx;

	public:
		model_passthrough_packer(std::shared_ptr<const stream> strm);

		[[nodiscard]] std::streamsize size() override;

		void ensure_initialized() override;

		std::streamsize translate_read(std::streamoff offset, void* buf, std::streamsize length) override;
	};

	class model_compressing_packer : public compressing_packer<packed_type::model> {
	public:
		std::unique_ptr<stream> pack(const stream& strm, int compressionLevel) const override;
	};
}

#endif
