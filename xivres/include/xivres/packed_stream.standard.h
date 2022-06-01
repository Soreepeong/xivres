#ifndef _XIVRES_BINARYPACKEDFILESTREAM_H_
#define _XIVRES_BINARYPACKEDFILESTREAM_H_

#include "util.zlib_wrapper.h"

#include "packed_stream.h"

namespace xivres {
	class standard_passthrough_packer : public passthrough_packer<packed_type::Binary> {
		std::vector<uint8_t> m_header;
		std::mutex m_mtx;

	public:
		standard_passthrough_packer(std::shared_ptr<const stream> strm);

		[[nodiscard]] std::streamsize size() override;

		void ensure_initialized() override;

		std::streamsize translate_read(std::streamoff offset, void* buf, std::streamsize length) override;
	};

	class standard_compressing_packer : public compressing_packer<packed_type::Binary> {
	public:
		std::unique_ptr<stream> pack(const stream& strm, int compressionLevel) const override;
	};
}

#endif
