#ifndef XIVRES_EXCEL_H_
#define XIVRES_EXCEL_H_

#include <map>
#include <ranges>

#include "util.h"
#include "util.byte_order.h"

#include "common.h"
#include "path_spec.h"
#include "sqpack.h"
#include "stream.h"
#include "xivstring.h"

namespace xivres::sqpack {
	class reader;
}

namespace xivres::excel {
	// https://xiv.dev/game-data/file-formats/excel

	enum class cell_type : uint16_t {
		String = 0x0,
		Bool = 0x1,
		Int8 = 0x2,
		UInt8 = 0x3,
		Int16 = 0x4,
		UInt16 = 0x5,
		Int32 = 0x6,
		UInt32 = 0x7,
		Float32 = 0x9,
		Int64 = 0xA,
		UInt64 = 0xB,

		// 0 is read like data & 1, 1 is like data & 2, 2 = data & 4, etc...
		PackedBool0 = 0x19,
		PackedBool1 = 0x1A,
		PackedBool2 = 0x1B,
		PackedBool3 = 0x1C,
		PackedBool4 = 0x1D,
		PackedBool5 = 0x1E,
		PackedBool6 = 0x1F,
		PackedBool7 = 0x20,
	};

	enum class variant : uint16_t {
		Unknown = 0,
		Level2 = 1,
		Level3 = 2,
	};

	struct cell {
		cell_type Type{};
		uint8_t ValidSize{};

		union {
			uint8_t Buffer[8]{};
			bool boolean;
			int8_t int8;
			uint8_t uint8;
			int16_t int16;
			uint16_t uint16;
			int32_t int32;
			uint32_t uint32;
			float float32;
			int64_t int64;
			uint64_t uint64;
		};

		xivres::xivstring String;
	};
}

namespace xivres::excel::exl {
	class reader {
		int m_version = 0;
		std::map<std::string, int> m_nameToIdMap;
		std::map<int, std::string> m_idToNameMap;

	public:
		reader(const stream& strm);
		const std::string& operator[](int id) const { return m_idToNameMap.at(id); }
		int operator[](const std::string& s) const { return m_nameToIdMap.at(s); }
		[[nodiscard]] auto begin() const { return m_nameToIdMap.begin(); }
		[[nodiscard]] auto end() const { return m_nameToIdMap.end(); }
		[[nodiscard]] auto rbegin() const { return m_nameToIdMap.rbegin(); }
		[[nodiscard]] auto rend() const { return m_nameToIdMap.rend(); }
	};
}

namespace xivres::excel::exh {
	struct column {
		BE<cell_type> Type;
		BE<uint16_t> Offset;
	};

	struct page {
		BE<uint32_t> StartId;
		BE<uint32_t> RowCountWithSkip;
	};

	enum class read_strategy_mode : uint16_t {
		All = 0,
		RingBuffer_1 = 1,
		RingBuffer_2 = 2,
		Invalid = 3,
	};

	struct read_strategy {
		uint16_t buffer_count : 14;
		read_strategy_mode mode : 2;
	};

	struct header {
		static constexpr char Signature_Value[4] = { 'E', 'X', 'H', 'F' };

		static constexpr uint16_t Version_Value = 3;

		char Signature[4]{};
		BE<uint16_t> Version;
		BE<uint16_t> FixedDataSize;
		BE<uint16_t> ColumnCount;
		BE<uint16_t> PageCount;
		BE<uint16_t> LanguageCount;
		BE<read_strategy> ReadStrategy;
		BE<variant> Variant;
		BE<uint16_t> Padding_0x012;
		BE<uint32_t> RowCountWithoutSkip;
		BE<uint64_t> Padding_0x018;
	};

	class reader {
	private:
		std::string m_name;
		exh::header m_header;
		std::vector<exh::column> m_columns;
		std::vector<exh::page> m_pages;
		std::vector<game_language> m_languages;

	public:
		reader(std::string name, const stream& strm, bool strict = false);
		const std::string& name() const { return m_name; }
		const exh::header& header() const { return m_header; }
		const std::vector<exh::column>& get_columns() const { return m_columns; }
		const std::vector<exh::page>& get_pages() const { return m_pages; }
		const std::vector<game_language>& get_languages() const { return m_languages; }
		size_t get_owning_page_index(uint32_t rowId) const;
		const exh::page& get_owning_page(uint32_t rowId) const { return m_pages[get_owning_page_index(rowId)]; }
		[[nodiscard]] path_spec get_exd_path(const exh::page& page, game_language language) const;
	};
}

