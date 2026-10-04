#include "kota/ipc/lsp/position.h"

namespace kota::ipc::lsp {

protocol::Position detail::position_in(std::string_view content,
                                       Line line,
                                       std::uint32_t offset,
                                       PositionEncoding encoding) {
    auto column = std::min(offset, line.end) - line.start;
    if(!line.ascii) {
        auto text = content.substr(line.start, line.end - line.start);
        // Counted to the start of the code point the offset is in.
        auto start = locate(text, column, PositionEncoding::UTF8).offset;
        column = encoded_length(text.substr(0, start), encoding);
    }
    return {.line = line.number, .character = column};
}

detail::Located detail::offset_in(std::string_view content,
                                  Line line,
                                  std::uint32_t character,
                                  PositionEncoding encoding) {
    if(line.ascii) {
        return {.offset = line.start + std::min(character, line.end - line.start), .exact = true};
    }
    auto found = locate(content.substr(line.start, line.end - line.start), character, encoding);
    // A character past the line's end is the end, as LSP asks.
    auto offset = line.start + found.offset;
    return {.offset = offset, .exact = found.exact || offset == line.end};
}

}  // namespace kota::ipc::lsp
