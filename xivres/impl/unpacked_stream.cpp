#include "../include/xivres/unpacked_stream.h"

#include "../include/xivres/unpacked_stream.standard.h"
#include "../include/xivres/unpacked_stream.placeholder.h"
#include "../include/xivres/unpacked_stream.model.h"
#include "../include/xivres/unpacked_stream.texture.h"

#pragma warning(push)
#pragma warning(disable: 26495)
// ReSharper disable once CppPossiblyUninitializedMember
xivres::base_unpacker::block_decoder::block_decoder(base_unpacker& unpacker, void* buf, std::streamsize length, std::streampos offset)  // NOLINT(cppcoreguidelines-pro-type-member-init)
	: m_unpacker(unpacker)
	, m_target(static_cast<uint8_t*>(buf), static_cast<size_t>(length))
	, m_remaining(m_target)
	, m_skipLength(static_cast<uint32_t>(offset))
	, m_currentOffset(0) {
}
#pragma warning(pop)

bool xivres::base_unpacker::block_decoder::skip(size_t lengthToSkip, bool dataFilled) {
	if (m_skipLength > 0) {
		const auto consume = (std::min)(m_skipLength, static_cast<uint32_t>(lengthToSkip));
		m_skipLength -= consume;
		m_currentOffset += consume;
		lengthToSkip -= consume;
	}
	
	if (lengthToSkip > 0) {
		lengthToSkip = (std::min)(lengthToSkip, m_remaining.size());
		if (!dataFilled)
			std::fill_n(m_remaining.begin(), lengthToSkip, 0);
		m_remaining = m_remaining.subspan(lengthToSkip);
		m_currentOffset += static_cast<uint32_t>(lengthToSkip);
	}
	
	return complete();
}

bool xivres::base_unpacker::block_decoder::skip_to(size_t offset, bool dataFilled) {
	if (m_currentOffset > offset)
		throw bad_data_error("Cannot skip backwards");
	
	if (m_currentOffset < offset)
		return skip(offset - m_currentOffset, dataFilled);
	
	return complete();
}

bool xivres::base_unpacker::block_decoder::forward(std::span<uint8_t> data) {
	const auto dataSize = static_cast<std::streamsize>(data.size());

	if (m_skipLength >= dataSize)
		return skip(dataSize);

	const auto available = (std::min)(m_remaining.size(), static_cast<size_t>(data.size() - m_skipLength));
	const auto src = data.subspan(static_cast<size_t>(m_skipLength), available);
	std::copy_n(src.begin(), available, m_remaining.begin());
	return skip(m_skipLength + available, true);
}

bool xivres::base_unpacker::block_decoder::forward(const stream& strm, const uint32_t blockOffset, const size_t knownBlockSize) {
	auto& [buffer, inflater] = *m_unpacker.m_tls;
	buffer.resize(knownBlockSize);

	const auto data = std::span(&buffer[0], static_cast<size_t>(strm.read(blockOffset, &buffer[0], static_cast<std::streamsize>(knownBlockSize))));

	if (data.empty())
		throw bad_data_error("Empty block read");
	
	if (data.size() < sizeof MostRecentBlockHeader)
		throw bad_data_error("Block read size < sizeof blockHeader");
	
	memcpy(&MostRecentBlockHeader, &buffer[0], sizeof MostRecentBlockHeader);

	if (data.size() < MostRecentBlockHeader.total_block_size())
		throw bad_data_error("Incomplete block read");

	if (MostRecentBlockHeader.total_block_size() > buffer.size())
		throw bad_data_error("sizeof blockHeader + blockHeader.CompressSize must be under 16K");

	if (m_skipLength >= MostRecentBlockHeader.DecompressedSize)
		return skip(MostRecentBlockHeader.DecompressedSize);

	const auto target = m_remaining.subspan(0, (std::min)(m_remaining.size_bytes(), static_cast<size_t>(MostRecentBlockHeader.DecompressedSize - m_skipLength)));
	if (MostRecentBlockHeader.compressed()) {
		if (sizeof MostRecentBlockHeader + MostRecentBlockHeader.CompressedSize > data.size_bytes())
			throw bad_data_error("Failed to read block");

		if (m_bMultithreaded) {
			m_waiter.submit([this, target, buffer_ = std::move(buffer), dataLength = data.size(), skip = m_skipLength](auto& c) mutable {
				auto& [buffer, inflater] = *m_unpacker.m_tls;
				buffer = std::move(buffer_);
				const auto data = std::span(buffer).subspan(0, dataLength);

				if (!inflater)
					inflater.emplace(-MAX_WBITS);

				const auto& blockHeader = *reinterpret_cast<const packed::block_header*>(&data[0]);
				if (skip) {
					const auto buf = (*inflater)(data.subspan(sizeof blockHeader, blockHeader.CompressedSize), blockHeader.DecompressedSize);
					if (buf.size_bytes() != blockHeader.DecompressedSize)
						throw bad_data_error(std::format("Expected {} bytes, inflated to {} bytes",
							*blockHeader.DecompressedSize, buf.size_bytes()));
					std::copy_n(&buf[static_cast<size_t>(skip)],
						target.size_bytes(),
						target.begin());

				} else {
					const auto buf = (*inflater)(data.subspan(sizeof blockHeader, blockHeader.CompressedSize), target);
					if (buf.size_bytes() != target.size_bytes())
						throw bad_data_error(std::format("Expected {} bytes, inflated to {} bytes",
							target.size_bytes(), buf.size_bytes()));
				}
			});

		} else {
			if (!inflater)
				inflater.emplace(-MAX_WBITS);

			const auto& blockHeader = *reinterpret_cast<const packed::block_header*>(&data[0]);
			if (m_skipLength) {
				const auto buf = (*inflater)(data.subspan(sizeof blockHeader, blockHeader.CompressedSize), blockHeader.DecompressedSize);
				if (buf.size_bytes() != blockHeader.DecompressedSize)
					throw bad_data_error(std::format("Expected {} bytes, inflated to {} bytes",
						*blockHeader.DecompressedSize, buf.size_bytes()));
				std::copy_n(&buf[static_cast<size_t>(m_skipLength)],
					target.size_bytes(),
					target.begin());

			} else {
				const auto buf = (*inflater)(data.subspan(sizeof blockHeader, blockHeader.CompressedSize), target);
				if (buf.size_bytes() != target.size_bytes())
					throw bad_data_error(std::format("Expected {} bytes, inflated to {} bytes",
						target.size_bytes(), buf.size_bytes()));
			}
		}

	} else {
		std::copy_n(&data[static_cast<size_t>(sizeof MostRecentBlockHeader + m_skipLength)], target.size(), target.begin());
	}

	return skip(m_skipLength + target.size_bytes(), true);
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
