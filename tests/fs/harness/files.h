#pragma once

// TempDir, a directory of a test's own, write_text() and read_text(),
// ScopedCurrentDir, make_junction() on Windows and ScopedPermissions
// elsewhere, all through std::filesystem, fstreams and cmd rather than
// kota::fs, which the tests check; linked() for a case that makes a symlink;
// text_of() and CountedBuffer for reads of large files.

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "kota/zest/zest.h"

namespace kota::test {

/// A directory of its own under the system temp directory, removed with
/// everything in it when this goes, or when a failed ZASSERT ends the process.
struct TempDir {
    TempDir() : path(create(std::filesystem::temp_directory_path())) {}

    /// One under `base` rather than the system temp directory.
    explicit TempDir(const std::filesystem::path& base) : path(create(base)) {}

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
#ifdef _WIN32
        // A case may leave paths longer than MAX_PATH.
        std::filesystem::remove_all(LR"(\\?\)" + path.wstring(), ignored);
#else
        std::filesystem::remove_all(path, ignored);
#endif
    }

    zest::FatalHook on_fatal{[this] { remove(); }};

    static std::filesystem::path create(const std::filesystem::path& base) {
        std::random_device random;
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

/// Text of `size` bytes that is no repetition of a short run.
inline std::string text_of(std::size_t size) {
    std::string text(size, '\0');
    for(std::size_t i = 0; i < size; ++i) {
        text[i] = static_cast<char>('a' + (i * 7 + i / 26) % 26);
    }
    return text;
}

/// A string that counts how often it is resized: what tells a read sized
/// once from one that grows its buffer step by step.
struct CountedBuffer {
    char* data() {
        return text.data();
    }

    std::size_t size() const {
        return text.size();
    }

    void resize(std::size_t size) {
        ++resizes;
        text.resize(size);
    }

    std::string text;
    int resizes = 0;
};

/// Writes `text` as the whole of the file at the UTF-8 `path`.
inline void write_text(std::string_view path, std::string_view text) {
    std::ofstream(std_path(path), std::ios::binary) << text;
}

/// The whole of the file at the UTF-8 `path`.
inline std::string read_text(std::string_view path) {
    std::ifstream in(std_path(path), std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

/// Whether a symlink was made, `error` being what making it returned.
/// Windows lets only an administrator or a developer-mode system make one:
/// a case refused that privilege is skipped.
inline bool linked(std::error_code error) {
    if(!error) {
        return true;
    }
    ZASSERT(error == std::errc::operation_not_permitted);
    zest::skip();
    return false;
}

/// The current directory, put back when this goes.
struct ScopedCurrentDir {
    explicit ScopedCurrentDir(const std::filesystem::path& path) {
        std::filesystem::current_path(path);
    }

    ScopedCurrentDir(const ScopedCurrentDir&) = delete;
    ScopedCurrentDir& operator=(const ScopedCurrentDir&) = delete;

    ~ScopedCurrentDir() {
        restore();
    }

    std::filesystem::path original = std::filesystem::current_path();

private:
    void restore() const {
        std::error_code ignored;
        std::filesystem::current_path(original, ignored);
    }

    // Windows removes no directory a process stands in: this runs before the
    // hook of a TempDir it stands in, declared before it.
    zest::FatalHook on_fatal{[this] { restore(); }};
};

#ifndef _WIN32
/// The permissions of `path` replaced by `perms` for as long as this lives,
/// or until a failed ZASSERT ends the process: then the owner's again, so
/// that TempDir can remove what is inside.
struct ScopedPermissions {
    ScopedPermissions(std::filesystem::path path, std::filesystem::perms perms) :
        path(std::move(path)) {
        std::filesystem::permissions(this->path, perms);
    }

    ~ScopedPermissions() {
        restore();
    }

    ScopedPermissions(const ScopedPermissions&) = delete;
    ScopedPermissions& operator=(const ScopedPermissions&) = delete;

    const std::filesystem::path path;

private:
    void restore() const {
        std::error_code ignored;
        std::filesystem::permissions(path, std::filesystem::perms::owner_all, ignored);
    }

    zest::FatalHook on_fatal{[this] { restore(); }};
};
#endif

#ifdef _WIN32
/// Makes `link` a junction to the directory `target`, as a link that needs
/// no privilege.
inline void make_junction(std::string_view link, std::string_view target) {
    auto command = L"mklink /J \"" + std_path(link).wstring() + L"\" \"" +
                   std_path(target).wstring() + L"\" > nul";
    ZASSERT(::_wsystem(command.c_str()) == 0);
}
#endif

}  // namespace kota::test