namespace xivres::excel::exd::row {
	struct locator {
		BE<uint32_t> RowId;
		BE<uint32_t> Offset;
	};

#pragma pack(push, 2)
	struct header {
		BE<uint32_t> DataSize;
		BE<uint16_t> SubRowCount;
	};
#pragma pack(pop)

	class reader {
		const std::span<const char> m_fixedData;
		const std::span<const char> m_fullData;
		mutable std::vector<std::optional<cell>> m_cells;

	public:
		const std::vector<exh::column>& Columns;
		reader(std::span<const char> fixedData, std::span<const char> fullData, const std::vector<exh::column>& columns);
		cell& operator[](size_t index) { return resolve_cell(index); }
		const cell& operator[](size_t index) const { return resolve_cell(index); }
		cell& at(size_t index);
		const cell& at(size_t index) const;
		size_t size() const { return m_cells.size(); }

		template<typename TParent, typename T, bool reversed>
		class base_iterator {
		public:
			using iterator = base_iterator<TParent, T, reversed>;
			using iterator_category = std::random_access_iterator_tag;
			using value_type = T;
			using difference_type = ptrdiff_t;
			using pointer = T*;
			using reference = T&;

		private:
			TParent* m_parent;
			difference_type m_index;

		public:
			base_iterator(TParent* parent, difference_type index)
				: m_parent(parent)
				, m_index(throw_if_out_of_boundary(index)) {}

			base_iterator(const iterator& r)
				: m_parent(r.m_parent)
				, m_index(r.m_index) {}

			~base_iterator() = default;

			iterator& operator=(const iterator& r) {
				m_parent = r.m_parent;
				m_index = r.m_index;
			}

			iterator& operator++() { //prefix increment
				m_index = throw_if_out_of_boundary(m_index + 1);
				return *this;
			}

			reference operator*() {
				return m_parent->resolve_cell(reversed ? m_parent->size() - 1 - m_index : m_index);
			}

			friend void swap(iterator& lhs, iterator& rhs) {
				std::swap(lhs.m_parent, rhs.m_parent);
				std::swap(lhs.m_index, rhs.m_index);
			}

			iterator operator++(int) { //postfix increment
				const auto index = m_index;
				m_index = throw_if_out_of_boundary(m_index + 1);
				return { m_parent, index };
			}

			pointer operator->() const {
				return &m_parent->resolve_cell(reversed ? m_parent->size() - 1 - m_index : m_index);
			}

			friend bool operator==(const iterator& l, const iterator& r) {
				return l.m_parent == r.m_parent && l.m_index == r.m_index;
			}

			friend bool operator!=(const iterator& l, const iterator& r) {
				return l.m_parent != r.m_parent || l.m_index != r.m_index;
			}

			reference operator*() const {
				return m_parent->resolve_cell(reversed ? m_parent->size() - 1 - m_index : m_index);
			}

			iterator& operator--() { //prefix decrement
				m_index = throw_if_out_of_boundary(m_index - 1);
				return *this;
			}

			iterator operator--(int) { //postfix decrement
				const auto index = m_index;
				m_index = throw_if_out_of_boundary(m_index - 1);
				return { m_parent, index };
			}

			friend bool operator<(const iterator& l, const iterator& r) {
				return l.m_index < r.m_index;
			}

			friend bool operator>(const iterator& l, const iterator& r) {
				return l.m_index > r.m_index;
			}

			friend bool operator<=(const iterator& l, const iterator& r) {
				return l.m_index <= r.m_index;
			}

			friend bool operator>=(const iterator& l, const iterator& r) {
				return l.m_index >= r.m_index;
			}

			iterator& operator+=(difference_type n) {
				m_index = throw_if_out_of_boundary(m_index + n);
				return *this;
			}

			friend iterator operator+(const iterator& l, difference_type r) {
				return iterator(l.m_parent, l.m_index + r);
			}

			friend iterator operator+(difference_type l, const iterator& r) {
				return iterator(r.m_parent, r.m_index + l);
			}

			iterator& operator-=(difference_type n) {
				m_index = throw_if_out_of_boundary(m_index - n);
				return *this;
			}

			friend iterator operator-(const iterator& l, difference_type r) {
				return iterator(l.m_parent, l.m_index - r);
			}

