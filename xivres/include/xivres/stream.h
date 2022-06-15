#ifndef XIVRES_STREAM_H_
#define XIVRES_STREAM_H_

#include <algorithm>
#include <filesystem>
#include <functional>
#include <mutex>
#include <span>

#include "util.span_cast.h"

namespace xivres {
	class partial_view_stream;

	template<typename T>
	using linear_reader = std::function<std::span<T>(size_t len, bool throwOnIncompleteRead)>;

	class stream {
	public:
		stream() = default;
		stream(stream&&) = default;
		stream(const stream&) = default;
		stream& operator=(stream&&) = default;
		stream& operator=(const stream&) = default;
		virtual ~stream() = default;

		[[nodiscard]] virtual std::streamsize size() const = 0;

		[[nodiscard]] virtual std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const = 0;

		[[nodiscard]] virtual std::unique_ptr<stream> substream(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) const = 0;

		void read_fully(std::streamoff offset, void* buf, std::streamsize length) const;

		template<typename T>
		[[nodiscard]] T read_fully(std::streamoff offset) const {
			T buf{};
			read_fully(offset, &buf, sizeof(T));
			return buf;
		}

		template<typename T>
		void read_fully(std::streamoff offset, std::span<T> buf) const {
			read_fully(offset, buf.data(), buf.size_bytes());
		}

		template<typename T>
		[[nodiscard]] std::vector<T> read_vector(std::streamoff offset, size_t count, size_t maxCount = SIZE_MAX) const {
			if (count > maxCount)
				throw std::runtime_error("trying to read too many");
			std::vector<T> result(count);
			read_fully(offset, std::span(result));
			return result;
		}

		template<typename T>
		[[nodiscard]] std::vector<T> read_vector(size_t maxCount = SIZE_MAX) const {
			return read_vector<T>(0, (std::min)(maxCount, static_cast<size_t>(size() / sizeof(T))));
		}

		template<typename T>
		[[nodiscard]] linear_reader<T> as_linear_reader() const {
			return [this, buf = std::vector<T>(), ptr = uint64_t(), to = size()](size_t len, bool throwOnIncompleteRead) mutable {
				if (ptr == to)
					return std::span<T>();
				buf.resize(static_cast<size_t>(std::min<uint64_t>(len, to - ptr)));
				const auto r = read(ptr, buf.data(), buf.size());
				if (r < static_cast<std::streamoff>(buf.size()) && throwOnIncompleteRead)
					throw std::runtime_error("incomplete read");
				ptr += buf.size();
				return std::span(buf);
			};
		}
	};

	class default_base_stream : public stream, public std::enable_shared_from_this<default_base_stream> {
	public:
		default_base_stream() = default;
		default_base_stream(default_base_stream&&) = default;
		default_base_stream(const default_base_stream&) = default;
		default_base_stream& operator=(default_base_stream&&) = default;
		default_base_stream& operator=(const default_base_stream&) = default;
		~default_base_stream() override = default;

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
		partial_view_stream(partial_view_stream&&) = delete;
		partial_view_stream& operator=(partial_view_stream&&) = delete;
		partial_view_stream& operator=(const partial_view_stream&) = delete;
		~partial_view_stream() override = default;

		[[nodiscard]] std::streamsize size() const override;
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;
		[[nodiscard]] std::unique_ptr<stream> substream(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) const override;
	};

	class file_stream : public default_base_stream {
		struct data;
		std::unique_ptr<data> m_data;

	public:
		file_stream(std::filesystem::path path);
		file_stream(file_stream&&) = delete;
		file_stream(const file_stream&) = delete;
		file_stream& operator=(file_stream&&) = delete;
		file_stream& operator=(const file_stream&) = delete;
		~file_stream() override;

		[[nodiscard]] std::streamsize size() const override;
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;
	};

	class memory_stream : public default_base_stream {
		std::vector<uint8_t> m_buffer;
		std::span<const uint8_t> m_view;

	public:
		memory_stream() = default;
		memory_stream(memory_stream&& r) noexcept;
		memory_stream(const memory_stream& r);
		memory_stream(const stream& r);
		memory_stream(std::vector<uint8_t> buffer);
		memory_stream(std::span<uint8_t> view);
		memory_stream(std::span<const uint8_t> view);
		memory_stream& operator=(std::vector<uint8_t>&& buf) noexcept;
		memory_stream& operator=(const std::vector<uint8_t>& buf);
		memory_stream& operator=(memory_stream&& r) noexcept;
		memory_stream& operator=(const memory_stream& r);
		~memory_stream() override = default;

		[[nodiscard]] std::streamsize size() const override;
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override;

		[[nodiscard]] bool owns_data() const;
		std::span<const uint8_t> as_span(std::streamoff offset, std::streamsize length = (std::numeric_limits<std::streamsize>::max)()) const;
	};

	class lazy_preloading_stream : public default_base_stream {
		std::shared_ptr<const stream> m_strm;
		std::streamoff m_offset;
		std::streamsize m_length;
		bool m_passthrough = false;
		mutable std::vector<uint8_t> m_data;
		mutable std::mutex m_mtx;

	public:
		lazy_preloading_stream(std::shared_ptr<const stream> strm, std::streamoff offset = 0, std::streamsize length = (std::numeric_limits<std::streamsize>::max)())
			: m_strm(strm)
			, m_offset(offset)
			, m_length(length) {

			m_passthrough = m_passthrough || dynamic_cast<const memory_stream*>(m_strm.get());
			m_passthrough = m_passthrough || dynamic_cast<const lazy_preloading_stream*>(m_strm.get());
		}

		[[nodiscard]] std::streamsize size() const override {
			return m_strm->size();
		}

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (m_passthrough)
				return m_strm->read(offset, buf, length);

			std::streamsize returnsize = 0;
			if (offset < m_offset) {
				const auto read = m_strm->read(offset, buf, std::min<std::streamsize>(length, m_offset - offset));
				returnsize += read;
				offset += read;
				buf = reinterpret_cast<char*>(buf) + read;
				length -= read;
			}

			if (length == 0)
				return returnsize;

			if (const auto targetSize = m_length == (std::numeric_limits<std::streamsize>::max)() ? m_strm->size() : m_length) {
				auto read = length;
				if (m_data.size() != targetSize) {
					const auto lock = std::lock_guard(m_mtx);
					if (m_data.size() != targetSize) {
						m_data.resize(targetSize);
						m_strm->read_fully(m_offset, std::span(m_data));
					}
				}

				if (offset - m_offset < static_cast<std::streamoff>(m_data.size())) {
					if (offset - m_offset + read > static_cast<std::streamoff>(m_data.size()))
						read = static_cast<std::streamsize>(m_data.size() - offset + m_offset);
					std::copy_n(&m_data[static_cast<size_t>(offset - m_offset)], static_cast<size_t>(read), static_cast<char*>(buf));
					returnsize += read;
					length -= read;
					buf = reinterpret_cast<char*>(buf) + read;
					offset += read;
				}
			}

			return returnsize + m_strm->read(offset, buf, length);
		}
	};
}

#endif
