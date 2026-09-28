#include "kota/ipc/lsp/position.h"

#include <algorithm>
#include <cassert>

namespace kota::ipc::lsp {

/// A bit for each line of `content`, split at '\n' as build_line_starts
/// splits it, set when the line holds a byte past ASCII; the words end at the
/// last line with one.
static std::vector<std::uint64_t> find_non_ascii_lines(std::string_view content) {
    std::vector<std::uint64_t> bits;
    std::size_t line = 0;
    std::size_t at = 0;
    while(true) {
        auto end = content.find('\n', at);
        unsigned char seen = 0;
        for(unsigned char byte: content.substr(at, end - at)) {
            seen |= byte;
        }
        if(seen >= 0x80) {
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

LineMap::LineMap(std::string_view content, PositionEncoding encoding) :
    source(content), starts(build_line_starts(content)),
    non_ascii_lines(find_non_ascii_lines(content)), enc(encoding) {
    assert(encoding != PositionEncoding::Default &&
           "Default is not valid for LineMap construction");
}

LineMap::LineMap(std::string_view content,
                 std::span<const std::uint32_t> line_starts,
                 PositionEncoding encoding) :
    source(content), starts(line_starts), non_ascii_lines(find_non_ascii_lines(content)),
    enc(encoding) {
    assert(encoding != PositionEncoding::Default &&
           "Default is not valid for LineMap construction");
}

LineMap::LineMap(std::string_view content,
                 std::vector<std::uint32_t>&& line_starts,
                 PositionEncoding encoding) :
    source(content), starts(std::move(line_starts)), non_ascii_lines(find_non_ascii_lines(content)),
    enc(encoding) {
    assert(encoding != PositionEncoding::Default &&
           "Default is not valid for LineMap construction");
}

PositionEncoding LineMap::resolve(PositionEncoding encoding) const {
    return encoding == PositionEncoding::Default ? enc : encoding;
}

std::uint32_t LineMap::line_end(std::uint32_t line) const {
    auto ls = line_starts();
    if(line + 1 >= ls.size()) {
        return static_cast<std::uint32_t>(source.size());
    }
    auto end = ls[line + 1] - 1;
    if(end > ls[line] && source[end - 1] == '\r') {
        --end;
    }
    return end;
}

bool LineMap::is_ascii(std::uint32_t line) const {
    return line / 64 >= non_ascii_lines.size() ||
           ((non_ascii_lines[line / 64] >> (line % 64)) & 1) == 0;
}

std::optional<protocol::Position> LineMap::to_position(std::uint32_t offset,
                                                       PositionEncoding encoding) const {
    if(offset > source.size()) [[unlikely]] {
        return std::nullopt;
    }
    auto actual = resolve(encoding);
    auto bounds = line_bounds(offset);
    auto column = std::min(offset, bounds.end) - bounds.start;
    if(actual != PositionEncoding::UTF8 && !is_ascii(bounds.line)) {
        column = encoded_length(source.substr(bounds.start, column), actual);
    }
    return protocol::Position{.line = bounds.line, .character = column};
}

std::optional<std::uint32_t> LineMap::to_offset(protocol::Position position,
                                                PositionEncoding encoding) const {
    auto actual = resolve(encoding);
    auto line = position.line;
    if(line >= line_starts().size()) [[unlikely]] {
        return std::nullopt;
    }

    auto begin = line_starts()[line];
    auto end = line_end(line);
    if(actual == PositionEncoding::UTF8 || is_ascii(line)) {
        return begin + std::min(position.character, end - begin);
    }

    auto text = source.substr(begin, end - begin);
    if(auto offset = encoded_offset(text, position.character, actual)) {
        return begin + *offset;
    }
    if(position.character >= encoded_length(text, actual)) {
        return end;
    }
    return std::nullopt;
}

std::optional<protocol::Range> LineMap::to_range(std::uint32_t begin,
                                                 std::uint32_t end,
                                                 PositionEncoding encoding) const {
    if(begin > end) [[unlikely]] {
        return std::nullopt;
    }
    auto start = to_position(begin, encoding);
    if(!start) {
        return std::nullopt;
    }
    auto stop = to_position(end, encoding);
    if(!stop) {
        return std::nullopt;
    }
    return protocol::Range{.start = *start, .end = *stop};
}

LineMap::LineBounds LineMap::line_bounds(std::uint32_t offset) const {
    auto ls = line_starts();
    assert(!ls.empty() && ls.front() == 0 && "line starts begin with the first line's, 0");
    auto line = static_cast<std::uint32_t>(std::ranges::upper_bound(ls, offset) - ls.begin() - 1);
    return {line, ls[line], line_end(line)};
}

std::string_view LineMap::content() const {
    return source;
}

std::span<const std::uint32_t> LineMap::line_starts() const {
    return std::visit([](const auto& v) -> std::span<const std::uint32_t> { return v; }, starts);
}

}  // namespace kota::ipc::lsp
