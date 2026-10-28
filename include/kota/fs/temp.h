#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <system_error>

#include "kota/fs/file.h"

namespace kota::fs {

/// A directory of a caller's own, made as create_temp_directory() makes one,
/// its owner's alone (0700) on POSIX, and removed with everything in it when
/// this goes, unless kept.
class TempDirectory {
public:
    /// Creates a directory in `directory`, named `prefix` and random
    /// characters; refused as create_temp_directory() refuses it. Where the
    /// OS cannot tell the canonical() name of the directory made, it removes
    /// it and fails with that error.
    static std::expected<TempDirectory, std::error_code> create(std::string_view directory,
                                                                std::string_view prefix);

    TempDirectory(TempDirectory&& other) noexcept;

    /// Removes the directory this held, as the destructor does, and takes
    /// `other`'s.
    TempDirectory& operator=(TempDirectory&& other) noexcept;

    /// Removes the directory as remove() does, its error left untold.
    ~TempDirectory();

    /// The directory, as canonical() names it: absolute, through the
    /// symlinks above it (macOS's /var, which is /private/var) and on Windows
    /// with each name in its long form rather than an 8.3 one (RUNNER~1), so
    /// that it is how the OS names what lies in it. Empty once removed or
    /// kept.
    std::string_view path() const {
        return location;
    }

    /// Removes the directory and everything in it, as remove_all() does,
    /// now rather than when this goes. Once removed or kept, it does nothing.
    std::error_code remove();

    /// Gives the directory up to the caller, to stay when this goes: its
    /// path.
    std::string keep() &&;

private:
    explicit TempDirectory(std::string location) : location(std::move(location)) {}

    std::string location;
};

/// A file of a caller's own, made and opened as create_temp_file() makes
/// one, its owner's alone (0600) on POSIX, and removed when this goes, unless
/// kept.
class TempFile {
public:
    /// Creates an empty file in `directory`, named `prefix`, random
    /// characters and `suffix`; refused as create_temp_file() refuses it, and
    /// removed again where the OS cannot tell its canonical() name, as
    /// TempDirectory::create() does.
    static std::expected<TempFile, std::error_code> create(std::string_view directory,
                                                           std::string_view prefix,
                                                           std::string_view suffix = {});

    TempFile(TempFile&& other) noexcept;

    /// Removes the file this held, as the destructor does, and takes
    /// `other`'s.
    TempFile& operator=(TempFile&& other) noexcept;

    /// Removes the file as remove() does, its error left untold.
    ~TempFile();

    /// The file, as TempDirectory::path() names a directory. Empty once
    /// removed or kept.
    std::string_view path() const {
        return location;
    }

    /// The file, open for reading and writing.
    File& file() {
        return opened;
    }

    /// Closes the file and removes it, now rather than when this goes. Once
    /// removed or kept, it does nothing.
    std::error_code remove();

    /// Gives the file up to the caller, to stay when this goes: its path.
    /// The file stays open until this goes.
    std::string keep() &&;

private:
    TempFile(std::string location, File opened) :
        location(std::move(location)), opened(std::move(opened)) {}

    std::string location;

    File opened;
};

}  // namespace kota::fs
