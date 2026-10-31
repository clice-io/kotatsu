#include "kotatsu/fs/resolve.h"

#include <cstdlib>
#include <memory>

#include "kotatsu/support/small_string.h"
#include "kotatsu/fs/directory.h"
#include "kotatsu/fs/path.h"

#ifdef _WIN32
#include "win32.h"
#else
#include <cerrno>
#include <unistd.h>

#include "posix.h"
#endif

namespace kotatsu::fs {

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

void weakly_canonical(buffer_ref out, std::string_view path, path::DotDot dot_dot) {
    small_string<256> full;
    if(absolute(full, path)) {
        out.assign(path);
        path::normalize(out, dot_dot);
        return;
    }
    std::string_view existing = full;
    small_string<256> real;
    while(canonical(real, existing)) {
        existing = path::parent(existing);
        if(existing.empty()) {
            out.assign(full);
            path::normalize(out, dot_dot);
            return;
        }
    }
    auto rest = std::string_view(full).substr(existing.size());
    while(!rest.empty() && path::is_separator(rest.front())) {
        rest.remove_prefix(1);
    }
    path::resolve(out, rest, real, dot_dot);
}

std::error_code follow_symlinks(buffer_ref out, std::string_view path) {
    // No system takes a null in a name: read_symlink() would refuse it as it
    // refuses what is no symlink.
    if(path.find('\0') != std::string_view::npos) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    // Apart from `out`, which `path` may view.
    small_string<256> followed(path);
    small_string<256> target;
    for(int links = 0;; ++links) {
        // The last name itself, a separator after it left off, through which
        // POSIX would follow it.
        auto name = path::filename(followed);
        auto error = read_symlink(
            target,
            std::string_view(followed).substr(0, name.data() + name.size() - followed.data()));
        // No symlink, or nothing at all: where the links lead.
        if(error == std::errc::invalid_argument || error == std::errc::no_such_file_or_directory ||
           error == std::errc::not_a_directory) {
            break;
        }
        if(error) {
            return error;
        }
        if(links == 40) {
            return std::make_error_code(std::errc::too_many_symbolic_link_levels);
        }
        path::set_file_name(followed, target);
        // Windows resolves a link's `..` by text, and leaves it in a verbatim
        // path, which then names nothing; on POSIX it stays, climbing out of
        // wherever the link's directory leads.
        path::normalize(followed,
                        path::native_style == path::Style::Windows ? path::DotDot::Fold
                                                                   : path::DotDot::Keep);
    }
    out.assign(followed);
    return {};
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
    // A verbatim path is absolute already, and its names are as the file
    // system takes them: Win32 would fold its `..` and drop the trailing
    // dots and spaces they keep.
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

}  // namespace kotatsu::fs
