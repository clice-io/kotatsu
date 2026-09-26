#pragma once

#include <cstddef>
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

/// What a text backend reports for a string char_from_utf8 rejects.
constexpr inline std::string_view invalid_char_message = "expected a single character up to U+00FF";

struct RawValue {
    std::string data;

    bool empty() const noexcept {
        return data.empty();
    }
};

}  // namespace kota::codec
