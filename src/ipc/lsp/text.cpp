#include "kota/ipc/lsp/text.h"

#include <cassert>
#include <cstddef>
#include <utility>

#include "locate.h"

namespace {

/// Whether `byte` continues a UTF-8 sequence, being 10xxxxxx.
constexpr bool is_continuation(unsigned char byte) {
    return (byte & 0xC0u) == 0x80u;
}

// Decodes one UTF-8 code point starting at `index`.
// Returns:
// - first: consumed UTF-8 byte count
// - second: corresponding UTF-16 code unit count
// For invalid/truncated sequences it falls back to {1, 1} so callers can
// keep scanning forward without getting stuck.
std::pair<std::uint32_t, std::uint32_t> next_codepoint_sizes(std::string_view text,
                                                             std::size_t index) {
    assert(index < text.size() && "index out of range");

    // First byte >= ascii_limit starts a multi-byte UTF-8 sequence.
    constexpr unsigned char ascii_limit = 0x80u;

    // Minimum valid 2-byte lead is C2 (C0/C1 are overlong).
    constexpr unsigned char two_byte_min = 0xC2u;

    // Lead-byte region boundaries.
    constexpr unsigned char three_byte_min = 0xE0u;
    constexpr unsigned char four_byte_min = 0xF0u;
    constexpr unsigned char four_byte_exclusive_max = 0xF5u;

    // E0 xx needs b2 >= A0 to avoid overlong 3-byte encoding.
    constexpr unsigned char three_byte_overlong_lead = 0xE0u;
    constexpr unsigned char three_byte_overlong_b2_min = 0xA0u;

    // ED A0..BF would encode UTF-16 surrogate range.
    constexpr unsigned char surrogate_lead = 0xEDu;
    constexpr unsigned char surrogate_b2_min = 0xA0u;

    // F0 xx needs b2 >= 90 to avoid overlong 4-byte encoding.
    constexpr unsigned char four_byte_overlong_lead = 0xF0u;
    constexpr unsigned char four_byte_overlong_b2_min = 0x90u;

    // F4 b2 must stay below 90 to remain <= U+10FFFF.
    constexpr unsigned char unicode_max_lead = 0xF4u;
    constexpr unsigned char unicode_max_b2_exclusive = 0x90u;

    // Inspect the leading byte to determine UTF-8 sequence width.
    const auto lead = static_cast<unsigned char>(text[index]);

    // 0xxxxxxx: ASCII, one UTF-8 byte and one UTF-16 code unit.
    if(lead < ascii_limit) [[likely]] {
        return {1, 1};
    }

    // Invalid leading byte:
    // - 10xxxxxx: continuation byte
    // - 0xC0/0xC1: overlong 2-byte lead
    if(lead < two_byte_min) [[unlikely]] {
        return {1, 1};
    }

    if(lead < three_byte_min) {
        // 2-byte UTF-8 lead range: C2..DF
        if(index + 2 > text.size()) [[unlikely]] {
            return {1, 1};
        }

        const auto b2 = static_cast<unsigned char>(text[index + 1]);
        if(!is_continuation(b2)) [[unlikely]] {
            return {1, 1};
        }

        return {2, 1};
    }

    if(lead < four_byte_min) {
        // 3-byte UTF-8 lead range: E0..EF
        if(index + 3 > text.size()) [[unlikely]] {
            return {1, 1};
        }

        const auto b2 = static_cast<unsigned char>(text[index + 1]);
        const auto b3 = static_cast<unsigned char>(text[index + 2]);
        if(!is_continuation(b2) || !is_continuation(b3)) [[unlikely]] {
            return {1, 1};
        }

        if(lead == three_byte_overlong_lead && b2 < three_byte_overlong_b2_min) [[unlikely]] {
            return {1, 1};
        }

        if(lead == surrogate_lead && b2 >= surrogate_b2_min) [[unlikely]] {
            return {1, 1};
        }

        return {3, 1};
    }

    if(lead < four_byte_exclusive_max) {
        // 4-byte UTF-8 lead range: F0..F4 (Unicode max U+10FFFF).
        if(index + 4 > text.size()) [[unlikely]] {
            return {1, 1};
        }

        const auto b2 = static_cast<unsigned char>(text[index + 1]);
        const auto b3 = static_cast<unsigned char>(text[index + 2]);
        const auto b4 = static_cast<unsigned char>(text[index + 3]);
        if(!is_continuation(b2) || !is_continuation(b3) || !is_continuation(b4)) [[unlikely]] {
            return {1, 1};
        }

        if(lead == four_byte_overlong_lead && b2 < four_byte_overlong_b2_min) [[unlikely]] {
            return {1, 1};
        }

        if(lead == unicode_max_lead && b2 >= unicode_max_b2_exclusive) [[unlikely]] {
            return {1, 1};
        }

        return {4, 2};
    }

    // F5..FF are invalid in UTF-8.
    return {1, 1};
}

}  // namespace

namespace kota::ipc::lsp {

detail::Located detail::locate(std::string_view text,
                               std::uint32_t character,
                               PositionEncoding encoding) {
    const auto size = static_cast<std::uint32_t>(text.size());
    if(encoding == PositionEncoding::UTF8) {
        if(character >= size) {
            return {.offset = size, .exact = character == size};
        }
        // UTF-8 resynchronizes: a byte that is no continuation byte starts a
        // code point, and a continuation byte lies inside the sequence of the
        // nearest such byte before it, at most three back, if that sequence
        // reaches it; else it is a code point of its own.
        auto continues = [&](std::uint32_t at) {
            return is_continuation(static_cast<unsigned char>(text[at]));
        };
        auto lead = character;
        while(continues(lead) && lead > 0 && character - lead < 3) {
            --lead;
        }
        if(lead != character && !continues(lead) &&
           lead + next_codepoint_sizes(text, lead).first > character) {
            return {.offset = lead, .exact = false};
        }
        return {.offset = character, .exact = true};
    }

    std::uint32_t units = 0;
    for(std::uint32_t i = 0; i < size;) {
        if(units == character) {
            return {.offset = i, .exact = true};
        }
        auto [utf8, utf16] = next_codepoint_sizes(text, i);
        units += encoding == PositionEncoding::UTF16 ? utf16 : 1;
        if(units > character) {
            return {.offset = i, .exact = false};
        }
        i += utf8;
    }
    return {.offset = size, .exact = units == character};
}

std::vector<std::uint32_t> build_line_starts(std::string_view content) {
    std::vector<std::uint32_t> starts;
    starts.push_back(0);
    for(std::uint32_t i = 0; i < content.size(); ++i) {
        if(content[i] == '\n') {
            starts.push_back(i + 1);
        }
    }
    return starts;
}

std::uint32_t encoded_length(std::string_view text, PositionEncoding encoding) {
    if(encoding == PositionEncoding::UTF8) {
        return static_cast<std::uint32_t>(text.size());
    }

    std::uint32_t units = 0;
    for(std::size_t i = 0; i < text.size();) {
        auto [utf8, utf16] = next_codepoint_sizes(text, i);
        i += utf8;
        units += (encoding == PositionEncoding::UTF16) ? utf16 : 1;
    }
    return units;
}

std::optional<std::uint32_t> encoded_offset(std::string_view text,
                                            std::uint32_t character,
                                            PositionEncoding encoding) {
    auto found = detail::locate(text, character, encoding);
    if(!found.exact) {
        return std::nullopt;
    }
    return found.offset;
}

}  // namespace kota::ipc::lsp
