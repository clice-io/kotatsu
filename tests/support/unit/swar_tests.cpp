#include <cstddef>
#include <cstdint>
#include <string>

#include "kota/zest/zest.h"
#include "kota/support/swar.h"

namespace kota {

namespace {

constexpr std::uint64_t ones = 0x0101'0101'0101'0101;

/// Eight bytes of rest, but the one at `at`, which is `byte`.
constexpr std::uint64_t word_with(std::size_t at, unsigned byte, unsigned rest = 'a') {
    const auto shift = 8 * at;
    return (ones * rest & ~(std::uint64_t{0xFF} << shift)) | std::uint64_t{byte} << shift;
}

ZEST_SUITE(support_swar) {

ZEST_CASE(has_byte_finds_its_byte_anywhere_in_the_word) {
    for(std::size_t at = 0; at < 8; ++at) {
        ZEST_CONTEXT("byte {}", at);
        ZEXPECT(has_byte_below(word_with(at, 0x1F), 0x20));
        ZEXPECT(has_byte_below(word_with(at, 0x00), 0x20));
        ZEXPECT(has_byte(word_with(at, '"'), '"'));
        ZEXPECT(has_non_ascii(word_with(at, 0x80)));
    }
}

ZEST_CASE(has_byte_passes_over_the_bytes_beside_its_bound) {
    for(std::size_t at = 0; at < 8; ++at) {
        ZEST_CONTEXT("byte {}", at);
        // The bytes from 0x80, which lie below the bound once a borrow
        // wraps them, do not count.
        for(unsigned byte: {0x20, 0x21, 0x7F, 0x80, 0xFF}) {
            ZEST_CONTEXT("value {}", byte);
            ZEXPECT(!has_byte_below(word_with(at, byte), 0x20));
        }
        ZEXPECT(!has_byte(word_with(at, '"' - 1), '"'));
        ZEXPECT(!has_byte(word_with(at, '"' + 1), '"'));
        ZEXPECT(!has_byte(word_with(at, '"' | 0x80), '"'));
        ZEXPECT(!has_non_ascii(word_with(at, 0x7F)));
    }
}

ZEST_CASE(first_byte_is_the_first_match_whatever_follows) {
    for(std::size_t at = 0; at < 8; ++at) {
        ZEST_CONTEXT("byte {}", at);
        // The bytes after the match, one above it, show through its borrow.
        ZEXPECT(first_byte(bytes_below(word_with(at, 0x1F, 0x20), 0x20)) == at);
        ZEXPECT(first_byte(bytes_equal(word_with(at, '"', '"' + 1), '"')) == at);
        ZEXPECT(first_byte(bytes_equal(word_with(at, '"', '"'), '"')) == 0U);
    }
}

ZEST_CASE(any_word_reads_every_byte_of_a_text_of_any_size) {
    for(std::size_t size = 0; size <= 20; ++size) {
        ZEST_CONTEXT("size {}", size);
        const std::string text(size, 'a');
        ZEXPECT(!any_word(text, 0, has_non_ascii));
        for(std::size_t at = 0; at < size; ++at) {
            ZEST_CONTEXT("byte {}", at);
            auto marked = text;
            marked[at] = '\x80';
            ZEXPECT(any_word(marked, 0, has_non_ascii));
        }
    }
}

ZEST_CASE(any_word_fills_a_short_text_with_the_filler) {
    // Zero bytes would be what the test looks for; the filler's are not.
    auto has_zero = [](std::uint64_t word) {
        return has_byte(word, '\0');
    };
    for(std::size_t size = 1; size < 4; ++size) {
        ZEST_CONTEXT("size {}", size);
        ZEXPECT(!any_word(std::string(size, 'a'), ones * 'a', has_zero));
    }
}

};  // ZEST_SUITE(support_swar)

}  // namespace

}  // namespace kota
