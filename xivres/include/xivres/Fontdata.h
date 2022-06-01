#ifndef _XIVRES_Fontdata_H_
#define _XIVRES_Fontdata_H_

#include <map>
#include <stdexcept>

#include "util.byte_order.h"

#include "common.h"
#include "stream.h"
#include "util.unicode.h"

namespace xivres {
	struct FontdataHeader {
		static constexpr char Signature_Value[8] = {
			'f', 'c', 's', 'v', '0', '1', '0', '0',
		};

		char Signature[8]{};
		LE<uint32_t> FontTableHeaderOffset;
		LE<uint32_t> KerningHeaderOffset;
		uint8_t Padding_0x10[0x10]{};
	};
	static_assert(sizeof FontdataHeader == 0x20);

	struct FontdataGlyphTableHeader {
		static constexpr char Signature_Value[4] = {
			'f', 't', 'h', 'd',
		};

		char Signature[4]{};
		LE<uint32_t> FontTableEntryCount;
		LE<uint32_t> KerningEntryCount;
		uint8_t Padding_0x0C[4]{};
		LE<uint16_t> TextureWidth;
		LE<uint16_t> TextureHeight;
		LE<float> Size{ 0.f };
		LE<uint32_t> LineHeight;
		LE<uint32_t> Ascent;
	};
	static_assert(sizeof FontdataGlyphTableHeader == 0x20);

	struct FontdataGlyphEntry {
		static constexpr size_t ChannelMap[4]{ 2, 1, 0, 3 };

		LE<uint32_t> Utf8Value;
		LE<uint16_t> ShiftJisValue;
		LE<uint16_t> TextureIndex;
		LE<uint16_t> TextureOffsetX;
		LE<uint16_t> TextureOffsetY;
		LE<uint8_t> BoundingWidth;
		LE<uint8_t> BoundingHeight;
		LE<int8_t> NextOffsetX;
		LE<int8_t> CurrentOffsetY;

		[[nodiscard]] char32_t Char() const {
			return util::unicode::u8uint32_to_u32(Utf8Value);
		}

		char32_t Char(char32_t newValue) {
			Utf8Value = util::unicode::u32_to_u8uint32(newValue);
			ShiftJisValue = util::unicode::u32_to_sjisuint16(newValue);
			return newValue;
		}

		uint16_t TextureFileIndex() const {
			return *TextureIndex >> 2;
		}

		uint16_t TexturePlaneIndex() const {
			return *TextureIndex & 3;
		}

		auto operator<=>(const FontdataGlyphEntry& r) const {
			return *Utf8Value <=> *r.Utf8Value;
		}

		auto operator<=>(uint32_t r) const {
			return *Utf8Value <=> r;
		}
	};
	static_assert(sizeof FontdataGlyphEntry == 0x10);

	struct FontdataKerningTableHeader {
		static constexpr char Signature_Value[4] = {
			'k', 'n', 'h', 'd',
		};

		char Signature[4]{};
		LE<uint32_t> EntryCount;
		uint8_t Padding_0x08[8]{};
	};
	static_assert(sizeof FontdataKerningTableHeader == 0x10);

	struct FontdataKerningEntry {
		LE<uint32_t> LeftUtf8Value;
		LE<uint32_t> RightUtf8Value;
		LE<uint16_t> LeftShiftJisValue;
		LE<uint16_t> RightShiftJisValue;
		LE<int32_t> RightOffset;

		[[nodiscard]] char32_t Left() const {
			return util::unicode::u8uint32_to_u32(LeftUtf8Value);
		}

		char32_t Left(char32_t newValue) {
			LeftUtf8Value = util::unicode::u32_to_u8uint32(newValue);
			LeftShiftJisValue = util::unicode::u32_to_sjisuint16(newValue);
			return newValue;
		}

		[[nodiscard]] char32_t Right() const {
			return util::unicode::u8uint32_to_u32(RightUtf8Value);
		}

		char32_t Right(char32_t newValue) {
			RightUtf8Value = util::unicode::u32_to_u8uint32(newValue);
			RightShiftJisValue = util::unicode::u32_to_sjisuint16(newValue);
			return newValue;
		}

