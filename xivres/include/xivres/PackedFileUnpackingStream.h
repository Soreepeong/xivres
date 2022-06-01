#ifndef _XIVRES_PACKEDFILEUNPACKINGSTREAM_H_
#define _XIVRES_PACKEDFILEUNPACKINGSTREAM_H_

#include "packed_stream.h"
#include "SqpackStreamDecoder.h"

namespace xivres {
	class BasePackedFileStreamDecoder;

	class PackedFileUnpackingStream : public default_base_stream {
		const std::shared_ptr<const packed_stream> m_provider;
		const PackedFileHeader m_entryHeader;
		const std::unique_ptr<BasePackedFileStreamDecoder> m_decoder;

	public:
		PackedFileUnpackingStream(std::shared_ptr<const packed_stream> provider, std::span<uint8_t> obfuscatedHeaderRewrite = {})
			: m_provider(std::move(provider))
			, m_entryHeader(m_provider->read_fully<PackedFileHeader>(0))
			, m_decoder(BasePackedFileStreamDecoder::CreateNew(m_entryHeader, m_provider, obfuscatedHeaderRewrite)) {
		}

		[[nodiscard]] std::streamsize size() const override {
			return m_decoder ? *m_entryHeader.DecompressedSize : 0;
		}

		[[nodiscard]] packed_type packed_type() const {
			return m_provider->get_packed_type();
		}

		[[nodiscard]] const xiv_path_spec& path_spec() const {
			return m_provider->path_spec();
		}

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (!m_decoder)
				return 0;

			const auto fullSize = *m_entryHeader.DecompressedSize;
			if (offset >= fullSize)
				return 0;
			if (offset + length > fullSize)
				length = fullSize - offset;

			const auto decompressedSize = *m_entryHeader.DecompressedSize;
			auto read = m_decoder->ReadStreamPartial(offset, buf, length);
			if (read != length)
				std::fill_n(static_cast<char*>(buf) + read, length - read, 0);
			return length;
		}
	};
}

inline xivres::PackedFileUnpackingStream xivres::packed_stream::GetUnpackedStream(std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return xivres::PackedFileUnpackingStream(std::static_pointer_cast<const packed_stream>(shared_from_this()), obfuscatedHeaderRewrite);
}

inline std::unique_ptr<xivres::PackedFileUnpackingStream> xivres::packed_stream::GetUnpackedStreamPtr(std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return std::make_unique<xivres::PackedFileUnpackingStream>(std::static_pointer_cast<const packed_stream>(shared_from_this()), obfuscatedHeaderRewrite);
}

#endif
