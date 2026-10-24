#pragma once

#include <concepts>
#include <cstddef>
#include <cwchar>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

namespace kota {

/// A UTF-16 code unit type: char16_t, or wchar_t where it is 16-bit (Windows).
template <typename Char>
concept utf16_unit = std::same_as<Char, char16_t> ||
                     (std::same_as<Char, wchar_t> && sizeof(wchar_t) == sizeof(char16_t));

namespace detail {

template <typename Char>
std::string from_utf16(std::basic_string_view<Char> text) {
    std::string result;
    result.reserve(text.size());
    for(std::size_t at = 0; at < text.size(); ++at) {
        char32_t unit = static_cast<char16_t>(text[at]);
        char32_t code_point = unit;
        if(unit >= 0xD800 && unit <= 0xDBFF && at + 1 < text.size()) {
            char32_t next = static_cast<char16_t>(text[at + 1]);
            if(next >= 0xDC00 && next <= 0xDFFF) {
                code_point = 0x10000 + ((unit - 0xD800) << 10) + (next - 0xDC00);
                ++at;
            }
        }
        if(code_point < 0x80) {
            result.push_back(static_cast<char>(code_point));
        } else if(code_point < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
            result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else if(code_point < 0x10000) {
            // A lone surrogate comes out here too, as WTF-8 spells it.
            result.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
            result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
            result.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
    }
    return result;
}

}  // namespace detail

/// `text`, WTF-8, as UTF-16: UTF-8 that may also spell a lone surrogate in
/// three bytes, as from_utf16() writes one a Windows name holds. Fails with
/// std::errc::illegal_byte_sequence on anything else: a byte no sequence
/// starts with, a sequence cut short, an overlong form, a code point past
/// U+10FFFF, and a surrogate pair spelled as two lone surrogates (CESU-8),
/// which UTF-8 spells in four bytes. A NUL converts like any code point.
template <utf16_unit Char = char16_t>
std::expected<std::basic_string<Char>, std::error_code> to_utf16(std::string_view text) {
    auto invalid = std::unexpected(std::make_error_code(std::errc::illegal_byte_sequence));
    std::basic_string<Char> result;
    result.reserve(text.size());
    for(std::size_t at = 0; at < text.size();) {
        auto lead = static_cast<unsigned char>(text[at]);
        if(lead < 0x80) {
            result.push_back(static_cast<Char>(lead));
            ++at;
            continue;
        }
        // The range of the second byte narrows for the leads whose code
        // points would be overlong or past U+10FFFF. Unlike UTF-8, `ED` takes
        // A0..BF too: the lone surrogates.
        std::size_t need = 0;
        char32_t code_point = 0;
        unsigned char low = 0x80;
        unsigned char high = 0xBF;
        if(lead >= 0xC2 && lead <= 0xDF) {
            need = 2;
            code_point = lead & 0x1F;
        } else if(lead >= 0xE0 && lead <= 0xEF) {
            need = 3;
            code_point = lead & 0x0F;
            low = lead == 0xE0 ? 0xA0 : low;
        } else if(lead >= 0xF0 && lead <= 0xF4) {
            need = 4;
            code_point = lead & 0x07;
            low = lead == 0xF0 ? 0x90 : low;
            high = lead == 0xF4 ? 0x8F : high;
        } else {
            return invalid;
        }
        if(text.size() - at < need) {
            return invalid;
        }
        for(std::size_t i = 1; i < need; ++i) {
            auto next = static_cast<unsigned char>(text[at + i]);
            if(next < low || next > high) {
                return invalid;
            }
            code_point = (code_point << 6) | (next & 0x3F);
            low = 0x80;
            high = 0xBF;
        }
        // A high surrogate right before a low one (`ED B0..BF`): CESU-8.
        if(code_point >= 0xD800 && code_point <= 0xDBFF && text.size() - at - need >= 2 &&
           static_cast<unsigned char>(text[at + need]) == 0xED &&
           (static_cast<unsigned char>(text[at + need + 1]) & 0xF0) == 0xB0) {
            return invalid;
        }
        if(code_point < 0x10000) {
            result.push_back(static_cast<Char>(code_point));
        } else {
            code_point -= 0x10000;
            result.push_back(static_cast<Char>(0xD800 + (code_point >> 10)));
            result.push_back(static_cast<Char>(0xDC00 + (code_point & 0x3FF)));
        }
        at += need;
    }
    return result;
}

/// `text` as WTF-8: UTF-8, with each lone surrogate, which a Windows name
/// may hold, in the three bytes to_utf16() reads back as that surrogate.
inline std::string from_utf16(std::u16string_view text) {
    return detail::from_utf16(text);
}

#if WCHAR_MAX <= 0xFFFF
/// The same, for the wide strings of the Windows API.
inline std::string from_utf16(std::wstring_view text) {
    return detail::from_utf16(text);
}
#endif

}  // namespace kota
