#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace kota::fs {

/// How File::open() opens a file.
enum class OpenMode : std::uint8_t {
    /// For reading; the file must exist.
    Read,
    /// For writing, created or emptied.
    Write,
    /// For writing a file that must not exist: anything there fails with
    /// std::errc::file_exists, a symlink too, dangling or not.
    CreateNew,
};

/// An open file, closed when this goes. Its handle is not inherited by
/// child processes, and on Windows does not keep another process from
/// renaming or removing the file.
class File {
public:
#ifdef _WIN32
    using native_handle_type = void*;
#else
    using native_handle_type = int;
#endif

    /// What a closed File holds: CreateFileW never gives a null handle.
#ifdef _WIN32
    static constexpr native_handle_type closed = nullptr;
#else
    static constexpr native_handle_type closed = -1;
#endif

    File() = default;

    File(File&& other) noexcept;

    File& operator=(File&& other) noexcept;

    ~File();

    static std::expected<File, std::error_code> open(std::string_view path, OpenMode mode);

    /// Reads what fits into `buffer` from where the last read stopped: how
    /// many bytes it read, zero at the end of the file.
    std::expected<std::size_t, std::error_code> read(std::span<char> buffer);

    /// Writes all of `bytes`.
    std::error_code write(std::string_view bytes);

    /// Closes the file, saying whether the system kept every write, which
    /// some file systems tell only now; closing a closed file does nothing.
    std::error_code close();

    bool is_open() const;

    /// The file descriptor, or on Windows the HANDLE; this keeps it.
    native_handle_type native_handle() const {
        return handle;
    }

private:
    explicit File(native_handle_type handle) : handle(handle) {}

    native_handle_type handle = closed;
};

/// The bytes of a file. Read, not mapped: on Windows a mapped file keeps an
/// editor from saving over it.
std::expected<std::string, std::error_code> read_file(std::string_view path);

/// Writes `bytes` as the whole of a file, created or emptied first.
std::error_code write_file(std::string_view path, std::string_view bytes);

/// Writes `bytes` as the whole of a file through a sibling renamed over it,
/// so a concurrent reader sees the old bytes or the new, never a part, and
/// concurrent writers never mix. A symlink at `path` is replaced, not
/// written through. It does not wait for the bytes to reach the disk: after
/// a crash of the system, `path` may hold the old bytes, the new, or none.
std::error_code write_file_atomic(std::string_view path, std::string_view bytes);

/// Creates an empty file in `directory`, named `prefix`, random characters
/// and `suffix`, and none of it there before: its path.
std::expected<std::string, std::error_code> create_temp_file(std::string_view directory,
                                                             std::string_view prefix,
                                                             std::string_view suffix);

}  // namespace kota::fs
