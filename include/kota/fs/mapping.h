#pragma once

#include <cstddef>
#include <expected>
#include <string_view>
#include <system_error>

#include "kota/fs/file.h"

namespace kota::fs {

/// The bytes of a file mapped into memory, read only, unmapped when this
/// goes. The system reads them in as they are touched, and shares them with
/// every other mapping of the file.
///
/// The mapping shows what others write to the file while it lives, and
/// touching a page a truncation took from under it ends the process
/// (SIGBUS) on POSIX; on Windows the file cannot be truncated, nor an editor
/// save over it in place, while the mapping lives.
class Mapping {
public:
    Mapping() = default;

    Mapping(Mapping&& other) noexcept;

    Mapping& operator=(Mapping&& other) noexcept;

    ~Mapping();

    /// Maps the whole of `file`, open for reading, at the size it has now;
    /// the mapping outlives the File. An empty file, or one that reports no
    /// size, such as those in /proc, maps to no bytes.
    static std::expected<Mapping, std::error_code> map(const File& file);

    std::string_view bytes() const {
        return {data, size};
    }

private:
    Mapping(const char* data, std::size_t size) : data(data), size(size) {}

    const char* data = nullptr;

    std::size_t size = 0;
};

}  // namespace kota::fs
