#include "../include/xivres/unpacked_stream.h"

#include "../include/xivres/unpacked_stream.standard.h"
#include "../include/xivres/unpacked_stream.placeholder.h"
#include "../include/xivres/unpacked_stream.model.h"
#include "../include/xivres/unpacked_stream.texture.h"

#pragma warning(push)
#pragma warning(disable: 26495)
// ReSharper disable once CppPossiblyUninitializedMember
xivres::base_unpacker::block_decoder::block_decoder(void* buf, std::streamsize length, std::streampos offset)  // NOLINT(cppcoreguidelines-pro-type-member-init)
	: m_target(static_cast<uint8_t*>(buf), static_cast<size_t>(length))
	, m_remaining(m_target)
	, m_relativeOffset(offset) {
}
#pragma warning(pop)

void xivres::base_unpacker::block_decoder::skip(uint32_t lengthToSkip) {
	m_relativeOffsetExpected += lengthToSkip;
	m_relativeOffset -= lengthToSkip;
}

void xivres::base_unpacker::block_decoder::forward(std::span<uint8_t> data) {
	const auto dataSize = static_cast<std::streamsize>(data.size());
	if (m_relativeOffset >= dataSize) {
		m_relativeOffset -= dataSize;
		return;
	}

	const auto available = (std::min)(m_remaining.size(), static_cast<size_t>(data.size() - m_relativeOffset));
	const auto src = data.subspan(static_cast<size_t>(m_relativeOffset), available);
	std::copy_n(src.begin(), available, m_remaining.begin());
	m_remaining = m_remaining.subspan(available);
	m_relativeOffset = 0;
}

void xivres::base_unpacker::block_decoder::forward(const uint32_t requestOffset, const stream& strm, const uint32_t blockOffset, const size_t knownBlockSize) {
	ensure_relative_offset(requestOffset);
	if (complete())
		return;

	const auto data = std::span(m_buffer, static_cast<size_t>(strm.read(blockOffset, m_buffer, static_cast<std::streamsize>(knownBlockSize))));

	if (data.empty())
		throw bad_data_error("Empty block read");
	
	if (data.size() < sizeof block_header())
		throw bad_data_error("Block read size < sizeof blockHeader");
	
	if (data.size() < block_header().total_block_size())
		throw bad_data_error("Incomplete block read");

	if (sizeof m_buffer < block_header().total_block_size())
		throw bad_data_error("sizeof blockHeader + blockHeader.CompressSize must be under 16K");

	m_relativeOffsetExpected += block_header().DecompressedSize;

	if (m_relativeOffset < block_header().DecompressedSize) {
		auto target = m_remaining.subspan(0, (std::min)(m_remaining.size_bytes(), static_cast<size_t>(block_header().DecompressedSize - m_relativeOffset)));
		if (block_header().compressed()) {
			if (sizeof block_header() + block_header().CompressedSize > data.size_bytes())
				throw bad_data_error("Failed to read block");

			if (m_relativeOffset) {
				const auto buf = m_inflater(data.subspan(sizeof block_header(), block_header().CompressedSize), block_header().DecompressedSize);
				if (buf.size_bytes() != block_header().DecompressedSize)
					throw bad_data_error(std::format("Expected {} bytes, inflated to {} bytes",
						*block_header().DecompressedSize, buf.size_bytes()));
				std::copy_n(&buf[static_cast<size_t>(m_relativeOffset)],
					target.size_bytes(),
					target.begin());
			} else {
				const auto buf = m_inflater(data.subspan(sizeof block_header(), block_header().CompressedSize), target);
				if (buf.size_bytes() != target.size_bytes())
					throw bad_data_error(std::format("Expected {} bytes, inflated to {} bytes",
						target.size_bytes(), buf.size_bytes()));
			}

		} else {
			std::copy_n(&data[static_cast<size_t>(sizeof block_header() + m_relativeOffset)], target.size(), target.begin());
		}

		m_remaining = m_remaining.subspan(target.size_bytes());
		m_relativeOffset = 0;

	} else
		m_relativeOffset -= block_header().DecompressedSize;
}

void xivres::base_unpacker::block_decoder::forward_zerofill(size_t len) {
	if (len == 0)
		return;

	const auto dataSize = static_cast<std::streamsize>(len);
	if (m_relativeOffset >= dataSize) {
		m_relativeOffset -= dataSize;
		return;
	}

	const auto available = (std::min)(m_remaining.size(), static_cast<size_t>(len - m_relativeOffset));
	std::fill_n(m_remaining.begin(), available, 0);
	m_remaining = m_remaining.subspan(available);
	m_relativeOffset = 0;
}

void xivres::base_unpacker::block_decoder::ensure_relative_offset(const uint32_t requestOffset) {
	if (m_relativeOffsetExpected < requestOffset) {
		const auto padding = requestOffset - m_relativeOffsetExpected;
		if (m_relativeOffset < padding) {
			const auto available = (std::min<size_t>)(m_remaining.size_bytes(), padding);
			std::fill_n(m_remaining.begin(), available, 0);
			m_remaining = m_remaining.subspan(available);
			m_relativeOffset = 0;

		} else
			m_relativeOffset -= padding;

		m_relativeOffsetExpected = requestOffset;

	} else if (m_relativeOffsetExpected > requestOffset)
		throw bad_data_error("Duplicate read on same region");
}

std::unique_ptr<xivres::base_unpacker> xivres::base_unpacker::make_unique(std::shared_ptr<const packed_stream> strm, std::span<uint8_t> obfuscatedHeaderRewrite) {
	const auto hdr = strm->read_fully<packed::file_header>(0);
	return make_unique(hdr, std::move(strm), obfuscatedHeaderRewrite);
}

std::unique_ptr<xivres::base_unpacker> xivres::base_unpacker::make_unique(const packed::file_header& header, std::shared_ptr<const packed_stream> strm, std::span<uint8_t> obfuscatedHeaderRewrite) {
	if (header.DecompressedSize == 0)
		return nullptr;

	switch (header.Type) {
		case packed::type::placeholder:
			return std::make_unique<placeholder_unpacker>(header, std::move(strm), obfuscatedHeaderRewrite);

		case packed::type::standard:
			return std::make_unique<standard_unpacker>(header, std::move(strm));

		case packed::type::texture:
			return std::make_unique<texture_unpacker>(header, std::move(strm));

		case packed::type::model:
			return std::make_unique<model_unpacker>(header, std::move(strm));

		default:
			throw bad_data_error("Unsupported type");
	}
}