		auto operator<=>(const FontdataKerningEntry& r) const {
			if (const auto v = (*LeftUtf8Value <=> *r.LeftUtf8Value); v != std::strong_ordering::equal)
				return v;
			return *RightUtf8Value <=> *r.RightUtf8Value;
		}
	};
	static_assert(sizeof FontdataKerningEntry == 0x10);

	class FontdataStream : public default_base_stream {
		FontdataHeader m_fcsv;
		FontdataGlyphTableHeader m_fthd;
		std::vector<FontdataGlyphEntry> m_fontTableEntries;
		FontdataKerningTableHeader m_knhd;
		std::vector<FontdataKerningEntry> m_kerningEntries;

	public:
		FontdataStream() {
			memcpy(m_fcsv.Signature, FontdataHeader::Signature_Value, sizeof m_fcsv.Signature);
			memcpy(m_fthd.Signature, FontdataGlyphTableHeader::Signature_Value, sizeof m_fthd.Signature);
			memcpy(m_knhd.Signature, FontdataKerningTableHeader::Signature_Value, sizeof m_knhd.Signature);
			m_fcsv.FontTableHeaderOffset = static_cast<uint32_t>(sizeof m_fcsv);
			m_fcsv.KerningHeaderOffset = static_cast<uint32_t>(sizeof m_fcsv + sizeof m_fthd);
		}

		FontdataStream(const stream& strm, bool strict = false)
			: m_fcsv(strm.read_fully<FontdataHeader>(0))
			, m_fthd(strm.read_fully<FontdataGlyphTableHeader>(m_fcsv.FontTableHeaderOffset))
			, m_fontTableEntries(strm.read_vector<FontdataGlyphEntry>(m_fcsv.FontTableHeaderOffset + sizeof m_fthd, m_fthd.FontTableEntryCount, 0x1000000))
			, m_knhd(strm.read_fully<FontdataKerningTableHeader>(m_fcsv.KerningHeaderOffset))
			, m_kerningEntries(strm.read_vector<FontdataKerningEntry>(m_fcsv.KerningHeaderOffset + sizeof m_knhd, (std::min)(m_knhd.EntryCount, m_fthd.KerningEntryCount), 0x1000000)) {
			if (strict) {
				if (0 != memcmp(m_fcsv.Signature, FontdataHeader::Signature_Value, sizeof m_fcsv.Signature))
					throw bad_data_error("fcsv.Signature != \"fcsv0100\"");
				if (m_fcsv.FontTableHeaderOffset != sizeof FontdataHeader)
					throw bad_data_error("FontTableHeaderOffset != sizeof FontdataHeader");
				if (!util::all_same_value(m_fcsv.Padding_0x10))
					throw bad_data_error("fcsv.Padding_0x10 != 0");

				if (0 != memcmp(m_fthd.Signature, FontdataGlyphTableHeader::Signature_Value, sizeof m_fthd.Signature))
					throw bad_data_error("fthd.Signature != \"fthd\"");
				if (!util::all_same_value(m_fthd.Padding_0x0C))
					throw bad_data_error("fthd.Padding_0x0C != 0");

				if (0 != memcmp(m_knhd.Signature, FontdataKerningTableHeader::Signature_Value, sizeof m_knhd.Signature))
					throw bad_data_error("knhd.Signature != \"knhd\"");
				if (!util::all_same_value(m_knhd.Padding_0x08))
					throw bad_data_error("knhd.Padding_0x08 != 0");

				if (m_knhd.EntryCount != m_fthd.KerningEntryCount)
					throw std::runtime_error("knhd.EntryCount != fthd.KerningEntryCount");
			}
			std::ranges::sort(m_fontTableEntries, [](const FontdataGlyphEntry& l, const FontdataGlyphEntry& r) {
				return l.Utf8Value < r.Utf8Value;
			});
			std::ranges::sort(m_kerningEntries, [](const FontdataKerningEntry& l, const FontdataKerningEntry& r) {
				if (l.LeftUtf8Value == r.LeftUtf8Value)
					return l.RightUtf8Value < r.RightUtf8Value;
				return l.LeftUtf8Value < r.LeftUtf8Value;
			});
		}

		[[nodiscard]] std::streamsize size() const override {
			return sizeof m_fcsv
				+ sizeof m_fthd
				+ std::span(m_fontTableEntries).size_bytes()
				+ sizeof m_knhd
				+ std::span(m_kerningEntries).size_bytes();
		}

