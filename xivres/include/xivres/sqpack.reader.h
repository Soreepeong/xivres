#ifndef _XIVRES_SQPACKREADER_H_
#define _XIVRES_SQPACKREADER_H_

#include <map>
#include <mutex>
#include <optional>
#include <ranges>

#include "unpacked_stream.h"
#include "sqpack.h"

namespace xivres::sqpack {
	class reader {
	public:
		template<typename HashLocatorT, typename TextLocatorT> 
		class sqindex_type {
		public:
			const std::vector<uint8_t> Data;

			const sqpack::header& header() const {
				return *reinterpret_cast<const sqpack::header*>(&Data[0]);
			}

			const sqpack::sqindex::header& index_header() const {
				return *reinterpret_cast<const sqpack::sqindex::header*>(&Data[header().HeaderSize]);
			}

			std::span<const HashLocatorT> hash_locators() const {
				return util::span_cast<HashLocatorT>(Data, index_header().HashLocatorSegment.Offset, index_header().HashLocatorSegment.Size, 1);
			}

			std::span<const TextLocatorT> text_locators() const {
				return util::span_cast<TextLocatorT>(Data, index_header().TextLocatorSegment.Offset, index_header().TextLocatorSegment.Size, 1);
			}

			std::span<const sqpack::sqindex::segment_3_entry> segment_3() const {
				return util::span_cast<sqpack::sqindex::segment_3_entry>(Data, index_header().UnknownSegment3.Offset, index_header().UnknownSegment3.Size, 1);
			}

			const sqpack::sqindex::data_locator& data_locator(const char* fullPath) const {
				const auto it = std::lower_bound(text_locators().begin(), text_locators().end(), fullPath, path_spec::LocatorComparator());
				if (it == text_locators().end() || _strcmpi(it->FullPath, fullPath) != 0)
					throw std::out_of_range(std::format("Entry {} not found", fullPath));
				return it->Locator;
			}

		protected:
			friend class sqpack::reader;

			sqindex_type(const stream& strm, bool strictVerify)
				: Data(strm.read_vector<uint8_t>()) {

				if (strictVerify) {
					header().verify_or_throw(sqpack::file_type::SqIndex);
					index_header().verify_or_throw(sqpack::sqindex::sqindex_type::Index);
					if (index_header().HashLocatorSegment.Size % sizeof HashLocatorT)
						throw bad_data_error("HashLocators has an invalid size alignment");
					if (index_header().TextLocatorSegment.Size % sizeof TextLocatorT)
						throw bad_data_error("TextLocators has an invalid size alignment");
					if (index_header().UnknownSegment3.Size % sizeof sqpack::sqindex::segment_3_entry)
						throw bad_data_error("Segment3 has an invalid size alignment");
					index_header().HashLocatorSegment.Sha1.verify(hash_locators(), "HashLocatorSegment has invalid data SHA-1");
					index_header().TextLocatorSegment.Sha1.verify(text_locators(), "TextLocatorSegment has invalid data SHA-1");
					index_header().UnknownSegment3.Sha1.verify(segment_3(), "UnknownSegment3 has invalid data SHA-1");
				}
			}
		};

		class sqindex_1_type : public sqindex_type<sqindex::pair_hash_locator, sqindex::pair_hash_with_text_locator> {
		public:
			std::span<const sqindex::path_hash_locator> pair_hash_locators() const;

			std::span<const sqindex::pair_hash_locator> pair_hash_locators_for_path(uint32_t pathHash) const;

			using sqindex_type<sqindex::pair_hash_locator, sqindex::pair_hash_with_text_locator>::data_locator;
			const sqindex::data_locator& data_locator(uint32_t pathHash, uint32_t nameHash) const;

		protected:
			friend class sqpack::reader;

			sqindex_1_type(const stream& strm, bool strictVerify);
		};

		class sqindex_2_type : public sqindex_type<sqindex::full_hash_locator, sqindex::full_hash_with_text_locator> {
		public:
			using sqindex_type<sqindex::full_hash_locator, sqindex::full_hash_with_text_locator>::data_locator;
			const sqindex::data_locator& data_locator(uint32_t fullPathHash) const;

		protected:
			friend class sqpack::reader;

			sqindex_2_type(const stream& strm, bool strictVerify);
		};

		struct sqdata_type {
			sqpack::header Header{};
			sqdata::header DataHeader{};
			std::shared_ptr<stream> Stream;

		private:
			friend class sqpack::reader;

			sqdata_type(std::shared_ptr<stream> strm, const uint32_t datIndex, bool strictVerify);
		};

		struct entry_info {
			sqpack::sqindex::data_locator Locator;
			xivres::path_spec path_spec;
			uint64_t Allocation;
		};

		sqindex_1_type Index1;
		sqindex_2_type Index2;
		std::vector<sqdata_type> Data;
		std::vector<entry_info> Entries;

		uint8_t CategoryId;
		uint8_t ExpacId;
		uint8_t PartId;

		reader(const std::string& fileName, std::shared_ptr<stream> indexStream1, std::shared_ptr<stream> indexStream2, std::vector<std::shared_ptr<stream>> dataStreams, bool strictVerify = false);

		static sqpack::reader from_path(const std::filesystem::path& indexFile, bool strictVerify = false);

		[[nodiscard]] const uint32_t pack_id() const { return (CategoryId << 16) | (ExpacId << 8) | PartId; }

		[[nodiscard]] const sqpack::sqindex::data_locator& data_locator_from_index1(const xivres::path_spec& pathSpec) const;

		[[nodiscard]] const sqpack::sqindex::data_locator& data_locator_from_index2(const xivres::path_spec& pathSpec) const;

		[[nodiscard]] std::shared_ptr<packed_stream> packed_at(const entry_info& info) const;

		[[nodiscard]] std::shared_ptr<packed_stream> packed_at(const xivres::path_spec& pathSpec) const;

		[[nodiscard]] std::shared_ptr<unpacked_stream> at(const entry_info& info, std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;

		[[nodiscard]] std::shared_ptr<unpacked_stream> at(const xivres::path_spec& pathSpec, std::span<uint8_t> obfuscatedHeaderRewrite = {}) const;
	};
}

#endif
