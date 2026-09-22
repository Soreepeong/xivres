#ifndef XIVRES_STREAMOPLOCKING_H_
#define XIVRES_STREAMOPLOCKING_H_

#include <filesystem>
#include <memory>

#include "stream.h"

namespace xivres {
	class oplocking_file_stream : public default_base_stream {
		struct data;
		std::unique_ptr<data> m_data;

	public:
		oplocking_file_stream(std::filesystem::path path, bool reopenOnChange);
		oplocking_file_stream(oplocking_file_stream&&) = delete;
		oplocking_file_stream(const oplocking_file_stream&) = delete;
		oplocking_file_stream& operator=(oplocking_file_stream&&) = delete;
		oplocking_file_stream& operator=(const oplocking_file_stream&) = delete;
		~oplocking_file_stream() override;

		[[nodiscard]] const std::filesystem::path& path() const;

		void open() const;
		void invalidate();
		[[nodiscard]] bool done() const;

		[[nodiscard]] std::streamsize size() const override;
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;
	};
}

#endif
