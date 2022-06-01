#ifndef _XIVRES_STREAMASPACKEDFILESTREAM_H_
#define _XIVRES_STREAMASPACKEDFILESTREAM_H_

#include "packed_stream.h"

namespace xivres {
	class StreamAsPackedFileViewStream : public packed_stream {
		const std::shared_ptr<const stream> m_stream;

		mutable std::optional<packed_type> m_entryType;

	public:
		StreamAsPackedFileViewStream(xiv_path_spec pathSpec, std::shared_ptr<const stream> strm)
			: packed_stream(std::move(pathSpec))
			, m_stream(std::move(strm)) {
		}

		[[nodiscard]] std::streamsize size() const override {
			return m_stream->size();
		}

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override {
			return m_stream->read(offset, buf, length);
		}

		[[nodiscard]] packed_type get_packed_type() const override {
			if (!m_entryType) {
				// operation that should be lightweight enough that lock should not be needed
				m_entryType = m_stream->read_fully<PackedFileHeader>(0).Type;
			}
			return *m_entryType;
		}
	};
}

#endif
