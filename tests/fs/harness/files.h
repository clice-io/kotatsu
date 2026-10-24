#pragma once

// TempDir, a directory of a test's own, and write_text() and read_text(),
// all through std::filesystem and fstreams rather than kota::fs, which the
// tests check.

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

#include "kota/zest/zest.h"

namespace kota::test {

/// A directory of its own under the system temp directory, removed with
/// everything in it when this goes, or when a failed ZASSERT ends the process.
struct TempDir {
    TempDir() : path(create()) {}

    ~TempDir() {
        remove();
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /// The directory, as the UTF-8 string kotatsu takes.
    std::string root() const {
        auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    /// `name`, UTF-8, inside the directory, as the UTF-8 string kotatsu takes.
    std::string file(std::string_view name) const {
        auto u8 = (path / std::u8string(name.begin(), name.end())).u8string();
        return std::string(u8.begin(), u8.end());
    }

    const std::filesystem::path path;

private:
    void remove() const {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    zest::FatalHook on_fatal{[this] { remove(); }};

    static std::filesystem::path create() {
        std::random_device random;
        auto base = std::filesystem::temp_directory_path();
        std::filesystem::path candidate;
        do {
            candidate = base / std::format("kota-{:08x}{:08x}", random(), random());
        } while(!std::filesystem::create_directory(candidate));
        return candidate;
    }
};

/// The UTF-8 `path` as std::filesystem takes it.
inline std::filesystem::path std_path(std::string_view path) {
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

/// Writes `text` as the whole of the file at the UTF-8 `path`.
inline void write_text(std::string_view path, std::string_view text) {
    std::ofstream(std_path(path), std::ios::binary) << text;
}

/// The whole of the file at the UTF-8 `path`.
inline std::string read_text(std::string_view path) {
    std::ifstream in(std_path(path), std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

}  // namespace kota::test
