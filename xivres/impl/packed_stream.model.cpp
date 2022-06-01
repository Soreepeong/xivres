#include "include/xivres/packed_stream.model.h"
#include "include/xivres/util.thread_pool.h"

xivres::model_passthrough_packer::model_passthrough_packer(std::shared_ptr<const stream> strm)
	: passthrough_packer<packed_type::model>(std::move(strm)) {}

std::streamsize xivres::model_passthrough_packer::size() {
	const auto blockCount = 11 + Align<uint64_t>(m_stream->size(), EntryBlockDataSize).Count;
	const auto headerSize = Align(sizeof m_header + blockCount * sizeof m_blockDataSizes[0]).Alloc;
	return headerSize + blockCount * EntryBlockSize;
}

void xivres::model_passthrough_packer::ensure_initialized() {
	if (*m_header.Entry.DecompressedSize)
		return;

	const auto lock = std::lock_guard(m_mtx);
	if (*m_header.Entry.DecompressedSize)
		return;

	model::header hdr;
	m_stream->read_fully(0, &hdr, sizeof hdr);

	m_header.Entry.Type = packed_type::model;
	m_header.Entry.DecompressedSize = static_cast<uint32_t>(m_stream->size());
	m_header.Entry.BlockCountOrVersion = hdr.Version;

	m_header.Model.VertexDeclarationCount = hdr.VertexDeclarationCount;
	m_header.Model.MaterialCount = hdr.MaterialCount;
	m_header.Model.LodCount = hdr.LodCount;
	m_header.Model.EnableIndexBufferStreaming = hdr.EnableIndexBufferStreaming;
	m_header.Model.EnableEdgeGeometry = hdr.EnableEdgeGeometry;

	const auto getNextBlockOffset = [&]() {
		return m_paddedBlockSizes.empty() ? 0U : m_blockOffsets.back() + m_paddedBlockSizes.back();
	};

	auto baseFileOffset = static_cast<uint32_t>(sizeof hdr);
	const auto generateSet = [&](const uint32_t size) {
		const auto alignedDecompressedSize = Align(size).Alloc;
		const auto alignedBlock = Align<uint32_t, uint16_t>(size, EntryBlockDataSize);
		const auto firstBlockOffset = size ? getNextBlockOffset() : 0;
		const auto firstBlockIndex = static_cast<uint16_t>(m_blockOffsets.size());
		alignedBlock.IterateChunked([&](auto, uint32_t offset, uint32_t size) {
			m_blockOffsets.push_back(getNextBlockOffset());
			m_blockDataSizes.push_back(size);
			m_paddedBlockSizes.push_back(static_cast<uint32_t>(Align(sizeof PackedBlockHeader + size)));
			m_actualFileOffsets.push_back(offset);
		}, baseFileOffset);
		const auto chunkSize = size ? getNextBlockOffset() - firstBlockOffset : 0;
		baseFileOffset += size;

		return std::make_tuple(
			alignedDecompressedSize,
			alignedBlock.Count,
			firstBlockOffset,
			firstBlockIndex,
			chunkSize
		);
	};

	std::tie(m_header.Model.AlignedDecompressedSizes.Stack,
		m_header.Model.BlockCount.Stack,
		m_header.Model.FirstBlockOffsets.Stack,
		m_header.Model.FirstBlockIndices.Stack,
		m_header.Model.ChunkSizes.Stack) = generateSet(hdr.StackSize);

	std::tie(m_header.Model.AlignedDecompressedSizes.Runtime,
		m_header.Model.BlockCount.Runtime,
		m_header.Model.FirstBlockOffsets.Runtime,
		m_header.Model.FirstBlockIndices.Runtime,
		m_header.Model.ChunkSizes.Runtime) = generateSet(hdr.RuntimeSize);

	for (size_t i = 0; i < 3; i++) {
		if (!hdr.VertexOffset[i])
			break;

		std::tie(m_header.Model.AlignedDecompressedSizes.Vertex[i],
			m_header.Model.BlockCount.Vertex[i],
			m_header.Model.FirstBlockOffsets.Vertex[i],
			m_header.Model.FirstBlockIndices.Vertex[i],
			m_header.Model.ChunkSizes.Vertex[i]) = generateSet(hdr.VertexSize[i]);

		std::tie(m_header.Model.AlignedDecompressedSizes.EdgeGeometryVertex[i],
			m_header.Model.BlockCount.EdgeGeometryVertex[i],
			m_header.Model.FirstBlockOffsets.EdgeGeometryVertex[i],
			m_header.Model.FirstBlockIndices.EdgeGeometryVertex[i],
			m_header.Model.ChunkSizes.EdgeGeometryVertex[i]) = generateSet(hdr.IndexOffset[i] - baseFileOffset);

		std::tie(m_header.Model.AlignedDecompressedSizes.Index[i],
			m_header.Model.BlockCount.Index[i],
			m_header.Model.FirstBlockOffsets.Index[i],
			m_header.Model.FirstBlockIndices.Index[i],
			m_header.Model.ChunkSizes.Index[i]) = generateSet(hdr.IndexSize[i]);
	}

	if (baseFileOffset > m_header.Entry.DecompressedSize)
		throw std::runtime_error("Bad model file (incomplete data)");

	m_header.Entry.HeaderSize = Align(static_cast<uint32_t>(sizeof m_header + std::span(m_blockDataSizes).size_bytes()));
	m_header.Entry.SetSpaceUnits(static_cast<size_t>(size()));
}

