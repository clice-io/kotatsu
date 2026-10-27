#pragma once

// What a File and a Directory ask alike of the handle they hold.

#include <expected>
#include <system_error>

#include "kota/support/buffer_ref.h"
#include "kota/fs/file.h"
#include "kota/fs/status.h"

namespace kota::fs::detail {

/// The status of what `handle` has open, as File::status() tells it.
std::expected<Status, std::error_code> handle_status(File::native_handle_type handle);

/// Writes into `out` the name the OS gives what `handle` has open, as
/// File::canonical() tells it.
std::error_code handle_canonical(buffer_ref out, File::native_handle_type handle);

}  // namespace kota::fs::detail
