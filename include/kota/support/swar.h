#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace kota {

namespace detail {

/// A word of eight bytes of 1, which times a byte is eight of that byte.
constexpr std::uint64_t byte_ones = 0x0101'0101'0101'0101;

}  // namespace detail

/// The top bit of each byte of word below n, which is at most 0x80. Up to
/// the first such byte, the lowest bit set, it is exact; past it, a byte not
/// below n may show too.
constexpr std::uint64_t bytes_below(std::uint64_t word, std::uint8_t n) {
    using detail::byte_ones;
    return (word - byte_ones * n) & ~word & byte_ones * 0x80;
}

/// The top bit of each byte of word that is c, as bytes_below shows them.
constexpr std::uint64_t bytes_equal(std::uint64_t word, char c) {
    return bytes_below(word ^ detail::byte_ones * static_cast<unsigned char>(c), 1);
}

/// Whether a byte of word is c.
constexpr bool has_byte(std::uint64_t word, char c) {
    return bytes_equal(word, c) != 0;
}

/// The eight bytes from `bytes` as a word whose lowest byte is the first, as
/// first_byte_index reads them.
inline std::uint64_t load_word(const char* bytes) {
    std::uint64_t word;
    std::memcpy(&word, bytes, sizeof(word));
    if constexpr(std::endian::native == std::endian::big) {
        word = std::byteswap(word);
    }
    return word;
}

/// The index of the first byte a nonzero mask of bytes_below or bytes_equal
/// shows in a word from load_word.
constexpr std::size_t first_byte_index(std::uint64_t mask) {
    return static_cast<std::size_t>(std::countr_zero(mask)) / 8;
}

/// Whether a byte of word is past ASCII.
constexpr bool has_non_ascii(std::uint64_t word) {
    return (word & detail::byte_ones * 0x80) != 0;
}

/// Whether test holds for some eight bytes of text, read as one
/// std::uint64_t: text runs eight bytes at a time, the last eight
/// overlapping those before them. A text shorter than that is read as one
/// word of its bytes, some of them twice, the rest of it filler's bytes.
/// test is byte-wise: it holds for a word when it holds for one of its
/// bytes, whatever the others, and never for filler's.
template <typename Test>
bool any_word(std::string_view text, std::uint64_t filler, Test test) {
    const char* bytes = text.data();
    const std::size_t size = text.size();
    if(size >= sizeof(std::uint64_t)) {
        std::uint64_t word;
        std::size_t at = 0;
        for(; size - at >= sizeof(word); at += sizeof(word)) {
            std::memcpy(&word, bytes + at, sizeof(word));
            if(test(word)) {
                return true;
            }
        }
        if(at == size) {
            return false;
        }
        std::memcpy(&word, bytes + size - sizeof(word), sizeof(word));
        return test(word);
    }
    if(size >= sizeof(std::uint32_t)) {
        std::uint32_t head;
        std::uint32_t tail;
        std::memcpy(&head, bytes, sizeof(head));
        std::memcpy(&tail, bytes + size - sizeof(tail), sizeof(tail));
        return test(head | std::uint64_t{tail} << 32);
    }
    if(size == 0) {
        return false;
    }
    auto byte = [&](std::size_t at) {
        return std::uint64_t{static_cast<unsigned char>(bytes[at])};
    };
    return test((filler & ~std::uint64_t{0xFF'FFFF}) | byte(0) | byte(size / 2) << 8 |
                byte(size - 1) << 16);
}

}  // namespace kota
