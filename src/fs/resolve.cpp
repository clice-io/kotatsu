#include "kota/fs/resolve.h"

#include <cstdlib>
#include <memory>

#include "kota/support/small_string.h"
#include "kota/fs/path.h"

#ifdef _WIN32
#include "win32.h"
#else
#include <cerrno>
#include <unistd.h>

#include "posix.h"
#endif

namespace kota::fs {

std::error_code canonical(buffer_ref out, std::string_view path) {
#ifdef _WIN32
    auto handle = win32::open(path, 0);
    if(!handle) {
        return handle.error();
    }
    return win32::final_path(out, handle->get());
#else
    posix::CPath c_path;
    if(auto error = c_path.assign(path)) {
        return error;
    }
    std::unique_ptr<char, decltype(&std::free)> real(::realpath(c_path.c_str(), nullptr),
                                                     &std::free);
    if(!real) {
        return posix::last_error();
    }
    out.assign(real.get());
    return {};
#endif
}

void weakly_canonical(buffer_ref out, std::string_view path) {
    small_string<256> full;
    if(absolute(full, path)) {
        out.assign(path);
        path::normalize(out);
        return;
    }
    std::string_view existing = full;
    small_string<256> real;
    while(canonical(real, existing)) {
        existing = path::parent(existing);
        if(existing.empty()) {
            out.assign(full);
            path::normalize(out);
            return;
        }
    }
    auto rest = std::string_view(full).substr(existing.size());
    while(!rest.empty() && path::is_separator(rest.front())) {
        rest.remove_prefix(1);
    }
    path::push(real, rest);
    path::normalize(real);
    out.assign(real);
}

std::error_code absolute(buffer_ref out, std::string_view path) {
    if(path.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
#ifdef _WIN32
    win32::WidePath wide;
    if(auto error = win32::widen(wide, path)) {
        return error;
    }
    // A verbatim path is absolute as it is: Win32 would fold its `..` and
    // drop the trailing dots and spaces its names keep.
    if(path.starts_with(R"(\\?\)")) {
        windows::from_native(out, std::wstring_view(wide.data(), wide.size()));
        return {};
    }
    win32::WidePath full;
    if(auto error = win32::full_path(full, wide.c_str())) {
        return error;
    }
    windows::from_native(out, full);
    return {};
#else
    if(path::is_absolute(path)) {
        out.assign(path);
    } else {
        small_string<256> joined;
        if(auto error = current_dir(joined)) {
            return error;
        }
        path::push(joined, path);
        out.assign(joined);
    }
    path::normalize(out, path::DotDot::Keep);
    return {};
#endif
}

std::error_code current_dir(buffer_ref out) {
#ifdef _WIN32
    win32::WidePath cwd;
    if(auto error = win32::fill_wide(cwd, [](wchar_t* buffer, DWORD size) {
           return ::GetCurrentDirectoryW(size, buffer);
       })) {
        return error;
    }
    windows::from_native(out, cwd);
    return {};
#else
    auto buffer = out.resize(256);
    while(::getcwd(buffer.data(), buffer.size()) == nullptr) {
        if(errno != ERANGE) {
            return posix::last_error();
        }
        buffer = out.resize(buffer.size() * 2);
    }
    out.resize(std::string_view(buffer.data()).size());
    return {};
#endif
}

}  // namespace kota::fs
