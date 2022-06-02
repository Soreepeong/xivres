#include "../include/xivres/packed_stream.texture.h"
#include "../include/xivres/util.thread_pool.h"

std::streamsize xivres::texture_passthrough_packer::size() {
	const auto blockCount = MaxMipmapCountPerTexture + Align<uint64_t>(m_stream->size(), packed::MaxBlockDataSize).Count;

	std::streamsize size = 0;

	// packed::file_header packedFileHeader;
	size += sizeof packed::file_header;

	// packed::mipmap_block_locator lodBlocks[mipmapCount];
	size += MaxMipmapCountPerTexture * sizeof packed::mipmap_block_locator;

	// uint16_t subBlockSizes[blockCount];
	size += blockCount * sizeof uint16_t;

	// Align block
	size = Align(size);

	// TextureHeader textureHeader;
	size += sizeof TextureHeader;

	// Mipmap offsets
	size += blockCount * sizeof uint16_t;

	// Just to be safe, align block
	size = Align(size);

	// PackedBlock blocksOfMaximumSize[blockCount];
	size += blockCount * packed::MaxBlockSize;

	return size;
}

void xivres::texture_passthrough_packer::ensure_initialized() {
	if (!m_mergedHeader.empty())
		return;

	const auto lock = std::lock_guard(m_mtx);
	if (!m_mergedHeader.empty())
		return;

	std::vector<uint16_t> subBlockSizes;
	std::vector<uint8_t> textureHeaderAndMipmapOffsets;

	auto entryHeader = packed::file_header{
		.HeaderSize = sizeof packed::file_header,
		.Type = packed::type::texture,
		.DecompressedSize = static_cast<uint32_t>(m_stream->size()),
	};

	textureHeaderAndMipmapOffsets.resize(sizeof TextureHeader);
	m_stream->read_fully(0, std::span(textureHeaderAndMipmapOffsets));

	const auto mipmapCount = *reinterpret_cast<const TextureHeader*>(&textureHeaderAndMipmapOffsets[0])->MipmapCount;
	textureHeaderAndMipmapOffsets.resize(sizeof TextureHeader + mipmapCount * sizeof uint32_t);
	m_stream->read_fully(sizeof TextureHeader, util::span_cast<uint32_t>(textureHeaderAndMipmapOffsets, sizeof TextureHeader, mipmapCount));

	const auto firstBlockOffset = *reinterpret_cast<const uint32_t*>(&textureHeaderAndMipmapOffsets[sizeof TextureHeader]);
	textureHeaderAndMipmapOffsets.resize(firstBlockOffset);
	const auto mipmapOffsets = util::span_cast<uint32_t>(textureHeaderAndMipmapOffsets, sizeof TextureHeader, mipmapCount);
	m_stream->read_fully(sizeof TextureHeader + mipmapOffsets.size_bytes(), std::span(textureHeaderAndMipmapOffsets).subspan(sizeof TextureHeader + mipmapOffsets.size_bytes()));
	const auto& texHeader = *reinterpret_cast<const TextureHeader*>(&textureHeaderAndMipmapOffsets[0]);

	m_mipmapSizes.resize(mipmapOffsets.size());
	for (size_t i = 0; i < mipmapOffsets.size(); ++i)
		m_mipmapSizes[i] = static_cast<uint32_t>(TextureRawDataLength(texHeader, i));

	// Actual data exists but the mipmap offset array after texture header does not bother to refer
	// to the ones after the first set of mipmaps?
	// For example: if there are mipmaps of 4x4, 2x2, 1x1, 4x4, 2x2, 1x2, 4x4, 2x2, and 1x1,
	// then it will record mipmap offsets only up to the first occurrence of 1x1.
	const auto repeatCount = mipmapOffsets.size() < 2 ? 1 : (size_t{} + mipmapOffsets[1] - mipmapOffsets[0]) / TextureRawDataLength(texHeader, 0);
	m_mipmapOffsetsWithRepeats = { mipmapOffsets.begin(), mipmapOffsets.end() };
	for (auto forceQuit = false; !forceQuit && (m_mipmapOffsetsWithRepeats.empty() || m_mipmapOffsetsWithRepeats.back() + m_mipmapSizes.back() * repeatCount < entryHeader.DecompressedSize);) {
		for (uint16_t i = 0; i < mipmapCount; ++i) {

			// <caused by TexTools export>
			const auto size = static_cast<uint32_t>(TextureRawDataLength(texHeader, i));
			if (m_mipmapOffsetsWithRepeats.back() + m_mipmapSizes.back() + size > entryHeader.DecompressedSize) {
				forceQuit = true;
				break;
			}
			// </caused by TexTools export>

			m_mipmapOffsetsWithRepeats.push_back(m_mipmapOffsetsWithRepeats.back() + m_mipmapSizes.back());
			m_mipmapSizes.push_back(static_cast<uint32_t>(TextureRawDataLength(texHeader, i)));
		}
	}

	auto blockOffsetCounter = firstBlockOffset;
	for (size_t i = 0; i < m_mipmapOffsetsWithRepeats.size(); ++i) {
		const auto mipmapSize = m_mipmapSizes[i];
		for (uint32_t repeatI = 0; repeatI < repeatCount; repeatI++) {
			const auto blockAlignment = Align<uint32_t>(mipmapSize, packed::MaxBlockDataSize);
			packed::mipmap_block_locator loc{
				.CompressedOffset = blockOffsetCounter,
				.CompressedSize = 0,
				.DecompressedSize = mipmapSize,
				.FirstBlockIndex = m_blockLocators.empty() ? 0 : m_blockLocators.back().FirstBlockIndex + m_blockLocators.back().BlockCount,
				.BlockCount = blockAlignment.Count,
			};

			blockAlignment.IterateChunked([&](uint32_t, const uint32_t offset, const uint32_t length) {
				packed::block_header header{
					.HeaderSize = sizeof packed::block_header,
					.Version = 0,
					.CompressedSize = packed::block_header::CompressedSizeNotCompressed,
					.DecompressedSize = length,
				};
				const auto alignmentInfo = Align(sizeof header + length);

				m_size += alignmentInfo.Alloc;
				subBlockSizes.push_back(static_cast<uint16_t>(alignmentInfo.Alloc));
				blockOffsetCounter += subBlockSizes.back();
				loc.CompressedSize += subBlockSizes.back();

			}, m_mipmapOffsetsWithRepeats[i] + m_mipmapSizes[i] * repeatI);

			m_blockLocators.emplace_back(loc);
		}
	}

	entryHeader.BlockCountOrVersion = static_cast<uint32_t>(m_blockLocators.size());
	entryHeader.HeaderSize = static_cast<uint32_t>(xivres::Align(
		sizeof entryHeader +
		std::span(m_blockLocators).size_bytes() +
		std::span(subBlockSizes).size_bytes()));
	entryHeader.set_space_units(m_size);

	m_mergedHeader.reserve(entryHeader.HeaderSize + m_blockLocators.front().CompressedOffset);
	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&entryHeader),
		reinterpret_cast<char*>(&entryHeader + 1));
	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&m_blockLocators.front()),
		reinterpret_cast<char*>(&m_blockLocators.back() + 1));
	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&subBlockSizes.front()),
		reinterpret_cast<char*>(&subBlockSizes.back() + 1));
	m_mergedHeader.resize(entryHeader.HeaderSize);
	m_mergedHeader.insert(m_mergedHeader.end(),
		textureHeaderAndMipmapOffsets.begin(),
		textureHeaderAndMipmapOffsets.end());
	m_mergedHeader.resize(entryHeader.HeaderSize + m_blockLocators.front().CompressedOffset);

	m_size += m_mergedHeader.size();
}

