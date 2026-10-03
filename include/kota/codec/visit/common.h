#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace kota::codec {

/// How the text backends (json, toml, dyn) carry a char: its value, 0 to
/// 255, is one codepoint, written in UTF-8 — one byte below 0x80, two above.
inline std::string char_to_utf8(char c) {
    auto codepoint = static_cast<unsigned char>(c);
    if(codepoint < 0x80) {
        return std::string(1, c);
    }
    return {static_cast<char>(0xC0 | (codepoint >> 6)),
            static_cast<char>(0x80 | (codepoint & 0x3F))};
}

/// The char `text` spells as char_to_utf8 writes it, if it spells one: a
/// single codepoint up to U+00FF.
inline std::optional<char> char_from_utf8(std::string_view text) {
    auto byte = [&](std::size_t at) {
        return static_cast<unsigned char>(text[at]);
    };
    if(text.size() == 1 && byte(0) < 0x80) {
        return text[0];
    }
    if(text.size() == 2 && (byte(0) == 0xC2 || byte(0) == 0xC3) && (byte(1) & 0xC0) == 0x80) {
        return static_cast<char>(((byte(0) & 0x1F) << 6) | (byte(1) & 0x3F));
    }
    return std::nullopt;
}

namespace detail {

/// The bytes at the start of text: their length, and whether they make one
/// code point. When they do not, the length is that of the longest prefix of
/// a code point they start with, at least one byte: the maximal subpart a
/// UTF-8 decoder replaces with one U+FFFD.
struct Utf8Step {
    std::size_t length;
    bool valid;
};

constexpr Utf8Step utf8_step(std::string_view text) {
    auto lead = static_cast<unsigned char>(text[0]);
    if(lead < 0x80) {
        return {.length = 1, .valid = true};
    }
    // The range of the second byte narrows for the leads whose code points
    // would be overlong, surrogates or past U+10FFFF.
    std::size_t need = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    if(lead >= 0xC2 && lead <= 0xDF) {
        need = 2;
    } else if(lead >= 0xE0 && lead <= 0xEF) {
        need = 3;
        low = lead == 0xE0 ? 0xA0 : low;
        high = lead == 0xED ? 0x9F : high;
    } else if(lead >= 0xF0 && lead <= 0xF4) {
        need = 4;
        low = lead == 0xF0 ? 0x90 : low;
        high = lead == 0xF4 ? 0x8F : high;
    } else {
        return {.length = 1, .valid = false};
    }
    std::size_t length = 1;
    while(length < need && length < text.size()) {
        auto next = static_cast<unsigned char>(text[length]);
        if(next < low || next > high) {
            break;
        }
        low = 0x80;
        high = 0xBF;
        ++length;
    }
    return {.length = length, .valid = length == need};
}

}  // namespace detail

/// Whether text is UTF-8: whole code points, none overlong, a surrogate or
/// past U+10FFFF. Runs of ASCII pass eight bytes at a time.
inline bool is_utf8(std::string_view text) {
    for(std::size_t at = 0; at < text.size();) {
        if(text.size() - at >= sizeof(std::uint64_t)) {
            std::uint64_t word;
            std::memcpy(&word, text.data() + at, sizeof(word));
            if((word & 0x8080'8080'8080'8080) == 0) {
                at += sizeof(word);
                continue;
            }
        }
        auto step = detail::utf8_step(text.substr(at));
        if(!step.valid) {
            return false;
        }
        at += step.length;
    }
    return true;
}

/// text as a UTF-8 decoder reads it: each maximal subpart of an ill-formed
/// sequence replaced with U+FFFD.
inline std::string replace_invalid_utf8(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for(std::size_t at = 0; at < text.size();) {
        auto step = detail::utf8_step(text.substr(at));
        if(step.valid) {
            result.append(text.substr(at, step.length));
        } else {
            result.append("\xEF\xBF\xBD");
        }
        at += step.length;
    }
    return result;
}

/// What a text backend reports for a string char_from_utf8 rejects.
constexpr inline std::string_view invalid_char_message = "expected a single character up to U+00FF";

struct RawValue {
    std::string data;

    bool empty() const noexcept {
        return data.empty();
    }
};

}  // namespace kota::codec