		std::streamsize read(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (!length)
				return 0;

			auto relativeOffset = offset;
			auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

			if (relativeOffset < sizeof m_fcsv) {
				const auto src = util::span_cast<char>(1, &m_fcsv).subspan(static_cast<size_t>(relativeOffset));
				const auto available = (std::min)(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					return length;
			} else
				relativeOffset -= sizeof m_fcsv;

			if (relativeOffset < sizeof m_fthd) {
				const auto src = util::span_cast<char>(1, &m_fthd).subspan(static_cast<size_t>(relativeOffset));
				const auto available = (std::min)(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					return length;
			} else
				relativeOffset -= sizeof m_fthd;

			if (const auto srcTyped = std::span(m_fontTableEntries);
				relativeOffset < static_cast<std::streamoff>(srcTyped.size_bytes())) {
				const auto src = util::span_cast<char>(srcTyped).subspan(static_cast<size_t>(relativeOffset));
				const auto available = (std::min)(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					return length;
			} else
				relativeOffset -= srcTyped.size_bytes();

			if (relativeOffset < sizeof m_knhd) {
				const auto src = util::span_cast<char>(1, &m_knhd).subspan(static_cast<size_t>(relativeOffset));
				const auto available = (std::min)(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					return length;
			} else
				relativeOffset -= sizeof m_knhd;

			if (const auto srcTyped = std::span(m_kerningEntries);
				relativeOffset < static_cast<std::streamoff>(srcTyped.size_bytes())) {
				const auto src = util::span_cast<char>(srcTyped).subspan(static_cast<size_t>(relativeOffset));
				const auto available = (std::min)(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);

				if (out.empty())
					return length;
			}

			return length - out.size_bytes();
		}

		[[nodiscard]] const FontdataGlyphEntry* GetFontEntry(char32_t c) const {
			const auto val = util::unicode::u32_to_u8uint32(c);
			const auto it = std::lower_bound(m_fontTableEntries.begin(), m_fontTableEntries.end(), val,
				[](const FontdataGlyphEntry& l, uint32_t r) {
				return l.Utf8Value < r;
			});
			if (it == m_fontTableEntries.end() || it->Utf8Value != val)
				return nullptr;
			return &*it;
		}

		[[nodiscard]] int GetKerningDistance(char32_t l, char32_t r) const {
			const auto pair = std::make_pair(util::unicode::u32_to_u8uint32(l), util::unicode::u32_to_u8uint32(r));
			const auto it = std::lower_bound(m_kerningEntries.begin(), m_kerningEntries.end(), pair,
				[](const FontdataKerningEntry& l, const std::pair<uint32_t, uint32_t>& r) {
				if (l.LeftUtf8Value == r.first)
					return l.RightUtf8Value < r.second;
				return l.LeftUtf8Value < r.first;
			});
			if (it == m_kerningEntries.end() || it->LeftUtf8Value != pair.first || it->RightUtf8Value != pair.second)
				return 0;
			return it->RightOffset;
		}

		[[nodiscard]] const std::vector<FontdataGlyphEntry>& GetFontTableEntries() const {
			return m_fontTableEntries;
		}

		[[nodiscard]] const std::vector<FontdataKerningEntry>& GetKerningEntries() const {
			return m_kerningEntries;
		}

		void ReserveFontEntries(size_t count) {
			m_fontTableEntries.reserve(count);
		}

		void ReserveKerningEntries(size_t count) {
			m_kerningEntries.reserve(count);
		}

		void AddFontEntry(char32_t c, uint16_t textureIndex, uint16_t textureOffsetX, uint16_t textureOffsetY, uint8_t boundingWidth, uint8_t boundingHeight, int8_t nextOffsetX, int8_t currentOffsetY) {
			if (textureOffsetX >= 4096 || textureOffsetY >= 4096)
				throw std::invalid_argument("Texture Offset X and Y must be lesser than 4096.");
			if (textureIndex >= 256)
				throw std::invalid_argument("Texture Index cannot be bigger than 255.");

			const auto val = util::unicode::u32_to_u8uint32(c);

			auto it = std::lower_bound(m_fontTableEntries.begin(), m_fontTableEntries.end(), val, [](const FontdataGlyphEntry& l, uint32_t r) {
				return l.Utf8Value < r;
			});

			if (it == m_fontTableEntries.end() || it->Utf8Value != val) {
				if (m_fontTableEntries.size() >= 65535)
					throw std::runtime_error("The game supports up to 65535 characters.");
				auto entry = FontdataGlyphEntry();
				entry.Utf8Value = val;
				entry.ShiftJisValue = util::unicode::u32_to_sjisuint16(val);
				it = m_fontTableEntries.insert(it, entry);
				m_fcsv.KerningHeaderOffset += sizeof entry;
				m_fthd.FontTableEntryCount += 1;
			}

			it->TextureIndex = textureIndex;
			it->TextureOffsetX = textureOffsetX;
			it->TextureOffsetY = textureOffsetY;
			it->BoundingWidth = boundingWidth;
			it->BoundingHeight = boundingHeight;
			it->NextOffsetX = nextOffsetX;
			it->CurrentOffsetY = currentOffsetY;
		}

		void AddFontEntry(const FontdataGlyphEntry& entry) {
			if (entry.TextureOffsetX >= 4096 || entry.TextureOffsetY >= 4096)
				throw std::invalid_argument("Texture Offset X and Y must be lesser than 4096.");
			if (entry.TextureIndex >= 256)
				throw std::invalid_argument("Texture Index cannot be bigger than 255.");

			auto it = m_fontTableEntries.end();

			if (m_fontTableEntries.empty() || m_fontTableEntries.back() < entry)
				void();
			else if (entry < m_fontTableEntries.front())
				it = m_fontTableEntries.begin();
			else
				it = std::lower_bound(m_fontTableEntries.begin(), m_fontTableEntries.end(), entry);

			if (it == m_fontTableEntries.end() || it->Utf8Value != entry.Utf8Value) {
				if (m_fontTableEntries.size() >= 65535)
					throw std::runtime_error("The game supports up to 65535 characters.");
				it = m_fontTableEntries.insert(it, entry);
				m_fcsv.KerningHeaderOffset += sizeof entry;
				m_fthd.FontTableEntryCount += 1;
			} else
				*it = entry;
		}

		void AddKerning(char32_t l, char32_t r, int rightOffset, bool cumulative = false) {
			auto entry = FontdataKerningEntry();
			entry.Left(l);
			entry.Right(r);
			entry.RightOffset = rightOffset;

			AddKerning(entry, cumulative);
		}

		void AddKerning(const FontdataKerningEntry& entry, bool cumulative = false) {
			auto it = m_kerningEntries.end();

			if (m_kerningEntries.empty() || m_kerningEntries.back() < entry)
				void();
			else if (entry < m_kerningEntries.front())
				it = m_kerningEntries.begin();
			else
				it = std::lower_bound(m_kerningEntries.begin(), m_kerningEntries.end(), entry);

			if (it != m_kerningEntries.end() && it->LeftUtf8Value == entry.LeftUtf8Value && it->RightUtf8Value == entry.RightUtf8Value) {
				if (entry.RightOffset)
					it->RightOffset = entry.RightOffset + (cumulative ? *it->RightOffset : 0);
				else
					m_kerningEntries.erase(it);
			} else if (entry.RightOffset) {
				if (m_kerningEntries.size() >= 65535)
					throw std::runtime_error("The game supports up to 65535 kerning pairs.");
				m_kerningEntries.insert(it, entry);
			}

			m_fthd.KerningEntryCount = m_knhd.EntryCount = static_cast<uint32_t>(m_kerningEntries.size());
		}

		[[nodiscard]] uint16_t TextureWidth() const {
			return m_fthd.TextureWidth;
		}

		[[nodiscard]] uint16_t TextureHeight() const {
			return m_fthd.TextureHeight;
		}

		void TextureWidth(uint16_t v) {
			m_fthd.TextureWidth = v;
		}

		void TextureHeight(uint16_t v) {
			m_fthd.TextureHeight = v;
		}

		[[nodiscard]] float Size() const {
			return m_fthd.Size;
		}

		void Size(float v) {
			m_fthd.Size = v;
		}

		[[nodiscard]] uint32_t LineHeight() const {
			return m_fthd.LineHeight;
		}

		void LineHeight(uint32_t v) {
			m_fthd.LineHeight = v;
		}

		[[nodiscard]] uint32_t Ascent() const {
			return m_fthd.Ascent;
		}

		void Ascent(uint32_t v) {
			m_fthd.Ascent = v;
		}
	};
}

#endif