			friend difference_type operator-(const iterator& l, const iterator& r) {
				return r.m_index - l.m_index;
			}

			reference operator[](difference_type n) const {
				const auto index = throw_if_out_of_boundary(m_index + n);
				return m_parent->resolve_cell(reversed ? m_parent->size() - 1 - index : index);
			}

		private:
			size_t throw_if_out_of_boundary(size_t n) const {
				if (n > m_parent->size())
					throw std::out_of_range("Reached end of iterator.");
				if (n < 0)
					throw std::out_of_range("Reached beginning of iterator.");
				return n;
			}
		};

		using iterator = base_iterator<exd::row::reader, cell, false>;
		using const_iterator = base_iterator<const exd::row::reader, const cell, false>;
		using reverse_iterator = base_iterator<exd::row::reader, cell, true>;
		using const_reverse_iterator = base_iterator<const exd::row::reader, const cell, true>;

		iterator begin() { return iterator(this, 0); }
		const_iterator begin() const { return const_iterator(this, 0); }
		const_iterator cbegin() const { return const_iterator(this, 0); }
		iterator end() { return iterator(this, Columns.size()); }
		const_iterator end() const { return const_iterator(this, Columns.size()); }
		const_iterator cend() const { return const_iterator(this, Columns.size()); }
		reverse_iterator rbegin() { return reverse_iterator(this, 0); }
		const_reverse_iterator rbegin() const { return const_reverse_iterator(this, 0); }
		const_reverse_iterator crbegin() const { return const_reverse_iterator(this, 0); }
		reverse_iterator rend() { return reverse_iterator(this, Columns.size()); }
		const_iterator rend() const { const_reverse_iterator const_reverse_iterator(this, Columns.size()); }
		const_reverse_iterator crend() const { return const_reverse_iterator(this, Columns.size()); }

	private:
		cell& resolve_cell(size_t index) const;
	};

	class buffer {
		uint32_t m_rowId;
		exd::row::header m_rowHeader;
		std::vector<char> m_buffer;
		std::vector<exd::row::reader> m_rows;

	public:
		buffer();
		buffer(uint32_t rowId, const exh::reader& exh, const stream& strm, std::streamoff offset);
		[[nodiscard]] exd::row::reader& operator[](size_t index) { return m_rows.at(index); }
		[[nodiscard]] const exd::row::reader& operator[](size_t index) const { return m_rows.at(index); }
		[[nodiscard]] uint32_t RowId() const { return m_rowId; }
		size_t size() const { return m_rows.size(); }

		template<typename TParent, typename T, bool reversed>
		class base_iterator {
		public:
			using iterator = base_iterator<TParent, T, reversed>;
			using iterator_category = std::random_access_iterator_tag;
			using value_type = T;
			using difference_type = ptrdiff_t;
			using pointer = T*;
			using reference = T&;

		private:
			TParent* m_parent;
			difference_type m_index;

		public:
			base_iterator(TParent* parent, difference_type index)
				: m_parent(parent)
				, m_index(throw_if_out_of_boundary(index)) {}

			base_iterator(const iterator& r)
				: m_parent(r.m_parent)
				, m_index(r.m_index) {}

			~base_iterator() = default;

			iterator& operator=(const iterator& r) {
				m_parent = r.m_parent;
				m_index = r.m_index;
			}

			iterator& operator++() { //prefix increment
				m_index = throw_if_out_of_boundary(m_index + 1);
				return *this;
			}

			reference operator*() {
				return (*m_parent)[reversed ? m_parent->size() - 1 - m_index : m_index];
			}

			friend void swap(iterator& lhs, iterator& rhs) {
				std::swap(lhs.m_parent, rhs.m_parent);
				std::swap(lhs.m_index, rhs.m_index);
			}

			iterator operator++(int) { //postfix increment
				const auto index = m_index;
				m_index = throw_if_out_of_boundary(m_index + 1);
				return { m_parent, index };
			}

			pointer operator->() const {
				return &(*m_parent)[reversed ? m_parent->size() - 1 - m_index : m_index];
			}

			friend bool operator==(const iterator& l, const iterator& r) {
				return l.m_parent == r.m_parent && l.m_index == r.m_index;
			}

			friend bool operator!=(const iterator& l, const iterator& r) {
				return l.m_parent != r.m_parent || l.m_index != r.m_index;
			}

