#pragma once

#include <cstddef>
#include <cwchar>
#include <string_view>
#include <system_error>

#include "kota/support/buffer_ref.h"
#include "kota/support/utf8.h"

namespace kota {

namespace detail {

template <typename Char>
void from_utf16(buffer_ref out, std::basic_string_view<Char> text) {
    // Three bytes at most for a unit, four for a pair of them.
    auto chars = out.resize(text.size() * 3);
    std::size_t length = 0;
    auto put = [&](char32_t byte) {
        chars[length++] = static_cast<char>(byte);
    };
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
            put(code_point);
        } else if(code_point < 0x800) {
            put(0xC0 | (code_point >> 6));
            put(0x80 | (code_point & 0x3F));
        } else if(code_point < 0x10000) {
            // A lone surrogate comes out here too, as WTF-8 spells it.
            put(0xE0 | (code_point >> 12));
            put(0x80 | ((code_point >> 6) & 0x3F));
            put(0x80 | (code_point & 0x3F));
        } else {
            put(0xF0 | (code_point >> 18));
            put(0x80 | ((code_point >> 12) & 0x3F));
            put(0x80 | ((code_point >> 6) & 0x3F));
            put(0x80 | (code_point & 0x3F));
        }
    }
    out.resize(length);
}

template <typename Char>
std::error_code to_utf16(basic_buffer_ref<Char> out, std::string_view text) {
    // A unit at most for each byte.
    auto units = out.resize(text.size());
    std::size_t length = 0;
    for(std::size_t at = 0; at < text.size();) {
        auto sequence = decode_sequence(text.substr(at), true);
        if(!sequence.valid) {
            out.resize(length);
            return std::make_error_code(std::errc::illegal_byte_sequence);
        }
        at += sequence.length;
        auto code_point = sequence.code_point;
        // A high surrogate right before a low one (`ED B0..BF`): CESU-8.
        if(code_point >= 0xD800 && code_point <= 0xDBFF && text.size() - at >= 2 &&
           static_cast<unsigned char>(text[at]) == 0xED &&
           (static_cast<unsigned char>(text[at + 1]) & 0xF0) == 0xB0) {
            out.resize(length);
            return std::make_error_code(std::errc::illegal_byte_sequence);
        }
        if(code_point < 0x10000) {
            units[length++] = static_cast<Char>(code_point);
        } else {
            code_point -= 0x10000;
            units[length++] = static_cast<Char>(0xD800 + (code_point >> 10));
            units[length++] = static_cast<Char>(0xDC00 + (code_point & 0x3FF));
        }
    }
    out.resize(length);
    return {};
}

}  // namespace detail

/// Writes `text`, WTF-8, into `out` as UTF-16: UTF-8 that may also spell a
/// lone surrogate in three bytes, as from_utf16() writes one a Windows name
/// holds. Fails with std::errc::illegal_byte_sequence on anything else, `out`
/// then holding what came before it: a byte no sequence starts with, a
/// sequence cut short, an overlong form, a code point past U+10FFFF, and a
/// surrogate pair spelled as two lone surrogates (CESU-8), which UTF-8
/// spells in four bytes. A NUL converts like any code point.
inline std::error_code to_utf16(basic_buffer_ref<char16_t> out, std::string_view text) {
    return detail::to_utf16(out, text);
}

/// Writes `text` into `out` as WTF-8: UTF-8, with each lone surrogate, which
/// a Windows name may hold, in the three bytes to_utf16() reads back as that
/// surrogate.
inline void from_utf16(buffer_ref out, std::u16string_view text) {
    detail::from_utf16(out, text);
}

#if WCHAR_MAX <= 0xFFFF
/// The same, for the wide strings of the Windows API.
inline std::error_code to_utf16(basic_buffer_ref<wchar_t> out, std::string_view text) {
    return detail::to_utf16(out, text);
}

inline void from_utf16(buffer_ref out, std::wstring_view text) {
    detail::from_utf16(out, text);
}
#endif

}  // namespace kota
