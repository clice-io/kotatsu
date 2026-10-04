#pragma once

#include <cstdint>
#include <string_view>

#include "kota/ipc/lsp/text.h"

namespace kota::ipc::lsp::detail {

/// Where unit `character` of `text`, counted in `encoding`'s units, falls:
/// at the start of the code point it begins, exact, or lies inside, not
/// exact; at the text's end past that, exact only for the end itself.
Located locate(std::string_view text, std::uint32_t character, PositionEncoding encoding);

}  // namespace kota::ipc::lsp::detail
