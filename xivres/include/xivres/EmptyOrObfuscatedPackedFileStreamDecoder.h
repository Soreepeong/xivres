#ifndef _XIVRES_EMPTYOROBFUSCATEDPACKEDFILESTREAMDECODER_H_
#define _XIVRES_EMPTYOROBFUSCATEDPACKEDFILESTREAMDECODER_H_

#include "util.zlib_wrapper.h"

#include "SqpackStreamDecoder.h"
#include "StreamAsPackedFileViewStream.h"

namespace xivres {
	class EmptyOrObfuscatedPackedFileStreamDecoder : public BasePackedFileStreamDecoder {
		std::optional<util::zlib_inflater> m_inflater;
		std::shared_ptr<partial_view_stream> m_partialView;
		std::optional<StreamAsPackedFileViewStream> m_provider;

	public:
		EmptyOrObfuscatedPackedFileStreamDecoder(const xivres::PackedFileHeader& header, std::shared_ptr<const xivres::packed_stream> strm, std::span<uint8_t> headerRewrite = {})
			: BasePackedFileStreamDecoder(std::move(strm)) {

			if (header.DecompressedSize < header.BlockCountOrVersion) {
				auto src = m_stream->read_vector<uint8_t>(header.HeaderSize, header.DecompressedSize);
				if (!headerRewrite.empty())
					std::copy(headerRewrite.begin(), headerRewrite.begin() + (std::min)(headerRewrite.size(), src.size()), src.begin());
				m_inflater.emplace(-MAX_WBITS, header.DecompressedSize);
				m_provider.emplace(m_stream->path_spec(), std::make_shared<xivres::memory_stream>((*m_inflater)(src)));

			} else {
				m_partialView = std::make_shared<xivres::partial_view_stream>(m_stream, header.HeaderSize);
				m_provider.emplace(m_stream->path_spec(), m_partialView);
			}
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) override {
			return m_provider->read(offset, buf, length);
		}
	};
}

#endif
