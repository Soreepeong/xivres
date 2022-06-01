#pragma once

#include "packed_stream.h"

#include "util.zlib_wrapper.h"

namespace xivres {
	class BasePackedFileStreamDecoder {
	protected:
		struct ReadStreamState {
			static constexpr auto ReadBufferMaxSize = 16384;

			uint8_t ReadBuffer[ReadBufferMaxSize];
			std::span<uint8_t> TargetBuffer;
			std::streamoff RelativeOffset = 0;
			size_t ReadBufferValidSize = 0;
			uint32_t RequestOffsetVerify = 0;
			bool HadCompressedBlocks = false;

			util::zlib_inflater Inflater{ -MAX_WBITS };

			[[nodiscard]] const auto& AsHeader() const {
				return *reinterpret_cast<const PackedBlockHeader*>(ReadBuffer);
			}

		private:
			void AttemptSatisfyRequestOffset(const uint32_t requestOffset) {
				if (RequestOffsetVerify < requestOffset) {
					const auto padding = requestOffset - RequestOffsetVerify;
					if (RelativeOffset < padding) {
						const auto available = (std::min<size_t>)(TargetBuffer.size_bytes(), padding);
						std::fill_n(TargetBuffer.begin(), available, 0);
						TargetBuffer = TargetBuffer.subspan(available);
						RelativeOffset = 0;

					} else
						RelativeOffset -= padding;

					RequestOffsetVerify = requestOffset;

				} else if (RequestOffsetVerify > requestOffset)
					throw bad_data_error("Duplicate read on same region");
			}

		public:
			void ProgressRead(const stream& strm, uint32_t blockOffset, size_t knownBlockSize = ReadBufferMaxSize) {
				ReadBufferValidSize = static_cast<size_t>(strm.read(blockOffset, ReadBuffer, knownBlockSize));

				if (ReadBufferValidSize < sizeof AsHeader() || ReadBufferValidSize < AsHeader().TotalBlockSize())
					throw xivres::bad_data_error("Incomplete block read");

				if (sizeof ReadBuffer < AsHeader().TotalBlockSize())
					throw xivres::bad_data_error("sizeof blockHeader + blockHeader.CompressSize must be under 16K");
			}

			void ProgressDecode(const uint32_t requestOffset) {
				const auto read = std::span(ReadBuffer, ReadBufferValidSize);

				AttemptSatisfyRequestOffset(requestOffset);
				if (TargetBuffer.empty())
					return;

				RequestOffsetVerify += AsHeader().DecompressedSize;

				if (RelativeOffset < AsHeader().DecompressedSize) {
					auto target = TargetBuffer.subspan(0, (std::min)(TargetBuffer.size_bytes(), static_cast<size_t>(AsHeader().DecompressedSize - RelativeOffset)));
					if (AsHeader().IsCompressed()) {
						if (sizeof AsHeader() + AsHeader().CompressedSize > read.size_bytes())
							throw bad_data_error("Failed to read block");

						if (RelativeOffset) {
							const auto buf = Inflater(read.subspan(sizeof AsHeader(), AsHeader().CompressedSize), AsHeader().DecompressedSize);
							if (buf.size_bytes() != AsHeader().DecompressedSize)
								throw bad_data_error(std::format("Expected {} bytes, inflated to {} bytes",
									*AsHeader().DecompressedSize, buf.size_bytes()));
							std::copy_n(&buf[static_cast<size_t>(RelativeOffset)],
								target.size_bytes(),
								target.begin());
						} else {
							const auto buf = Inflater(read.subspan(sizeof AsHeader(), AsHeader().CompressedSize), target);
							if (buf.size_bytes() != target.size_bytes())
								throw bad_data_error(std::format("Expected {} bytes, inflated to {} bytes",
									target.size_bytes(), buf.size_bytes()));
						}

					} else {
						std::copy_n(&read[static_cast<size_t>(sizeof AsHeader() + RelativeOffset)], target.size(), target.begin());
					}

					TargetBuffer = TargetBuffer.subspan(target.size_bytes());
					RelativeOffset = 0;

				} else
					RelativeOffset -= AsHeader().DecompressedSize;
			}
		};

		const std::shared_ptr<const packed_stream> m_stream;

	public:
		BasePackedFileStreamDecoder(std::shared_ptr<const packed_stream> strm)
			: m_stream(std::move(strm)) {
		}

		virtual std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) = 0;

		virtual ~BasePackedFileStreamDecoder() = default;

		static std::unique_ptr<BasePackedFileStreamDecoder> CreateNew(const PackedFileHeader& header, std::shared_ptr<const packed_stream> strm, std::span<uint8_t> obfuscatedHeaderRewrite = {});
	};
}

#include "BinaryPackedFileStreamDecoder.h"
#include "EmptyOrObfuscatedPackedFileStreamDecoder.h"
#include "ModelPackedFileStreamDecoder.h"
#include "TexturePackedFileStreamDecoder.h"

inline std::unique_ptr<xivres::BasePackedFileStreamDecoder> xivres::BasePackedFileStreamDecoder::CreateNew(const PackedFileHeader& header, std::shared_ptr<const packed_stream> strm, std::span<uint8_t> obfuscatedHeaderRewrite) {
	if (header.DecompressedSize == 0)
		return nullptr;

	switch (header.Type) {
		case packed_type::empty_or_hidden:
			return std::make_unique<EmptyOrObfuscatedPackedFileStreamDecoder>(header, std::move(strm), obfuscatedHeaderRewrite);

		case packed_type::standard:
			return std::make_unique<BinaryPackedFileStreamDecoder>(header, std::move(strm));

		case packed_type::texture:
			return std::make_unique<TexturePackedFileStreamDecoder>(header, std::move(strm));

		case packed_type::model:
			return std::make_unique<ModelPackedFileStreamDecoder>(header, std::move(strm));

		default:
			throw xivres::bad_data_error("Unsupported type");
	}
}
