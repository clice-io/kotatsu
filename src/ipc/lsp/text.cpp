#include "kota/ipc/lsp/text.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <utility>

#include "locate.h"
#include "kota/support/utf8.h"

namespace {

/// Whether `byte` continues a UTF-8 sequence, being 10xxxxxx.
constexpr bool is_continuation(unsigned char byte) {
    return (byte & 0xC0u) == 0x80u;
}

/// The bytes of the code point at `index` and the UTF-16 units it takes. A
/// byte that starts no code point counts as one of its own, one unit wide,
/// so a scan goes on from the next byte.
std::pair<std::uint32_t, std::uint32_t> next_codepoint_sizes(std::string_view text,
                                                             std::size_t index) {
    assert(index < text.size() && "index out of range");
    auto sequence = kota::decode_utf8(text.substr(index));
    if(!sequence.valid) [[unlikely]] {
        return {1, 1};
    }
    // A code point past the BMP takes a surrogate pair.
    return {static_cast<std::uint32_t>(sequence.length), sequence.code_point < 0x10000 ? 1U : 2U};
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

std::vector<std::uint32_t> line_starts(std::string_view content) {
    std::vector<std::uint32_t> starts;
    starts.push_back(0);
    for(std::uint32_t i = 0; i < content.size(); ++i) {
        if(content[i] == '\n') {
            starts.push_back(i + 1);
        }
    }
    return starts;
}

bool is_ascii(std::string_view text) {
    // One OR over every byte, which compilers vectorize, rather than a test
    // per byte.
    unsigned char seen = 0;
    for(unsigned char byte: text) {
        seen |= byte;
    }
    return seen < 0x80;
}

std::vector<std::uint64_t> non_ascii_lines(std::string_view content) {
    std::vector<std::uint64_t> bits;
    std::size_t line = 0;
    std::size_t at = 0;
    while(true) {
        auto end = content.find('\n', at);
        if(!is_ascii(content.substr(at, end - at))) {
            bits.resize(std::max(bits.size(), line / 64 + 1));
            bits[line / 64] |= std::uint64_t{1} << (line % 64);
        }
        if(end == std::string_view::npos) {
            return bits;
        }
        at = end + 1;
        ++line;
    }
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
