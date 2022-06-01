#ifndef _XIVRES_PACKEDFILESTREAM_H_
#define _XIVRES_PACKEDFILESTREAM_H_

#include <type_traits>

#include <zlib.h>

#include "Sqpack.h"
#include "stream.h"

namespace xivres {
	class PackedFileUnpackingStream;

	class packed_stream : public default_base_stream {
		xiv_path_spec m_pathSpec;

	public:
		packed_stream(xiv_path_spec pathSpec)
			: m_pathSpec(std::move(pathSpec)) {
		}

		bool Updatepath_spec(const xiv_path_spec& r) {
			if (m_pathSpec.HasOriginal() || !r.HasOriginal() || m_pathSpec != r)
				return false;

			m_pathSpec = r;
			return true;
		}

		[[nodiscard]] const xiv_path_spec& path_spec() const {
			return m_pathSpec;
		}

		[[nodiscard]] virtual packed_type get_packed_type() const = 0;

		PackedFileUnpackingStream GetUnpackedStream(std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;

		std::unique_ptr<PackedFileUnpackingStream> GetUnpackedStreamPtr(std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;
	};

	class untyped_passthrough_packer {
	public:
		virtual ~untyped_passthrough_packer() = default;

		[[nodiscard]] virtual packed_type get_packed_type() = 0;

		[[nodiscard]] virtual std::streamsize size() = 0;

		[[nodiscard]] virtual std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) = 0;
	};

	template<packed_type TPackedFileType>
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
		[[nodiscard]] packed_type get_packed_type() override final { return TPackedFileType; }
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
		passthrough_packed_stream(xiv_path_spec spec, std::shared_ptr<const stream> strm)
			: packed_stream(std::move(spec))
			, m_packer(std::move(strm)) {}

		[[nodiscard]] std::streamsize size() const final {
			return m_packer.size();
		}

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const final {
			return m_packer.read(offset, buf, length);
		}

		packed_type get_packed_type() const final {
			return m_packer.get_packed_type();
		}
	};

	class untyped_compressing_packer {};

	template<packed_type TPackedFileType>
	class compressing_packer : public untyped_compressing_packer {
	public:
		static constexpr auto Type = TPackedFileType;

	private:
		bool m_bCancel = false;

	protected:
		bool is_cancelled() const { return m_bCancel; }

	public:
		void cancel() { m_bCancel = true; }

		virtual std::unique_ptr<stream> pack(const stream& rawStream, int compressionLevel) const = 0;
	};

	template<typename TPacker, typename = std::enable_if_t<std::is_base_of_v<untyped_compressing_packer, TPacker>>>
	class compressing_packed_stream : public packed_stream {
		constexpr static int CompressionLevel_AlreadyPacked = Z_BEST_COMPRESSION + 1;

		mutable std::mutex m_mtx;
		mutable std::shared_ptr<const stream> m_stream;
		mutable int m_compressionLevel;

	public:
		compressing_packed_stream(xiv_path_spec spec, std::shared_ptr<const stream> strm, int compressionLevel = Z_BEST_COMPRESSION)
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

		packed_type get_packed_type() const final {
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

	class LazyPackedFileStream : public packed_stream {
		mutable std::mutex m_initializationMutex;
		mutable bool m_initialized = false;
		bool m_cancelled = false;

	protected:
		const std::filesystem::path m_path;
		const std::shared_ptr<const stream> m_stream;
		const uint64_t m_originalSize;
		const int m_compressionLevel;

	public:
		LazyPackedFileStream(xiv_path_spec spec, std::filesystem::path path, int compressionLevel = Z_BEST_COMPRESSION)
			: packed_stream(std::move(spec))
			, m_path(std::move(path))
			, m_stream(std::make_shared<file_stream>(m_path))
			, m_originalSize(m_stream->size())
			, m_compressionLevel(compressionLevel) {}

		LazyPackedFileStream(xiv_path_spec spec, std::shared_ptr<const stream> strm, int compressionLevel = Z_BEST_COMPRESSION)
			: packed_stream(std::move(spec))
			, m_path()
			, m_stream(std::move(strm))
			, m_originalSize(m_stream->size())
			, m_compressionLevel(compressionLevel) {}

		[[nodiscard]] std::streamsize size() const final {
			if (const auto estimate = MaxPossibleStreamSize();
				estimate != SqpackDataHeader::MaxFileSize_MaxValue)
				return estimate;

			ResolveConst();
			return size(*m_stream);
		}

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const final {
			ResolveConst();

			const auto size = m_stream->size();
			const auto estimate = MaxPossibleStreamSize();
			if (estimate == size || offset + length <= size)
				return read(*m_stream, offset, buf, length);

			if (offset >= estimate)
				return 0;
			if (offset + length > estimate)
				length = estimate - offset;

			auto target = std::span(static_cast<char*>(buf), static_cast<size_t>(length));
			if (offset < size) {
				const auto read = static_cast<size_t>(m_stream->read(offset, &target[0], (std::min<uint64_t>)(size - offset, target.size())));
				target = target.subspan(read);
			}

			const auto remaining = static_cast<size_t>((std::min<uint64_t>)(estimate - size, target.size()));
			std::ranges::fill(target.subspan(0, remaining), 0);
			return length - (target.size() - remaining);
		}

		void Resolve() {
			if (m_initialized)
				return;

			const auto lock = std::lock_guard(m_initializationMutex);
			if (m_initialized)
				return;

			Initialize(*m_stream);
			m_initialized = true;
		}

	protected:
		void ResolveConst() const {
			const_cast<LazyPackedFileStream*>(this)->Resolve();
		}

		virtual void Initialize(const stream& strm) {
			// does nothing
		}

		[[nodiscard]] virtual std::streamsize MaxPossibleStreamSize() const {
			return SqpackDataHeader::MaxFileSize_MaxValue;
		}

		[[nodiscard]] virtual std::streamsize size(const stream& strm) const = 0;

		virtual std::streamsize read(const stream& strm, std::streamoff offset, void* buf, std::streamsize length) const = 0;
	};
}

#endif
