#ifndef XIVRES_PACKEDFILESTREAMOPLOCKING_H_
#define XIVRES_PACKEDFILESTREAMOPLOCKING_H_

#include <filesystem>

#include "packed_stream.h"
#include "stream.oplocking.h"

namespace xivres {
	class oplocking_packed_stream : public packed_stream {
		const std::filesystem::path m_path;
		packed::type m_packedType = packed::type::placeholder;

		mutable std::shared_ptr<oplocking_file_stream> m_oplockingStream;
		mutable std::shared_ptr<packed_stream> m_packedStream;

		bool open() const;

	public:
		oplocking_packed_stream(xivres::path_spec pathSpec, std::filesystem::path path);

		[[nodiscard]] packed::type get_packed_type() const override;
		[[nodiscard]] std::streamsize size() const override;
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;

		void close();
	};
}

#endif
