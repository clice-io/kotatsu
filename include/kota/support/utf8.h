#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace kota {

/// The bytes a UTF-8 decoder takes from the start of a text: their length,
/// whether they make a code point, and the code point they make, U+FFFD when
/// they do not. An ill-formed sequence is its maximal subpart, the longest
/// prefix of a code point it starts with and at least one byte, which a
/// decoder replaces with one U+FFFD.
struct Utf8Sequence {
    char32_t code_point;
    std::size_t length;
    bool valid;
};

/// The sequence at the start of text, which is not empty. A code point is
/// none overlong, a surrogate or past U+10FFFF.
constexpr Utf8Sequence decode_utf8(std::string_view text) {
    constexpr char32_t replacement = 0xFFFD;
    auto lead = static_cast<unsigned char>(text[0]);
    if(lead < 0x80) {
        return {.code_point = lead, .length = 1, .valid = true};
    }
    // The range of the second byte narrows for the leads whose code points
    // would be overlong, surrogates or past U+10FFFF.
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
        high = lead == 0xED ? 0x9F : high;
    } else if(lead >= 0xF0 && lead <= 0xF4) {
        need = 4;
        code_point = lead & 0x07;
        low = lead == 0xF0 ? 0x90 : low;
        high = lead == 0xF4 ? 0x8F : high;
    } else {
        return {.code_point = replacement, .length = 1, .valid = false};
    }
    std::size_t length = 1;
    while(length < need && length < text.size()) {
        auto next = static_cast<unsigned char>(text[length]);
        if(next < low || next > high) {
            break;
        }
        code_point = (code_point << 6) | (next & 0x3F);
        low = 0x80;
        high = 0xBF;
        ++length;
    }
    if(length != need) {
        return {.code_point = replacement, .length = length, .valid = false};
    }
    return {.code_point = code_point, .length = length, .valid = true};
}

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
        auto sequence = decode_utf8(text.substr(at));
        if(!sequence.valid) {
            return false;
        }
        at += sequence.length;
    }
    return true;
}

/// text as a UTF-8 decoder reads it: each maximal subpart of an ill-formed
/// sequence replaced with U+FFFD.
inline std::string replace_invalid_utf8(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for(std::size_t at = 0; at < text.size();) {
        auto sequence = decode_utf8(text.substr(at));
        if(sequence.valid) {
            result.append(text.substr(at, sequence.length));
        } else {
            result.append("\xEF\xBF\xBD");
        }
        at += sequence.length;
    }
    return result;
}

}  // namespace kota