std::streamsize xivres::model_passthrough_packer::translate_read(std::streamoff offset, void* buf, std::streamsize length) {
	if (!length)
		return 0;

	auto relativeOffset = static_cast<uint64_t>(offset);
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	if (relativeOffset < sizeof m_header) {
		const auto src = util::span_cast<char>(1, &m_header).subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= sizeof m_header;

	if (const auto srcTyped = std::span(m_paddedBlockSizes);
		relativeOffset < srcTyped.size_bytes()) {
		const auto src = util::span_cast<char>(srcTyped).subspan(static_cast<size_t>(relativeOffset));
		const auto available = (std::min)(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= srcTyped.size_bytes();

	if (const auto padBeforeBlocks = Align(sizeof ModelEntryHeader + std::span(m_paddedBlockSizes).size_bytes()).Pad;
		relativeOffset < padBeforeBlocks) {
		const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(padBeforeBlocks - relativeOffset));
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= padBeforeBlocks;

	auto it = std::ranges::lower_bound(m_blockOffsets,
		static_cast<uint32_t>(relativeOffset),
		[&](uint32_t l, uint32_t r) {
		return l < r;
	});

	if (it == m_blockOffsets.end())
		--it;
	while (*it > relativeOffset) {
		if (it == m_blockOffsets.begin()) {
			const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(*it - relativeOffset));
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty()) return length;
			break;
		} else
			--it;
	}

	relativeOffset -= *it;

	for (auto i = it - m_blockOffsets.begin(); it != m_blockOffsets.end(); ++it, ++i) {
		if (relativeOffset < sizeof PackedBlockHeader) {
			const auto header = PackedBlockHeader{
				.HeaderSize = sizeof PackedBlockHeader,
				.Version = 0,
				.CompressedSize = PackedBlockHeader::CompressedSizeNotCompressed,
				.DecompressedSize = m_blockDataSizes[i],
			};
			const auto src = util::span_cast<uint8_t>(1, &header).subspan(static_cast<size_t>(relativeOffset));
			const auto available = (std::min)(out.size_bytes(), src.size_bytes());
			std::copy_n(src.begin(), available, out.begin());
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty()) return length;
		} else
			relativeOffset -= sizeof PackedBlockHeader;

		if (relativeOffset < m_blockDataSizes[i]) {
			const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(m_blockDataSizes[i] - relativeOffset));
			m_stream->read_fully(m_actualFileOffsets[i] + relativeOffset, &out[0], available);
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty()) return length;
		} else
			relativeOffset -= m_blockDataSizes[i];

		if (const auto padSize = m_paddedBlockSizes[i] - m_blockDataSizes[i] - sizeof PackedBlockHeader;
			relativeOffset < padSize) {
			const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(static_cast<size_t>(available));
			relativeOffset = 0;

			if (out.empty()) return length;
		} else
			relativeOffset -= padSize;
	}

	if (!out.empty()) {
		const auto actualDataSize = m_header.Entry.HeaderSize + (m_paddedBlockSizes.empty() ? 0 : m_blockOffsets.back() + m_paddedBlockSizes.back());
		const auto endPadSize = static_cast<uint64_t>(size() - actualDataSize);
		if (relativeOffset < endPadSize) {
			const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(endPadSize - relativeOffset));
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(static_cast<size_t>(available));
		}
	}

	return length - out.size_bytes();
}

