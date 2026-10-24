#include "kota/fs/file.h"

#include <algorithm>
#include <random>
#include <utility>

#include "kota/fs/directory.h"
#include "kota/fs/path.h"

#ifdef _WIN32
#include "win32.h"
#else
#include <fcntl.h>
#include <unistd.h>

#include "posix.h"
#endif

namespace kota::fs {

namespace {

/// Bytes past which one read or write call takes a piece at a time: Windows
/// counts in DWORD, POSIX in ssize_t, and either may stop short anyway.
constexpr std::size_t largest_call = std::size_t{1} << 30;

}  // namespace

File::File(File&& other) noexcept : handle(std::exchange(other.handle, closed)) {}

File& File::operator=(File&& other) noexcept {
    std::swap(handle, other.handle);
    return *this;
}

File::~File() {
    [[maybe_unused]] auto ignored = close();
}

bool File::is_open() const {
    return handle != closed;
}

#ifdef _WIN32

std::expected<File, std::error_code> File::open(std::string_view path, OpenMode mode) {
    auto native = windows::native_path(path);
    if(!native) {
        return std::unexpected(native.error());
    }
    DWORD access = mode == OpenMode::Read ? GENERIC_READ : GENERIC_WRITE;
    // CREATE_ALWAYS would drop a hidden or system file's attributes, or
    // refuse it: an existing file is emptied after it opens instead.
    DWORD disposition = mode == OpenMode::Read    ? OPEN_EXISTING
                        : mode == OpenMode::Write ? OPEN_ALWAYS
                                                  : CREATE_NEW;
    // A new file takes the place of nothing, a dangling symlink included.
    DWORD flags = mode == OpenMode::CreateNew ? FILE_FLAG_OPEN_REPARSE_POINT : 0;
    HANDLE handle = ::CreateFileW(native->c_str(),
                                  access,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr,
                                  disposition,
                                  FILE_ATTRIBUTE_NORMAL | flags,
                                  nullptr);
    if(handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(windows::last_error());
    }
    File file(handle);
    if(mode == OpenMode::Write && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        FILE_END_OF_FILE_INFO end{};
        if(!::SetFileInformationByHandle(handle, FileEndOfFileInfo, &end, sizeof(end))) {
            return std::unexpected(windows::last_error());
        }
    }
    return file;
}

std::expected<std::size_t, std::error_code> File::read(std::span<char> buffer) {
    DWORD count = 0;
    auto size = static_cast<DWORD>(std::min(buffer.size(), largest_call));
    if(!::ReadFile(handle, buffer.data(), size, &count, nullptr)) {
        auto error = ::GetLastError();
        if(error == ERROR_HANDLE_EOF || error == ERROR_BROKEN_PIPE) {
            return 0;
        }
        return std::unexpected(windows::to_error_code(error));
    }
    return count;
}

std::error_code File::write(std::string_view bytes) {
    while(!bytes.empty()) {
        DWORD count = 0;
        auto size = static_cast<DWORD>(std::min(bytes.size(), largest_call));
        if(!::WriteFile(handle, bytes.data(), size, &count, nullptr)) {
            return windows::last_error();
        }
        bytes.remove_prefix(count);
    }
    return {};
}

std::error_code File::close() {
    if(!is_open()) {
        return {};
    }
    auto closing = std::exchange(handle, closed);
    return ::CloseHandle(closing) ? std::error_code() : windows::last_error();
}

#else

std::expected<File, std::error_code> File::open(std::string_view path, OpenMode mode) {
    auto c_path = posix::c_path(path);
    if(!c_path) {
        return std::unexpected(c_path.error());
    }
    int flags = mode == OpenMode::Read    ? O_RDONLY
                : mode == OpenMode::Write ? O_WRONLY | O_CREAT | O_TRUNC
                                          : O_WRONLY | O_CREAT | O_EXCL;
    int fd;
    do {
        fd = ::open(c_path->c_str(), flags | O_CLOEXEC, 0666);
    } while(fd < 0 && errno == EINTR);
    if(fd < 0) {
        return std::unexpected(posix::last_error());
    }
    return File(fd);
}

std::expected<std::size_t, std::error_code> File::read(std::span<char> buffer) {
    while(true) {
        auto count = ::read(handle, buffer.data(), std::min(buffer.size(), largest_call));
        if(count >= 0) {
            return static_cast<std::size_t>(count);
        }
        if(errno != EINTR) {
            return std::unexpected(posix::last_error());
        }
    }
}

std::error_code File::write(std::string_view bytes) {
    while(!bytes.empty()) {
        auto count = ::write(handle, bytes.data(), std::min(bytes.size(), largest_call));
        if(count < 0) {
            if(errno == EINTR) {
                continue;
            }
            return posix::last_error();
        }
        bytes.remove_prefix(static_cast<std::size_t>(count));
    }
    return {};
}

std::error_code File::close() {
    if(!is_open()) {
        return {};
    }
    // The descriptor is gone whatever close() returns, EINTR included: a
    // retry could close one another thread has opened since.
    return ::close(std::exchange(handle, closed)) == 0 ? std::error_code() : posix::last_error();
}

#endif

std::expected<std::string, std::error_code> read_file(std::string_view path) {
    auto file = File::open(path, OpenMode::Read);
    if(!file) {
        return std::unexpected(file.error());
    }
    std::string content;
    // Files under /proc report no size, and any file may grow while read:
    // the end is where a read returns nothing.
    std::size_t at = 0;
    while(true) {
        if(at == content.size()) {
            content.resize(std::max<std::size_t>(content.size() * 2, 4096));
        }
        auto count = file->read(std::span(content).subspan(at));
        if(!count) {
            return std::unexpected(count.error());
        }
        if(*count == 0) {
            break;
        }
        at += *count;
    }
    content.resize(at);
    return content;
}

std::error_code write_file(std::string_view path, std::string_view bytes) {
    auto file = File::open(path, OpenMode::Write);
    if(!file) {
        return file.error();
    }
    if(auto error = file->write(bytes)) {
        return error;
    }
    return file->close();
}

namespace {

struct TempFile {
    File file;
    std::string path;
};

/// create_temp_file(), the file left open for writing.
std::expected<TempFile, std::error_code> open_temp_file(std::string_view directory,
                                                        std::string_view prefix,
                                                        std::string_view suffix) {
    constexpr std::string_view alphabet = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device device;
    std::uniform_int_distribution<std::size_t> pick(0, alphabet.size() - 1);
    // Another process may hold a name; it holds a hundred only by design.
    std::error_code error;
    for(int attempt = 0; attempt < 100; ++attempt) {
        std::string name(prefix);
        for(int i = 0; i < 12; ++i) {
            name.push_back(alphabet[pick(device)]);
        }
        name.append(suffix);
        auto candidate = path::join(directory, name);
        auto file = File::open(candidate, OpenMode::CreateNew);
        if(file) {
            return TempFile{std::move(*file), std::move(candidate)};
        }
        error = file.error();
        if(error != std::errc::file_exists) {
            break;
        }
    }
    return std::unexpected(error);
}

}  // namespace

std::error_code write_file_atomic(std::string_view path, std::string_view bytes) {
    auto sibling =
        open_temp_file(path::parent(path), std::string(path::filename(path)) + ".", ".tmp");
    if(!sibling) {
        return sibling.error();
    }
    auto error = sibling->file.write(bytes);
    if(auto closed = sibling->file.close(); !error) {
        error = closed;
    }
    if(!error) {
        error = rename(sibling->path, path);
    }
    if(error) {
        [[maybe_unused]] auto removed = remove(sibling->path);
    }
    return error;
}

std::expected<std::string, std::error_code> create_temp_file(std::string_view directory,
                                                             std::string_view prefix,
                                                             std::string_view suffix) {
    auto created = open_temp_file(directory, prefix, suffix);
    if(!created) {
        return std::unexpected(created.error());
    }
    if(auto error = created->file.close()) {
        return std::unexpected(error);
    }
    return std::move(created->path);
}

}  // namespace kota::fs
