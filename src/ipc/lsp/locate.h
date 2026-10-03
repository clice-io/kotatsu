#pragma once

#include <cstdint>
#include <string_view>

#include "kota/ipc/lsp/text.h"

namespace kota::ipc::lsp::detail {

/// Where a unit falls in a text: the start of the code point it begins or
/// lies inside, or the text's end for a unit past it; exact when it begins a
/// code point or is the end itself.
struct Located {
    std::uint32_t offset;
    bool exact;
};

/// Where unit `character` of `text`, counted in `encoding`'s units, falls.
Located locate(std::string_view text, std::uint32_t character, PositionEncoding encoding);

}  // namespace kota::ipc::lsp::detail