std::streamsize xivres::texture_passthrough_packer::translate_read(std::streamoff offset, void* buf, std::streamsize length) {
	if (!length)
		return 0;

	const auto& packedFileHeader = *reinterpret_cast<const packed::file_header*>(&m_mergedHeader[0]);
	const auto& texHeader = *reinterpret_cast<const TextureHeader*>(&m_mergedHeader[packedFileHeader.HeaderSize]);

	auto relativeOffset = static_cast<uint64_t>(offset);
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	// 1. Read headers and locators
	if (relativeOffset < m_mergedHeader.size()) {
		const auto src = std::span(m_mergedHeader)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= m_mergedHeader.size();

	// 2. Read data blocks
	if (relativeOffset < m_size - m_mergedHeader.size()) {

		// 1. Find the first LOD block
		relativeOffset += m_blockLocators[0].CompressedOffset;
		auto it = std::ranges::lower_bound(m_blockLocators, packed::mipmap_block_locator{ .CompressedOffset = static_cast<uint32_t>(relativeOffset) },
			[&](const auto& l, const auto& r) { return l.CompressedOffset < r.CompressedOffset; });
		if (it == m_blockLocators.end() || relativeOffset < it->CompressedOffset)
			--it;
		relativeOffset -= it->CompressedOffset;

		// 2. Iterate through LOD block headers
		for (; it != m_blockLocators.end(); ++it) {
			const auto blockIndex = it - m_blockLocators.begin();
			auto j = relativeOffset / packed::MaxBlockSize;
			relativeOffset -= j * packed::MaxBlockSize;

			// Iterate through packed blocks belonging to current LOD block
			for (; j < it->BlockCount; ++j) {
				const auto decompressedSize = j == it->BlockCount - 1 ? m_mipmapSizes[blockIndex] % packed::MaxBlockDataSize : packed::MaxBlockDataSize;
				const auto pad = Align(sizeof packed::block_header + decompressedSize).Pad;

				// 1. Read packed block header
				if (relativeOffset < sizeof packed::block_header) {
					const auto header = packed::block_header{
						.HeaderSize = sizeof packed::block_header,
						.Version = 0,
						.CompressedSize = packed::block_header::CompressedSizeNotCompressed,
						.DecompressedSize = decompressedSize,
					};
					const auto src = util::span_cast<uint8_t>(1, &header).subspan(static_cast<size_t>(relativeOffset));
					const auto available = (std::min)(out.size_bytes(), src.size_bytes());
					std::copy_n(src.begin(), available, out.begin());
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else
					relativeOffset -= sizeof packed::block_header;

				// 2. Read packed block data
				if (relativeOffset < decompressedSize) {
					const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(decompressedSize - relativeOffset));
					m_stream->read_fully(m_mipmapOffsetsWithRepeats[blockIndex] + j * packed::MaxBlockDataSize + relativeOffset, &out[0], available);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else
					relativeOffset -= decompressedSize;

				// 3. Fill padding with zero
				if (relativeOffset < pad) {
					const auto available = (std::min)(out.size_bytes(), pad);
					std::fill_n(&out[0], available, 0);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else {
					relativeOffset -= pad;
				}
			}
		}
	}

	// 3. Fill remainder with zero
	if (const auto endPadSize = static_cast<uint64_t>(size() - m_size); relativeOffset < endPadSize) {
		const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(endPadSize - relativeOffset));
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(static_cast<size_t>(available));
	}

	return length - out.size_bytes();
}

std::unique_ptr<xivres::stream> xivres::texture_compressing_packer::pack(const stream& strm, int compressionLevel) const {
	std::vector<uint8_t> textureHeaderAndMipmapOffsets;

	const auto rawStreamSize = static_cast<uint32_t>(strm.size());

	textureHeaderAndMipmapOffsets.resize(sizeof TextureHeader);
	strm.read_fully(0, std::span(textureHeaderAndMipmapOffsets));

	const auto mipmapCount = *reinterpret_cast<const TextureHeader*>(&textureHeaderAndMipmapOffsets[0])->MipmapCount;
	textureHeaderAndMipmapOffsets.resize(sizeof TextureHeader + mipmapCount * sizeof uint32_t);
	strm.read_fully(sizeof TextureHeader, util::span_cast<uint32_t>(textureHeaderAndMipmapOffsets, sizeof TextureHeader, mipmapCount));

	const auto firstBlockOffset = *reinterpret_cast<const uint32_t*>(&textureHeaderAndMipmapOffsets[sizeof TextureHeader]);
	textureHeaderAndMipmapOffsets.resize(firstBlockOffset);
	const auto mipmapOffsets = util::span_cast<uint32_t>(textureHeaderAndMipmapOffsets, sizeof TextureHeader, mipmapCount);
	strm.read_fully(sizeof TextureHeader + mipmapOffsets.size_bytes(), std::span(textureHeaderAndMipmapOffsets).subspan(sizeof TextureHeader + mipmapOffsets.size_bytes()));
	const auto& texHeader = *reinterpret_cast<const TextureHeader*>(&textureHeaderAndMipmapOffsets[0]);

	if (is_cancelled())
		return nullptr;

	std::vector<uint32_t> mipmapSizes(mipmapOffsets.size());
	for (size_t i = 0; i < mipmapOffsets.size(); ++i)
		mipmapSizes[i] = static_cast<uint32_t>(TextureRawDataLength(texHeader, i));

	// Actual data exists but the mipmap offset array after texture header does not bother to refer
	// to the ones after the first set of mipmaps?
	// For example: if there are mipmaps of 4x4, 2x2, 1x1, 4x4, 2x2, 1x2, 4x4, 2x2, and 1x1,
	// then it will record mipmap offsets only up to the first occurrence of 1x1.
	const auto repeatCount = mipmapOffsets.size() < 2 ? 1 : (size_t{} + mipmapOffsets[1] - mipmapOffsets[0]) / TextureRawDataLength(texHeader, 0);
	std::vector<uint32_t> mipmapOffsetsWithRepeats(mipmapOffsets.begin(), mipmapOffsets.end());
	for (auto forceQuit = false; !forceQuit && (mipmapOffsetsWithRepeats.empty() || mipmapOffsetsWithRepeats.back() + mipmapSizes.back() * repeatCount < rawStreamSize);) {
		for (uint16_t i = 0; i < mipmapCount; ++i) {

			// <caused by TexTools export>
			const auto size = static_cast<uint32_t>(TextureRawDataLength(texHeader, i));
			if (mipmapOffsetsWithRepeats.back() + mipmapSizes.back() + size > rawStreamSize) {
				forceQuit = true;
				break;
			}
			// </caused by TexTools export>

			mipmapOffsetsWithRepeats.push_back(mipmapOffsetsWithRepeats.back() + mipmapSizes.back());
			mipmapSizes.push_back(static_cast<uint32_t>(TextureRawDataLength(texHeader, i)));
		}
	}

	if (is_cancelled())
		return nullptr;

	std::vector<uint32_t> maxMipmapSizes;
	{
		maxMipmapSizes.reserve(mipmapOffsetsWithRepeats.size());
		std::vector<uint8_t> readBuffer;

		for (size_t i = 0; i < mipmapOffsetsWithRepeats.size(); ++i) {
			if (is_cancelled())
				return nullptr;

			uint32_t maxMipmapSize = 0;

			const auto minSize = (std::max)(4U, static_cast<uint32_t>(TextureRawDataLength(texHeader.Type, 1, 1, texHeader.Depth, i)));
			if (mipmapSizes[i] > minSize) {
				for (size_t repeatI = 0; repeatI < repeatCount; repeatI++) {
					if (is_cancelled())
						return nullptr;

					size_t offset = mipmapOffsetsWithRepeats[i] + mipmapSizes[i] * repeatI;
					auto mipmapSize = mipmapSizes[i];
					readBuffer.resize(mipmapSize);

					if (const auto read = static_cast<size_t>(strm.read(offset, &readBuffer[0], mipmapSize)); read != mipmapSize) {
						// <caused by TexTools export>
						std::fill_n(&readBuffer[read], mipmapSize - read, 0);
						// </caused by TexTools export>
					}

					for (auto nextSize = mipmapSize;; mipmapSize = nextSize) {
						if (is_cancelled())
							return nullptr;

						nextSize /= 2;
						if (nextSize < minSize)
							break;

						auto anyNonZero = false;
						for (const auto& v : std::span(readBuffer).subspan(nextSize, mipmapSize - nextSize))
							if ((anyNonZero = anyNonZero || v))
								break;
						if (anyNonZero)
							break;
					}
					maxMipmapSize = (std::max)(maxMipmapSize, mipmapSize);
				}
			} else {
				maxMipmapSize = mipmapSizes[i];
			}

			maxMipmapSize = mipmapSizes[i];  // TODO
			maxMipmapSizes.emplace_back(maxMipmapSize);
		}
	}

	std::vector<std::vector<std::vector<std::pair<bool, std::vector<uint8_t>>>>> blockDataList;
	size_t blockLocatorCount = 0, subBlockCount = 0;
	{
		blockDataList.resize(mipmapOffsetsWithRepeats.size());

		util::thread_pool threadPool;
		std::vector<std::optional<util::zlib_deflater>> deflaters(threadPool.GetThreadCount());
		std::vector<std::vector<uint8_t>> readBuffers(threadPool.GetThreadCount());

		try {
			for (size_t i = 0; i < mipmapOffsetsWithRepeats.size(); ++i) {
				if (is_cancelled())
					return nullptr;

				blockDataList[i].resize(repeatCount);
				const auto mipmapSize = maxMipmapSizes[i];

				for (uint32_t repeatI = 0; repeatI < repeatCount; repeatI++) {
					if (is_cancelled())
						return nullptr;

					const auto blockAlignment = Align<uint32_t>(mipmapSize, packed::MaxBlockDataSize);
					auto& blockDataVector = blockDataList[i][repeatI];
					blockDataVector.resize(blockAlignment.Count);
					subBlockCount += blockAlignment.Count;

					blockAlignment.IterateChunkedBreakable([&](const uint32_t index, const uint32_t offset, const uint32_t length) {
						if (is_cancelled())
							return false;

						threadPool.Submit([this, compressionLevel, index, offset, length, &strm, &readBuffers, &deflaters, &blockDataVector](size_t threadIndex) {
							if (is_cancelled())
								return;

							auto& readBuffer = readBuffers[threadIndex];
							auto& deflater = deflaters[threadIndex];
							if (compressionLevel && !deflater)
								deflater.emplace(compressionLevel, Z_DEFLATED, -15);

							readBuffer.clear();
							readBuffer.resize(length);
							if (const auto read = static_cast<size_t>(strm.read(offset, &readBuffer[0], length)); read != length) {
								// <caused by TexTools export>
								std::fill_n(&readBuffer[read], length - read, 0);
								// </caused by TexTools export>
							}

							if ((blockDataVector[index].first = deflater && deflater->deflate(std::span(readBuffer)).size() < readBuffer.size()))
								blockDataVector[index].second = std::move(deflater->result());
							else
								blockDataVector[index].second = std::move(readBuffer);
						});

						return true;
					}, mipmapOffsetsWithRepeats[i] + mipmapSizes[i] * repeatI);

					blockLocatorCount++;
				}
			}

			threadPool.SubmitDoneAndWait();
		} catch (...) {
			// pass
		}

		threadPool.SubmitDoneAndWait();
	}

	if (is_cancelled())
		return nullptr;

	const auto entryHeaderLength = static_cast<uint16_t>(Align(0
		+ sizeof packed::file_header
		+ blockLocatorCount * sizeof packed::mipmap_block_locator
		+ subBlockCount * sizeof uint16_t
	));
	size_t entryBodyLength = textureHeaderAndMipmapOffsets.size();
	for (const auto& repeatedItem : blockDataList) {
		for (const auto& mipmapItem : repeatedItem) {
			for (const auto& blockItem : mipmapItem) {
				entryBodyLength += Align(sizeof packed::block_header + blockItem.second.size());
			}
		}
	}
	entryBodyLength = Align(entryBodyLength);

	std::vector<uint8_t> result(entryHeaderLength + entryBodyLength);

	auto& entryHeader = *reinterpret_cast<packed::file_header*>(&result[0]);
	entryHeader.Type = packed::type::texture;
	entryHeader.DecompressedSize = rawStreamSize;
	entryHeader.BlockCountOrVersion = static_cast<uint32_t>(blockLocatorCount);
	entryHeader.HeaderSize = entryHeaderLength;
	entryHeader.set_space_units(entryBodyLength);

	const auto blockLocators = util::span_cast<packed::mipmap_block_locator>(result, sizeof entryHeader, blockLocatorCount);
	const auto subBlockSizes = util::span_cast<uint16_t>(result, sizeof entryHeader + blockLocators.size_bytes(), subBlockCount);
	auto resultDataPtr = result.begin() + entryHeaderLength;
	resultDataPtr = std::copy(textureHeaderAndMipmapOffsets.begin(), textureHeaderAndMipmapOffsets.end(), resultDataPtr);

	auto blockOffsetCounter = static_cast<uint32_t>(std::span(textureHeaderAndMipmapOffsets).size_bytes());
	for (size_t i = 0, subBlockCounter = 0, blockLocatorIndexCounter = 0; i < mipmapOffsetsWithRepeats.size(); ++i) {
		if (is_cancelled())
			return nullptr;

		const auto maxMipmapSize = maxMipmapSizes[i];

		for (uint32_t repeatI = 0; repeatI < repeatCount; repeatI++) {
			if (is_cancelled())
				return nullptr;

			const auto blockAlignment = Align<uint32_t>(maxMipmapSize, packed::MaxBlockDataSize);

			auto& loc = blockLocators[blockLocatorIndexCounter++];
			loc.CompressedOffset = blockOffsetCounter;
			loc.CompressedSize = 0;
			loc.DecompressedSize = maxMipmapSize;
			loc.FirstBlockIndex = blockLocators.empty() ? 0 : blockLocators.back().FirstBlockIndex + blockLocators.back().BlockCount;
			loc.BlockCount = blockAlignment.Count;

			blockAlignment.IterateChunkedBreakable([&](const uint32_t index, const uint32_t offset, const uint32_t length) {
				if (is_cancelled())
					return false;

				auto& [useCompressed, targetBuf] = blockDataList[i][repeatI][index];

				auto& header = *reinterpret_cast<packed::block_header*>(&*resultDataPtr);
				header.HeaderSize = sizeof packed::block_header;
				header.Version = 0;
				header.CompressedSize = useCompressed ? static_cast<uint32_t>(targetBuf.size()) : packed::block_header::CompressedSizeNotCompressed;
				header.DecompressedSize = length;

				std::copy(targetBuf.begin(), targetBuf.end(), resultDataPtr + sizeof header);

				const auto& subBlockSize = subBlockSizes[subBlockCounter++] = static_cast<uint16_t>(Align(sizeof header + targetBuf.size()));
				blockOffsetCounter += subBlockSize;
				loc.CompressedSize += subBlockSize;
				resultDataPtr += subBlockSize;

				std::vector<uint8_t>().swap(targetBuf);

				return true;
			}, mipmapOffsetsWithRepeats[i] + mipmapSizes[i] * repeatI);
		}
	}

	return std::make_unique<memory_stream>(std::move(result));
}
