#pragma once

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

#include "kota/support/config.h"
#include "kota/support/swar.h"
#include "kota/codec/json/type.h"

namespace kota::codec::json {

namespace detail {

/// The letter after the backslash JSON writes for c in a string, when it
/// has one: a quote, a backslash, or a control character with a short form.
constexpr char short_escape(char c) {
    switch(c) {
        case '"': return '"';
        case '\\': return '\\';
        case '\b': return 'b';
        case '\f': return 'f';
        case '\n': return 'n';
        case '\r': return 'r';
        case '\t': return 't';
        default: return 0;
    }
}

constexpr bool is_control(char c) {
    return static_cast<unsigned char>(c) < 0x20;
}

/// The size of text escaped as the content of a JSON string.
constexpr std::size_t escaped_size(std::string_view text) {
    std::size_t size = 0;
    for(char c: text) {
        size += short_escape(c) != 0 ? 2 : is_control(c) ? 6 : 1;
    }
    return size;
}

/// Writes text at out escaped as the content of a JSON string, and returns
/// where it ends: a quote, a backslash and the control characters with a
/// short form after a backslash, the other control characters as \u00XX.
constexpr char* escape_to(std::string_view text, char* out) {
    constexpr std::string_view hex = "0123456789abcdef";
    for(char c: text) {
        if(char letter = short_escape(c)) {
            *out++ = '\\';
            *out++ = letter;
        } else if(is_control(c)) {
            for(char part: {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 0xF]}) {
                *out++ = part;
            }
        } else {
            *out++ = c;
        }
    }
    return out;
}

/// Whether a byte of word is one escape_to changes: a quote, a backslash or
/// a control character.
constexpr bool has_escaped_byte(std::uint64_t word) {
    return has_byte_below(word, 0x20) || has_byte(word, '"') || has_byte(word, '\\');
}

/// Whether escape_to changes text.
inline bool needs_escape(std::string_view text) {
    constexpr std::uint64_t letters = 0x6161'6161'6161'6161;
    return any_word(text, letters, [](std::uint64_t word) { return has_escaped_byte(word); });
}

/// Copies size bytes from source to out, without a call for a short run.
inline void copy_bytes(char* out, const char* source, std::size_t size) {
    if(size > 16) {
        std::memcpy(out, source, size);
    } else if(size >= 8) {
        std::uint64_t head;
        std::uint64_t tail;
        std::memcpy(&head, source, 8);
        std::memcpy(&tail, source + size - 8, 8);
        std::memcpy(out, &head, 8);
        std::memcpy(out + size - 8, &tail, 8);
    } else if(size >= 4) {
        std::uint32_t head;
        std::uint32_t tail;
        std::memcpy(&head, source, 4);
        std::memcpy(&tail, source + size - 4, 4);
        std::memcpy(out, &head, 4);
        std::memcpy(out + size - 4, &tail, 4);
    } else if(size > 0) {
        out[0] = source[0];
        out[size / 2] = source[size / 2];
        out[size - 1] = source[size - 1];
    }
}

}  // namespace detail

/// JSON text as it is written, in a buffer that grows fourfold: in a large
/// document, the pages each growth copies the text into are most of what
/// writing it costs.
class StringBuilder {
public:
    explicit StringBuilder(std::size_t initial_capacity) :
        buffer(new char[initial_capacity]), capacity(initial_capacity) {}

    void put(char c) {
        *room(1) = c;
        ++size;
    }

    void append_raw(std::string_view raw) {
        detail::copy_bytes(room(raw.size()), raw.data(), raw.size());
        size += raw.size();
    }

    /// N bytes from raw, a size known at compile time.
    template <std::size_t N>
    void append_raw(const char* raw) {
        std::memcpy(room(N), raw, N);
        size += N;
    }

    void append_null() {
        append_raw<4>("null");
    }

    void append_bool(bool value) {
        if(value) {
            append_raw<4>("true");
        } else {
            append_raw<5>("false");
        }
    }

    template <typename Integer>
        requires std::same_as<Integer, std::int64_t> || std::same_as<Integer, std::uint64_t>
    void append_number(Integer value) {
        auto* at = room(20);
        end_at(std::to_chars(at, at + 20, value).ptr);
    }

    /// A finite double, in a form that reads back as it, written as
    /// simdjson writes it: digits alone get a fraction, so that they read
    /// back as a double, and exponents start where the digits would run
    /// long.
    void append_number(double value) {
        constexpr std::size_t longest = 24;
        auto* at = room(longest);
        end_at(simdjson::internal::to_chars(at, nullptr, value));
    }

    /// value as a JSON string: quoted, escaped.
    void append_string(std::string_view value) {
        if(!detail::needs_escape(value)) [[likely]] {
            auto* at = room(value.size() + 2);
            *at++ = '"';
            detail::copy_bytes(at, value.data(), value.size());
            at += value.size();
            *at++ = '"';
            end_at(at);
            return;
        }
        auto* at = room(detail::escaped_size(value) + 2);
        *at++ = '"';
        at = detail::escape_to(value, at);
        *at++ = '"';
        end_at(at);
    }

    /// The text written.
    std::string take() && {
        return std::string(buffer.get(), size);
    }

private:
    /// Where up to n more bytes go.
    char* room(std::size_t n) {
        if(capacity - size < n) [[unlikely]] {
            grow(n);
        }
        return buffer.get() + size;
    }

    /// Ends the text at end, which a write into room() reached.
    void end_at(const char* end) {
        size = static_cast<std::size_t>(end - buffer.get());
    }

    KOTA_NOINLINE void grow(std::size_t n) {
        auto grown = std::max(capacity * 4, size + n);
        std::unique_ptr<char[]> bigger(new char[grown]);
        std::memcpy(bigger.get(), buffer.get(), size);
        buffer = std::move(bigger);
        capacity = grown;
    }

    /// The bytes past size are not written yet.
    std::unique_ptr<char[]> buffer;
    std::size_t size = 0;
    std::size_t capacity;
};

}  // namespace kota::codec::json
