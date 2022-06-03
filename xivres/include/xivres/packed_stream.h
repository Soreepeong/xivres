#ifndef XIVRES_PACKEDSTREAM_H_
#define XIVRES_PACKEDSTREAM_H_

#include <type_traits>

#include <zlib.h>

#include "path_spec.h"
#include "sqpack.h"
#include "stream.h"

namespace xivres {
	class unpacked_stream;

	class packed_stream : public default_base_stream {
		path_spec m_pathSpec;

	public:
		packed_stream(path_spec pathSpec)
			: m_pathSpec(std::move(pathSpec)) {
		}

		bool update_path_spec(const path_spec& r) {
			if (m_pathSpec.HasOriginal() || !r.HasOriginal() || m_pathSpec != r)
				return false;

			m_pathSpec = r;
			return true;
		}

		[[nodiscard]] const path_spec& path_spec() const {
			return m_pathSpec;
		}

		[[nodiscard]] virtual packed::type get_packed_type() const = 0;

		unpacked_stream get_unpacked(std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;

		std::unique_ptr<unpacked_stream> make_unpacked_ptr(std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;
	};

	class stream_as_packed_stream : public packed_stream {
		const std::shared_ptr<const stream> m_stream;

		mutable packed::type m_entryType = packed::type::invalid;

	public:
		stream_as_packed_stream(xivres::path_spec pathSpec, std::shared_ptr<const stream> strm)
			: packed_stream(std::move(pathSpec))
			, m_stream(std::move(strm)) {}

		[[nodiscard]] std::streamsize size() const override {
			return m_stream->size();
		}

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override {
			return m_stream->read(offset, buf, length);
		}

		[[nodiscard]] packed::type get_packed_type() const override {
			if (m_entryType == packed::type::invalid) {
				// operation that should be lightweight enough that lock should not be needed
				m_entryType = m_stream->read_fully<packed::file_header>(0).Type;
			}
			return m_entryType;
		}
	};

	class untyped_passthrough_packer {
	public:
		untyped_passthrough_packer() = default;
		untyped_passthrough_packer(untyped_passthrough_packer&&) = default;
		untyped_passthrough_packer(const untyped_passthrough_packer&) = default;
		untyped_passthrough_packer& operator=(untyped_passthrough_packer&&) = default;
		untyped_passthrough_packer& operator=(const untyped_passthrough_packer&) = default;
		virtual ~untyped_passthrough_packer() = default;

		[[nodiscard]] virtual packed::type get_packed_type() = 0;

		[[nodiscard]] virtual std::streamsize size() = 0;

		[[nodiscard]] virtual std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) = 0;
	};

	template<packed::type TPackedFileType>
	class passthrough_packer : public untyped_passthrough_packer {
	public:
		static constexpr auto Type = TPackedFileType;

	protected:
		const std::shared_ptr<const stream> m_stream;
		size_t m_size = 0;

		virtual void ensure_initialized() = 0;

		virtual std::streamsize translate_read(std::streamoff offset, void* buf, std::streamsize length) = 0;

	public:
		passthrough_packer(std::shared_ptr<const stream> strm) : m_stream(std::move(strm)) {}
		[[nodiscard]] packed::type get_packed_type() override final { return TPackedFileType; }
		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) override final {
			ensure_initialized();
			return translate_read(offset, buf, length);
		}
	};

	template<typename TPacker, typename = std::enable_if_t<std::is_base_of_v<untyped_passthrough_packer, TPacker>>>
	class passthrough_packed_stream : public packed_stream {
		const std::shared_ptr<const stream> m_stream;
		mutable TPacker m_packer;

	public:
		passthrough_packed_stream(xivres::path_spec spec, std::shared_ptr<const stream> strm)
			: packed_stream(std::move(spec))
			, m_packer(std::move(strm)) {}

		[[nodiscard]] std::streamsize size() const final {
			return m_packer.size();
		}

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const final {
			return m_packer.read(offset, buf, length);
		}

		packed::type get_packed_type() const final {
			return m_packer.get_packed_type();
		}
	};

	class untyped_compressing_packer {
	public:
		untyped_compressing_packer() = default;
		untyped_compressing_packer(untyped_compressing_packer&&) = default;
		untyped_compressing_packer(const untyped_compressing_packer&) = default;
		untyped_compressing_packer& operator=(untyped_compressing_packer&&) = default;
		untyped_compressing_packer& operator=(const untyped_compressing_packer&) = default;
		virtual ~untyped_compressing_packer() = default;
	};

	template<packed::type TPackedFileType>
	class compressing_packer : public untyped_compressing_packer {
	public:
		static constexpr auto Type = TPackedFileType;

	private:
		bool m_bCancel = false;

	protected:
		[[nodiscard]] bool is_cancelled() const { return m_bCancel; }

	public:
		void cancel() { m_bCancel = true; }

		[[nodiscard]] virtual std::unique_ptr<stream> pack(const stream& rawStream, int compressionLevel) const = 0;
	};

	template<typename TPacker, typename = std::enable_if_t<std::is_base_of_v<untyped_compressing_packer, TPacker>>>
	class compressing_packed_stream : public packed_stream {
		constexpr static int CompressionLevel_AlreadyPacked = Z_BEST_COMPRESSION + 1;

		mutable std::mutex m_mtx;
		mutable std::shared_ptr<const stream> m_stream;
		mutable int m_compressionLevel;

	public:
		compressing_packed_stream(xivres::path_spec spec, std::shared_ptr<const stream> strm, int compressionLevel = Z_BEST_COMPRESSION)
			: packed_stream(std::move(spec))
			, m_stream(std::move(strm))
			, m_compressionLevel(compressionLevel) {}

		[[nodiscard]] std::streamsize size() const final {
			ensure_initialized();
			return m_stream->size();
		}

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const final {
			ensure_initialized();
			return m_stream->read(offset, buf, length);
		}

		packed::type get_packed_type() const final {
			return TPacker::Type;
		}

	private:
		void ensure_initialized() const {
			if (m_compressionLevel == CompressionLevel_AlreadyPacked)
				return;

			const auto lock = std::lock_guard(m_mtx);
			if (m_compressionLevel == CompressionLevel_AlreadyPacked)
				return;

			auto newStream = TPacker().pack(*m_stream, m_compressionLevel);
			if (!newStream)
				throw std::logic_error("TODO; cancellation currently unhandled");

			m_stream = std::move(newStream);
			m_compressionLevel = CompressionLevel_AlreadyPacked;
		}
	};
}

#endif
