#pragma once

#include <cstdint>
#include <string_view>

#include "kota/ipc/lsp/text.h"

namespace kota::ipc::lsp::detail {

/// Where a unit falls: the offset a lenient reading gives it, and whether a
/// strict reading gives it that offset too.
struct Located {
    std::uint32_t offset;
    bool exact;
};

/// Where unit `character` of `text`, in `encoding`'s units, falls: at the
/// start of the code point it begins, exact, or lies inside, not exact; at
/// the text's end past that, exact only for the end itself.
Located locate(std::string_view text, std::uint32_t character, PositionEncoding encoding);

}  // namespace kota::ipc::lsp::detail
