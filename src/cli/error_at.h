#pragma once

#include <cstdint>
#include <string>

#include "kotatsu/cli/error.h"
#include "kotatsu/support/string_list_ref.h"

namespace kotatsu::cli::detail {

/// The error of `kind` at elements [begin, end) of `argv`, which it copies.
Error error_at(Error::Kind kind,
               std::string message,
               string_list_ref argv,
               std::uint32_t begin,
               std::uint32_t end);

}  // namespace kotatsu::cli::detail
