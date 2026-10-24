#include "kota/fs/resolve.h"

#include <cstdlib>
#include <memory>

#include "kota/fs/path.h"

#ifdef _WIN32
#include "win32.h"
#else
#include <cerrno>
#include <unistd.h>

#include "posix.h"
#endif

namespace kota::fs {

std::expected<std::string, std::error_code> canonical(std::string_view path) {
#ifdef _WIN32
    auto handle = win32::open(path, 0);
    if(!handle) {
        return std::unexpected(handle.error());
    }
    auto final = win32::fill_wide([&](wchar_t* buffer, DWORD size) {
        return ::GetFinalPathNameByHandleW(handle->get(),
                                           buffer,
                                           size,
                                           FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    });
    if(!final) {
        return std::unexpected(final.error());
    }
    return windows::from_native(*final);
#else
    auto c_path = posix::c_path(path);
    if(!c_path) {
        return std::unexpected(c_path.error());
    }
    std::unique_ptr<char, decltype(&std::free)> real(::realpath(c_path->c_str(), nullptr),
                                                     &std::free);
    if(!real) {
        return std::unexpected(posix::last_error());
    }
    return std::string(real.get());
#endif
}

std::string weakly_canonical(std::string_view path) {
    auto full = absolute(path);
    if(!full) {
        return path::normalize(path);
    }
    std::string_view existing = *full;
    while(true) {
        if(auto real = canonical(existing)) {
            auto rest = std::string_view(*full).substr(existing.size());
            while(!rest.empty() && path::is_separator(rest.front())) {
                rest.remove_prefix(1);
            }
            return path::normalize(path::join(*real, rest));
        }
        auto above = path::parent(existing);
        if(above.empty()) {
            return path::normalize(*full);
        }
        existing = above;
    }
}

std::expected<std::string, std::error_code> absolute(std::string_view path) {
    if(path.empty()) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
#ifdef _WIN32
    auto wide = win32::widen(path);
    if(!wide) {
        return std::unexpected(wide.error());
    }
    auto full = win32::full_path(*wide);
    if(!full) {
        return std::unexpected(full.error());
    }
    return windows::from_native(*full);
#else
    if(path::is_absolute(path)) {
        return path::normalize(path, path::DotDot::Keep);
    }
    auto cwd = current_dir();
    if(!cwd) {
        return std::unexpected(cwd.error());
    }
    return path::normalize(path::join(*cwd, path), path::DotDot::Keep);
#endif
}

std::expected<std::string, std::error_code> current_dir() {
#ifdef _WIN32
    auto cwd = win32::fill_wide(
        [](wchar_t* buffer, DWORD size) { return ::GetCurrentDirectoryW(size, buffer); });
    if(!cwd) {
        return std::unexpected(cwd.error());
    }
    return windows::from_native(*cwd);
#else
    std::string buffer(256, '\0');
    while(::getcwd(buffer.data(), buffer.size()) == nullptr) {
        if(errno != ERANGE) {
            return std::unexpected(posix::last_error());
        }
        buffer.resize(buffer.size() * 2);
    }
    buffer.resize(buffer.find('\0'));
    return buffer;
#endif
}

}  // namespace kota::fs
