#ifndef _XIVRES_UNICODE_H_
#define _XIVRES_UNICODE_H_

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>

namespace xivres::util::unicode {
	constexpr char32_t UReplacement = U'\uFFFD';
	constexpr char32_t UInvalid = U'\uFFFF';

	char32_t u8uint32_to_u32(uint32_t n);

	uint32_t u32_to_u8uint32(char32_t codepoint);

	uint16_t u32_to_sjisuint16(char32_t codepoint);

	template<typename T> struct EncodingTag {};

	size_t decode(EncodingTag<char8_t>, char32_t& out, const char8_t* in, size_t nRemainingBytes, bool strict);
	size_t decode(EncodingTag<char16_t>, char32_t& out, const char16_t* in, size_t nRemainingBytes, bool strict);
	size_t decode(EncodingTag<char32_t>, char32_t& out, const char32_t* in, size_t nRemainingBytes, bool strict);
	size_t decode(EncodingTag<char>, char32_t& out, const char* in, size_t nRemainingBytes, bool strict);
	size_t decode(EncodingTag<wchar_t>, char32_t& out, const wchar_t* in, size_t nRemainingBytes, bool strict);

	template<typename T>
	inline size_t decode(char32_t& out, const T* in, size_t nRemainingBytes, bool strict = true) {
		return decode(EncodingTag<T>(), out, in, nRemainingBytes, strict);
	}

	size_t encode(EncodingTag<char8_t>, char8_t* ptr, char32_t c, bool strict);
	size_t encode(EncodingTag<char16_t>, char16_t* ptr, char32_t c, bool strict);
	size_t encode(EncodingTag<char32_t>, char32_t* ptr, char32_t c, bool strict);
	size_t encode(EncodingTag<char>, char* ptr, char32_t c, bool strict);
	size_t encode(EncodingTag<wchar_t>, wchar_t* ptr, char32_t c, bool strict);

	template<typename T>
	inline size_t encode(T* ptr, char32_t c, bool strict = true) {
		return encode(EncodingTag<T>(), ptr, c, strict);
	}

	template<class TTo>
	inline TTo& ConvertFromChar(TTo& out, char32_t c, bool strict = true) {
		const auto encLen = util::unicode::encode<TTo::value_type>(nullptr, c, strict);
		const auto baseIndex = out.size();
		out.resize(baseIndex + encLen);
		util::unicode::encode(&out[baseIndex], c, strict);
		return out;
	}

	template<class TTo>
	inline TTo ConvertFromChar(char32_t c, bool strict = true) {
		TTo out{};
		return ConvertFromChar(out, c, strict);
	}

	const char32_t* codepoint_name(char32_t c);

	template<class TTo>
	inline TTo& represent_codepoint(TTo& out, char32_t c, bool strict = true) {
		if (const auto name = codepoint_name(c))
			return convert<TTo>(out, name);
		return ConvertFromChar<TTo>(out, c, strict);
	}

	template<class TTo>
	inline TTo represent_codepoint(char32_t c, bool strict = true) {
		TTo out{};
		return represent_codepoint(out, c, strict);
	}

	template<class TTo, class TFromElem, class TFromTraits = std::char_traits<TFromElem>>
	inline TTo& convert(TTo& out, const std::basic_string_view<TFromElem, TFromTraits>& in, bool strict = true) {
		out.reserve(out.size() + in.size() * 4 / sizeof(in[0]) / sizeof(out[0]));

		char32_t c{};
		for (size_t decLen = 0, decIdx = 0; decIdx < in.size() && (decLen = util::unicode::decode(c, &in[decIdx], in.size() - decIdx, strict)); decIdx += decLen) {
			const auto encIdx = out.size();
			const auto encLen = util::unicode::encode<TTo::value_type>(nullptr, c, strict);
			out.resize(encIdx + encLen);
			util::unicode::encode(&out[encIdx], c, strict);
		}

		return out;
	}

	template<class TTo, class TFromElem, class TFromTraits = std::char_traits<TFromElem>, class TFromAlloc = std::allocator<TFromElem>>
	inline TTo& convert(TTo& out, const std::basic_string<TFromElem, TFromTraits, TFromAlloc>& in, bool strict = true) {
		return convert(out, std::basic_string_view<TFromElem, TFromTraits>(in), strict);
	}

	template<class TTo, class TFromElem, typename = std::enable_if_t<std::is_integral_v<TFromElem>>>
	inline TTo& convert(TTo& out, const TFromElem* in, size_t length = (std::numeric_limits<size_t>::max)(), bool strict = true) {
		if (length == (std::numeric_limits<size_t>::max)())
			length = std::char_traits<TFromElem>::length(in);

		return convert(out, std::basic_string_view<TFromElem>(in, length), strict);
	}

	template<class TTo, class TFromElem, class TFromTraits = std::char_traits<TFromElem>>
	inline TTo convert(const std::basic_string_view<TFromElem, TFromTraits>& in, bool strict = true) {
		TTo out{};
		return convert(out, in, strict);
	}

	template<class TTo, class TFromElem, class TFromTraits = std::char_traits<TFromElem>, class TFromAlloc = std::allocator<TFromElem>>
	inline TTo convert(const std::basic_string<TFromElem, TFromTraits, TFromAlloc>& in, bool strict = true) {
		TTo out{};
		return convert(out, std::basic_string_view<TFromElem, TFromTraits>(in), strict);
	}

	template<class TTo, class TFromElem, typename = std::enable_if_t<std::is_integral_v<TFromElem>>>
	inline TTo convert(const TFromElem* in, size_t length = (std::numeric_limits<size_t>::max)(), bool strict = true) {
		if (length == (std::numeric_limits<size_t>::max)())
			length = std::char_traits<TFromElem>::length(in);

		TTo out{};
		return convert(out, std::basic_string_view<TFromElem>(in, length), strict);
	}

	template<>
	inline const std::u8string& convert(const std::u8string& in, bool strict) {
		return in;
	}

	template<>
	inline const std::u16string& convert(const std::u16string& in, bool strict) {
		return in;
	}

	template<>
	inline const std::u32string& convert(const std::u32string& in, bool strict) {
		return in;
	}

	template<>
	inline const std::string& convert(const std::string& in, bool strict) {
		return in;
	}

	template<>
	inline const std::wstring& convert(const std::wstring& in, bool strict) {
		return in;
	}

	namespace blocks {
		enum purpose_flags : uint64_t {
			LTR = 0,
			RTL = 1 << 0,
			Invalid = 1 << 1,
			UsedWithCombining = 1 << 22,
		};

		inline constexpr purpose_flags operator|(purpose_flags a, purpose_flags b) {
			return static_cast<purpose_flags>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
		}

		inline constexpr purpose_flags operator&(purpose_flags a, purpose_flags b) {
			return static_cast<purpose_flags>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
		}

		inline constexpr purpose_flags operator~(purpose_flags a) {
			return static_cast<purpose_flags>(~static_cast<uint64_t>(a));
		}

		enum negative_lsb_group : uint64_t {
			None,
			Combining,
			Cyrillic,
			Thai,
		};

		struct block_definition {
			char32_t First;
			char32_t Last;
			const char* Name;
			purpose_flags Purpose;
			negative_lsb_group NegativeLsbGroup = None;
		};

		std::span<const block_definition> all_blocks();

		const block_definition& block_for(char32_t codepoint);
	}
}

#endif