			reference operator*() const {
				return (*m_parent)[reversed ? m_parent->size() - 1 - m_index : m_index];
			}

			iterator& operator--() { //prefix decrement
				m_index = throw_if_out_of_boundary(m_index - 1);
				return *this;
			}

			iterator operator--(int) { //postfix decrement
				const auto index = m_index;
				m_index = throw_if_out_of_boundary(m_index - 1);
				return { m_parent, index };
			}

			friend bool operator<(const iterator& l, const iterator& r) {
				return l.m_index < r.m_index;
			}

			friend bool operator>(const iterator& l, const iterator& r) {
				return l.m_index > r.m_index;
			}

			friend bool operator<=(const iterator& l, const iterator& r) {
				return l.m_index <= r.m_index;
			}

			friend bool operator>=(const iterator& l, const iterator& r) {
				return l.m_index >= r.m_index;
			}

			iterator& operator+=(difference_type n) {
				m_index = throw_if_out_of_boundary(m_index + n);
				return *this;
			}

			friend iterator operator+(const iterator& l, difference_type r) {
				return iterator(l.m_parent, l.m_index + r);
			}

			friend iterator operator+(difference_type l, const iterator& r) {
				return iterator(r.m_parent, r.m_index + l);
			}

			iterator& operator-=(difference_type n) {
				m_index = throw_if_out_of_boundary(m_index - n);
				return *this;
			}

			friend iterator operator-(const iterator& l, difference_type r) {
				return iterator(l.m_parent, l.m_index - r);
			}

			friend difference_type operator-(const iterator& l, const iterator& r) {
				return r.m_index - l.m_index;
			}

			reference operator[](difference_type n) const {
				const auto index = throw_if_out_of_boundary(m_index + n);
				return (*m_parent)[reversed ? m_parent->size() - 1 - index : index];
			}

		private:
			size_t throw_if_out_of_boundary(size_t n) const {
				if (n > m_parent->size())
					throw std::out_of_range("Reached end of iterator.");
				if (n < 0)
					throw std::out_of_range("Reached beginning of iterator.");
				return n;
			}
		};

		using iterator = base_iterator<exd::row::buffer, exd::row::reader, false>;
		using const_iterator = base_iterator<const exd::row::buffer, const exd::row::reader, false>;
		using reverse_iterator = base_iterator<exd::row::buffer, exd::row::reader, true>;
		using const_reverse_iterator = base_iterator<const exd::row::buffer, const exd::row::reader, true>;

		iterator begin() { return iterator(this, 0); }
		const_iterator begin() const { return const_iterator(this, 0); }
		const_iterator cbegin() const { return const_iterator(this, 0); }
		iterator end() { return iterator(this, m_rows.size()); }
		const_iterator end() const { return const_iterator(this, m_rows.size()); }
		const_iterator cend() const { return const_iterator(this, m_rows.size()); }
		reverse_iterator rbegin() { return reverse_iterator(this, 0); }
		const_reverse_iterator rbegin() const { return const_reverse_iterator(this, 0); }
		const_reverse_iterator crbegin() const { return const_reverse_iterator(this, 0); }
		reverse_iterator rend() { return reverse_iterator(this, m_rows.size()); }
		const_reverse_iterator rend() const { return const_reverse_iterator(this, m_rows.size()); }
		const_reverse_iterator crend() const { return const_reverse_iterator(this, m_rows.size()); }
	};
}

namespace xivres::excel::exd {
	struct header {
		static constexpr char Signature_Value[4] = { 'E', 'X', 'D', 'F' };
		static constexpr uint16_t Version_Value = 2;

		char Signature[4]{};
		BE<uint16_t> Version;
		BE<uint16_t> Padding_0x006;
		BE<uint32_t> IndexSize;
		BE<uint32_t> DataSize;
		BE<uint32_t> Padding_0x010[4];
	};

	class reader {
	public:
		const std::shared_ptr<const stream> Stream;
		const exh::reader& ExhReader;
		const exd::header Header;

	private:
		std::vector<uint32_t> m_rowIds;
		std::vector<uint32_t> m_offsets;

		mutable std::vector<std::optional<exd::row::buffer>> m_rowBuffers;
		mutable std::mutex m_populateMtx;

	public:
		reader(const exh::reader& exh, std::shared_ptr<const stream> strm);
		[[nodiscard]] const exd::row::buffer& operator[](uint32_t rowId) const;
		[[nodiscard]] const std::vector<uint32_t>& get_row_ids() const { return m_rowIds; }
		size_t size() const { return m_rowIds.size(); }

