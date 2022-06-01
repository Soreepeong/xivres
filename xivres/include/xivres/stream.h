#ifndef _XIVRES_STREAM_H_
#define _XIVRES_STREAM_H_

#include <algorithm>
#include <fstream>
#include <functional>
#include <mutex>
#include <filesystem>
#include <span>
#include <type_traits>

#include "util.byte_order.h"
#include "util.span_cast.h"
#include "util.h"

namespace xivres {
	class partial_view_stream;

	class stream {
	public:
		virtual ~stream() = default;

		[[nodiscard]] virtual std::streamsize size() const = 0;

		[[nodiscard]] virtual std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const = 0;

		virtual std::unique_ptr<stream> substream(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) const = 0;

		void read_fully(std::streamoff offset, void* buf, std::streamsize length) const;

		template<typename T>
		T read_fully(std::streamoff offset) const {
			T buf{};
			read_fully(offset, &buf, sizeof T);
			return buf;
		}

		template<typename T>
		void read_fully(std::streamoff offset, std::span<T> buf) const {
			read_fully(offset, buf.data(), buf.size_bytes());
		}

		template<typename T>
		std::vector<T> read_vector(std::streamoff offset, size_t count, size_t maxCount = SIZE_MAX) const {
			if (count > maxCount)
				throw std::runtime_error("trying to read too many");
			std::vector<T> result(count);
			read_fully(offset, std::span(result));
			return result;
		}

		template<typename T>
		std::vector<T> read_vector(size_t maxCount = SIZE_MAX) const {
			return read_vector<T>(0, (std::min)(maxCount, static_cast<size_t>(size() / sizeof T)));
		}
	};

	class default_base_stream : public stream, public std::enable_shared_from_this<default_base_stream> {
	public:
		default_base_stream() = default;
		default_base_stream(stream&&) = delete;
		default_base_stream(const stream&) = delete;
		stream& operator=(stream&&) = delete;
		stream& operator=(const stream&) = delete;

		std::unique_ptr<stream> substream(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) const override;
	};

	class partial_view_stream : public default_base_stream {
		const std::shared_ptr<const stream> m_streamSharedPtr;

	public:
		const stream& m_stream;
		const std::streamoff m_offset;

	private:
		const std::streamsize m_size;

	public:
		partial_view_stream(const stream& strm, std::streamoff offset = 0, std::streamsize length = (std::numeric_limits<std::streamsize>::max)());
		partial_view_stream(const partial_view_stream&);
		partial_view_stream(std::shared_ptr<const stream> strm, std::streamoff offset = 0, std::streamsize length = (std::numeric_limits<std::streamsize>::max)());

		[[nodiscard]] std::streamsize size() const override;
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;
		std::unique_ptr<stream> substream(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) const override;
	};

	class file_stream : public default_base_stream {
		struct Data;
		std::unique_ptr<Data> m_data;

	public:
		file_stream(std::filesystem::path path);
		~file_stream() override;

		[[nodiscard]] std::streamsize size() const override;
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;
	};

	class memory_stream : public default_base_stream {
		std::vector<uint8_t> m_buffer;
		std::span<uint8_t> m_view;

	public:
		memory_stream() = default;
		memory_stream(memory_stream&& r) noexcept;
		memory_stream(const memory_stream& r);
		memory_stream(const stream& r);
		memory_stream(std::vector<uint8_t> buffer);
		memory_stream(std::span<uint8_t> view);
		memory_stream& operator=(std::vector<uint8_t>&& buf) noexcept;
		memory_stream& operator=(const std::vector<uint8_t>& buf);
		memory_stream& operator=(memory_stream&& r) noexcept;
		memory_stream& operator=(const memory_stream& r);

		[[nodiscard]] std::streamsize size() const override;
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;

		[[nodiscard]] bool owns_data() const;
		std::span<const uint8_t> as_span(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) const;
	};
}

#endif
