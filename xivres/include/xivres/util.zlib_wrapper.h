#ifndef _XIVRES_INTERNAL_ZLIBWRAPPER_H_
#define _XIVRES_INTERNAL_ZLIBWRAPPER_H_

#include <cstdint>
#include <format>
#include <stdexcept>
#include <span>
#include <system_error>
#include <vector>
#include <zlib.h>

namespace xivres::util {
	class zlib_error : public std::runtime_error {
	public:
		static std::string zlib_error_to_string(int code);

		explicit zlib_error(int returnCode);
	};

	class zlib_inflater {
		const int m_windowBits;
		const size_t m_defaultBufferSize;
		z_stream m_zstream{};
		bool m_initialized = false;
		std::vector<uint8_t> m_buffer;

		void initialize_inflation();

	public:
		zlib_inflater(int windowBits = 15, int defaultBufferSize = 16384);

		~zlib_inflater();

		std::span<uint8_t> operator()(std::span<const uint8_t> source);

		std::span<uint8_t> operator()(std::span<const uint8_t> source, size_t maxSize);

		std::span<uint8_t> operator()(std::span<const uint8_t> source, std::span<uint8_t> target);
	};

	class zlib_deflater {
		const int m_level;
		const int m_method;
		const int m_windowBits;
		const int m_memLevel;
		const int m_strategy;
		const size_t m_defaultBufferSize;
		z_stream m_zstream{};
		bool m_initialized = false;
		std::vector<uint8_t> m_buffer;

		std::span<uint8_t> m_latestResult;

		void initialize_deflation();

	public:
		zlib_deflater(
			int level = Z_DEFAULT_COMPRESSION,
			int method = Z_DEFLATED,
			int windowBits = 15,
			int memLevel = 8,
			int strategy = Z_DEFAULT_STRATEGY,
			size_t defaultBufferSize = 16384);

		~zlib_deflater();

		std::span<uint8_t> deflate(std::span<const uint8_t> source);

		std::span<uint8_t> operator()(std::span<const uint8_t> source);

		const std::span<uint8_t>& result() const;

		std::vector<uint8_t>& result();
	};
}

#endif