		template<typename TParent, typename T, bool reversed>
		class base_iterator {
		public:
			using iterator = base_iterator<TParent, T, reversed>;
			using iterator_category = std::random_access_iterator_tag;
			using value_type = T;
			using difference_type = ptrdiff_t;
			using pointer = T*;
			using reference = T&;

		private:
			TParent* m_parent;
			difference_type m_index;

		public:
			base_iterator(TParent* parent, difference_type index)
				: m_parent(parent)
				, m_index(throw_if_out_of_boundary(index)) {}

			base_iterator(const iterator& r)
				: m_parent(r.m_parent)
				, m_index(r.m_index) {}

			~base_iterator() = default;

			iterator& operator=(const iterator& r) {
				m_parent = r.m_parent;
				m_index = r.m_index;
			}

			iterator& operator++() { //prefix increment
				m_index = throw_if_out_of_boundary(m_index + 1);
				return *this;
			}

			reference operator*() {
				return (*m_parent)[m_parent->m_rowIds[reversed ? m_parent->size() - 1 - m_index : m_index]];
			}

			friend void swap(iterator& lhs, iterator& rhs) {
				std::swap(lhs.m_parent, rhs.m_parent);
				std::swap(lhs.m_index, rhs.m_index);
			}

			iterator operator++(int) { //postfix increment
				const auto index = m_index;
				m_index = throw_if_out_of_boundary(m_index + 1);
				return { m_parent, index };
			}

			pointer operator->() const {
				return &(*m_parent)[m_parent->m_rowIds[reversed ? m_parent->size() - 1 - m_index : m_index]];
			}

			friend bool operator==(const iterator& l, const iterator& r) {
				return l.m_parent == r.m_parent && l.m_index == r.m_index;
			}

			friend bool operator!=(const iterator& l, const iterator& r) {
				return l.m_parent != r.m_parent || l.m_index != r.m_index;
			}

			reference operator*() const {
				return (*m_parent)[m_parent->m_rowIds[reversed ? m_parent->size() - 1 - m_index : m_index]];
			}

			iterator& operator--() { //prefix decrement
				m_index = throw_if_out_of_boundary(m_index - 1);
				return *this;
			}

			iterator operator--(int) { //postfix decrement
				const auto index = m_index;
				m_index = throw_if_out_of_boundary(m_index - 1);
				return { m_parent, index };
			}

		private:
			size_t throw_if_out_of_boundary(size_t n) const {
				if (n > m_parent->size())
					throw std::out_of_range("Reached end of iterator.");
				if (n < 0)
					throw std::out_of_range("Reached beginning of iterator.");
				return n;
			}
		};

		using iterator = base_iterator<const reader, const exd::row::buffer, false>;
		using reverse_iterator = base_iterator<const reader, const exd::row::buffer, true>;

		iterator begin() const { return iterator(this, 0); }
		iterator cbegin() const { return iterator(this, 0); }
		iterator end() const { return iterator(this, m_rowIds.size()); }
		iterator cend() const { return iterator(this, m_rowIds.size()); }
		reverse_iterator rbegin() const { return reverse_iterator(this, 0); }
		reverse_iterator crbegin() const { return reverse_iterator(this, 0); }
		reverse_iterator rend() const { return reverse_iterator(this, m_rowIds.size()); }
		reverse_iterator crend() const { return reverse_iterator(this, m_rowIds.size()); }
	};
}

namespace xivres::excel {
	class reader {
		const sqpack::reader* m_sqpackReader;
		std::optional<exh::reader> m_exhReader;
		game_language m_language;
		mutable std::vector<std::unique_ptr<exd::reader>> m_exdReaders;
		mutable std::mutex m_populateMtx;

	public:
		reader();
		reader(const sqpack::reader* sqpackReader, const std::string& name);
		reader(const reader& r);
		reader(reader&& r) noexcept;
		reader& operator=(const reader& r);
		reader& operator=(reader&& r) noexcept;
		void clear();
		reader new_with_language(game_language language);
		const xivres::game_language& get_language() const;
		reader& set_language(game_language language);
		const exh::reader& get_exh_reader() const;
		const exd::reader& get_exd_reader(size_t pageIndex) const;
		[[nodiscard]] const exd::row::buffer& operator[](uint32_t rowId) const;
	};
}

#endif