std::unique_ptr<xivres::stream> xivres::model_compressing_packer::pack(const stream& strm, int compressionLevel) const {
	const auto hdr = strm.read_fully<model::header>(0);

	std::vector<std::vector<std::pair<bool, std::vector<uint8_t>>>> blockDataList(11);
	{
		util::thread_pool<> threadPool;
		std::vector<std::optional<util::zlib_deflater>> deflaters(threadPool.GetThreadCount());
		std::vector<std::vector<uint8_t>> readBuffers(threadPool.GetThreadCount());

		auto baseFileOffset = static_cast<uint32_t>(sizeof hdr);
		const auto generateSet = [&](const size_t setIndex, const uint32_t setSize) {
			const auto alignedBlock = Align<uint32_t, uint16_t>(setSize, EntryBlockDataSize);

			auto& setBlockDataList = blockDataList[setIndex];
			setBlockDataList.resize(alignedBlock.Count);

			alignedBlock.IterateChunkedBreakable([&](size_t blockIndex, uint32_t offset, uint32_t length) {
				if (is_cancelled())
					return false;

				threadPool.Submit([this, compressionLevel, offset, length, &strm, &readBuffers, &deflaters, &blockData = setBlockDataList[blockIndex]](size_t threadIndex) {
					if (is_cancelled())
						return;

					auto& readBuffer = readBuffers[threadIndex];
					auto& deflater = deflaters[threadIndex];
					if (compressionLevel && !deflater)
						deflater.emplace(compressionLevel, Z_DEFLATED, -15);

					readBuffer.clear();
					readBuffer.resize(length);
					strm.read_fully(offset, std::span(readBuffer));

					if ((blockData.first = deflater && deflater->deflate(std::span(readBuffer)).size() < readBuffer.size()))
						blockData.second = std::move(deflater->result());
					else
						blockData.second = std::move(readBuffer);
				});

				return true;
			}, baseFileOffset);

			baseFileOffset += setSize;
		};

		try {
			size_t setIndexCounter = 0;
			generateSet(setIndexCounter++, hdr.StackSize);
			generateSet(setIndexCounter++, hdr.RuntimeSize);

			for (size_t i = 0; i < 3; i++) {
				generateSet(setIndexCounter++, hdr.VertexSize[i]);
				generateSet(setIndexCounter++, hdr.IndexOffset[i] - hdr.VertexOffset[i] - hdr.VertexSize[i]);
				generateSet(setIndexCounter++, hdr.IndexSize[i]);
			}
		} catch (...) {
			// pass
		}
		threadPool.SubmitDoneAndWait();
	}

	if (is_cancelled())
		return nullptr;

	size_t totalBlockCount = 0, entryBodyLength = 0;
	for (const auto& set : blockDataList) {
		for (const auto& block : set) {
			totalBlockCount++;
			entryBodyLength += Align(sizeof PackedBlockHeader + block.second.size());
		}
	}
	const auto entryHeaderLength = static_cast<uint16_t>(Align(0
		+ sizeof PackedFileHeader
		+ sizeof SqpackModelPackedFileBlockLocator
		+ sizeof uint16_t * totalBlockCount
	));

	std::vector<uint8_t> result(entryHeaderLength + entryBodyLength);

	auto& entryHeader = *reinterpret_cast<PackedFileHeader*>(&result[0]);
	entryHeader.Type = packed_type::model;
	entryHeader.DecompressedSize = static_cast<uint32_t>(strm.size());
	entryHeader.BlockCountOrVersion = static_cast<uint32_t>(hdr.Version);
	entryHeader.HeaderSize = entryHeaderLength;
	entryHeader.SetSpaceUnits(entryBodyLength);

	auto& modelHeader = *reinterpret_cast<SqpackModelPackedFileBlockLocator*>(&entryHeader + 1);
	modelHeader.VertexDeclarationCount = hdr.VertexDeclarationCount;
	modelHeader.MaterialCount = hdr.MaterialCount;
	modelHeader.LodCount = hdr.LodCount;
	modelHeader.EnableIndexBufferStreaming = hdr.EnableIndexBufferStreaming;
	modelHeader.EnableEdgeGeometry = hdr.EnableEdgeGeometry;

	const auto paddedBlockSizes = util::span_cast<uint16_t>(result, sizeof entryHeader + sizeof modelHeader, totalBlockCount);

	{
		uint16_t totalBlockIndex = 0;
		uint32_t nextBlockOffset = 0;


		auto resultDataPtr = result.begin() + entryHeaderLength;
		auto baseFileOffset = static_cast<uint32_t>(sizeof hdr);
		const auto generateSet = [&](size_t setIndex, const uint32_t size) {
			const auto alignedDecompressedSize = Align(size).Alloc;
			const auto alignedBlock = Align<uint32_t, uint16_t>(size, EntryBlockDataSize);
			const auto firstBlockOffset = size ? nextBlockOffset : 0;
			const auto firstBlockIndex = static_cast<uint16_t>(totalBlockIndex);

			alignedBlock.IterateChunked([&](size_t blockIndex, uint32_t offset, uint32_t length) {
				if (is_cancelled())
					return false;
				auto& [useCompressed, targetBuf] = blockDataList[setIndex][blockIndex];

				auto& header = *reinterpret_cast<PackedBlockHeader*>(&*resultDataPtr);
				header.HeaderSize = sizeof PackedBlockHeader;
				header.Version = 0;
				header.CompressedSize = useCompressed ? static_cast<uint32_t>(targetBuf.size()) : PackedBlockHeader::CompressedSizeNotCompressed;
				header.DecompressedSize = static_cast<uint32_t>(length);

				std::copy(targetBuf.begin(), targetBuf.end(), resultDataPtr + sizeof header);

				const auto alloc = static_cast<uint16_t>(Align(sizeof header + targetBuf.size()));

				paddedBlockSizes[totalBlockIndex++] = alloc;
				nextBlockOffset += alloc;

				resultDataPtr += alloc;

				std::vector<uint8_t>().swap(targetBuf);
				return true;
			}, baseFileOffset);

			const auto chunkSize = size ? nextBlockOffset - firstBlockOffset : 0;
			baseFileOffset += size;

			return std::make_tuple(
				alignedDecompressedSize,
				alignedBlock.Count,
				firstBlockOffset,
				firstBlockIndex,
				chunkSize
			);
		};

		size_t setIndexCounter = 0;
		std::tie(modelHeader.AlignedDecompressedSizes.Stack,
			modelHeader.BlockCount.Stack,
			modelHeader.FirstBlockOffsets.Stack,
			modelHeader.FirstBlockIndices.Stack,
			modelHeader.ChunkSizes.Stack) = generateSet(setIndexCounter++, hdr.StackSize);

		std::tie(modelHeader.AlignedDecompressedSizes.Runtime,
			modelHeader.BlockCount.Runtime,
			modelHeader.FirstBlockOffsets.Runtime,
			modelHeader.FirstBlockIndices.Runtime,
			modelHeader.ChunkSizes.Runtime) = generateSet(setIndexCounter++, hdr.RuntimeSize);

		for (size_t i = 0; i < 3; i++) {
			if (is_cancelled())
				return {};

			std::tie(modelHeader.AlignedDecompressedSizes.Vertex[i],
				modelHeader.BlockCount.Vertex[i],
				modelHeader.FirstBlockOffsets.Vertex[i],
				modelHeader.FirstBlockIndices.Vertex[i],
				modelHeader.ChunkSizes.Vertex[i]) = generateSet(setIndexCounter++, hdr.VertexSize[i]);

			std::tie(modelHeader.AlignedDecompressedSizes.EdgeGeometryVertex[i],
				modelHeader.BlockCount.EdgeGeometryVertex[i],
				modelHeader.FirstBlockOffsets.EdgeGeometryVertex[i],
				modelHeader.FirstBlockIndices.EdgeGeometryVertex[i],
				modelHeader.ChunkSizes.EdgeGeometryVertex[i]) = generateSet(setIndexCounter++, hdr.IndexOffset[i] - hdr.VertexOffset[i] - hdr.VertexSize[i]);

			std::tie(modelHeader.AlignedDecompressedSizes.Index[i],
				modelHeader.BlockCount.Index[i],
				modelHeader.FirstBlockOffsets.Index[i],
				modelHeader.FirstBlockIndices.Index[i],
				modelHeader.ChunkSizes.Index[i]) = generateSet(setIndexCounter++, hdr.IndexSize[i]);
		}
	}

	return std::make_unique<memory_stream>(std::move(result));
}
