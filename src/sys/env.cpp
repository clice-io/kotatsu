#include "kota/sys/env.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>

#include "os.h"
#include "kota/support/string_ref.h"

#ifdef _WIN32
#include "kota/support/utf16.h"
#elif defined(__APPLE__)
#include <crt_externs.h>
#else
extern char** environ;
#endif

namespace kota::sys {

namespace {

bool valid_name(std::string_view name) {
    if(name.empty() || name.find('\0') != std::string_view::npos) {
        return false;
    }
#ifdef _WIN32
    return name.find('=', 1) == std::string_view::npos;
#else
    return name.find('=') == std::string_view::npos;
#endif
}

#ifndef _WIN32

/// Held around getenv, setenv and environ, whose array setenv may move.
constinit std::mutex lock;

#endif

}  // namespace

std::optional<std::string> get_env(std::string_view name) {
    if(!valid_name(name)) {
        return std::nullopt;
    }
#ifdef _WIN32
    small_wstring<64> wide_name;
    if(to_utf16(wide_name, name)) {
        return std::nullopt;
    }
    small_wstring<256> value;
    value.resize(256);
    while(true) {
        // An empty value reads as 0 too, without an error.
        ::SetLastError(ERROR_SUCCESS);
        auto size = ::GetEnvironmentVariableW(wide_name.c_str(),
                                              value.data(),
                                              static_cast<DWORD>(value.size()));
        if(size == 0 && ::GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
            return std::nullopt;
        }
        // Too small, it asks for the size with the NUL.
        if(size < value.size()) {
            std::string text;
            from_utf16(text, std::wstring_view(value.data(), size));
            return text;
        }
        value.resize(size);
    }
#else
    std::string key(name);
    std::lock_guard guard(lock);
    const char* value = std::getenv(key.c_str());
    if(value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

std::error_code set_env(std::string_view name, std::string_view value) {
    if(!valid_name(name) || value.find('\0') != std::string_view::npos) {
        return std::make_error_code(std::errc::invalid_argument);
    }
#ifdef _WIN32
    small_wstring<64> wide_name;
    if(auto error = to_utf16(wide_name, name)) {
        return error;
    }
    small_wstring<256> wide_value;
    if(auto error = to_utf16(wide_value, value)) {
        return error;
    }
    if(!::SetEnvironmentVariableW(wide_name.c_str(), wide_value.c_str())) {
        return fs::windows::last_error();
    }
    return {};
#else
    std::string key(name);
    std::string text(value);
    std::lock_guard guard(lock);
    if(::setenv(key.c_str(), text.c_str(), 1) != 0) {
        return errno_code();
    }
    return {};
#endif
}

std::error_code unset_env(std::string_view name) {
    if(!valid_name(name)) {
        return std::make_error_code(std::errc::invalid_argument);
    }
#ifdef _WIN32
    small_wstring<64> wide_name;
    if(auto error = to_utf16(wide_name, name)) {
        return error;
    }
    if(!::SetEnvironmentVariableW(wide_name.c_str(), nullptr) &&
       ::GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
        return fs::windows::last_error();
    }
    return {};
#else
    std::string key(name);
    std::lock_guard guard(lock);
    if(::unsetenv(key.c_str()) != 0) {
        return errno_code();
    }
    return {};
#endif
}

std::vector<EnvVar> environment() {
    std::vector<EnvVar> entries;
    // The name ends at the first '=' after its first character, which is '='
    // in the Windows `=C:` variables.
    auto add = [&](std::string_view entry) {
        auto end = std::min(entry.find('=', 1), entry.size());
        entries.push_back({
            .name = std::string(entry.substr(0, end)),
            .value = std::string(entry.substr(std::min(end + 1, entry.size()))),
        });
    };
#ifdef _WIN32
    wchar_t* block = ::GetEnvironmentStringsW();
    if(block == nullptr) {
        return entries;
    }
    small_string<256> utf8;
    for(const wchar_t* entry = block; *entry != L'\0';) {
        std::wstring_view text(entry);
        from_utf16(utf8, text);
        add(utf8);
        entry += text.size() + 1;
    }
    ::FreeEnvironmentStringsW(block);
#else
    std::lock_guard guard(lock);
#ifdef __APPLE__
    char** block = *_NSGetEnviron();
#else
    char** block = environ;
#endif
    // None after clearenv().
    for(char** entry = block; entry != nullptr && *entry != nullptr; ++entry) {
        add(*entry);
    }
#endif
    return entries;
}

bool env_name_equal(std::string_view left, std::string_view right) {
#ifdef _WIN32
    if(string_ref(left).equals_insensitive(right)) {
        return true;
    }
    // Past ASCII, as Windows folds case.
    small_wstring<64> wide_left;
    small_wstring<64> wide_right;
    return !to_utf16(wide_left, left) && !to_utf16(wide_right, right) &&
           ::CompareStringOrdinal(wide_left.data(),
                                  static_cast<int>(wide_left.size()),
                                  wide_right.data(),
                                  static_cast<int>(wide_right.size()),
                                  TRUE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

std::vector<std::string> split_paths(std::string_view list, fs::path::Style style) {
    std::vector<std::string> paths;
    if(style == fs::path::Style::Posix) {
        std::size_t at = 0;
        while(true) {
            auto end = list.find(':', at);
            paths.emplace_back(list.substr(at, end - at));
            if(end == std::string_view::npos) {
                return paths;
            }
            at = end + 1;
        }
    }
    if(list.empty()) {
        return paths;
    }
    std::string current;
    bool quoted = false;
    for(char c: list) {
        if(c == '"') {
            quoted = !quoted;
        } else if(c == ';' && !quoted) {
            paths.push_back(std::move(current));
            current.clear();
        } else {
            current += c;
        }
    }
    paths.push_back(std::move(current));
    return paths;
}

std::expected<std::string, std::error_code> join_paths(std::span<const std::string> paths,
                                                       fs::path::Style style) {
    bool posix = style == fs::path::Style::Posix;
    std::string list;
    for(std::size_t i = 0; i < paths.size(); ++i) {
        std::string_view path = paths[i];
        if(path.find(posix ? ':' : '"') != std::string_view::npos) {
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }
        if(i != 0) {
            list += posix ? ':' : ';';
        }
        // Quoted, a lone empty entry is not the empty list.
        if(!posix &&
           (path.find(';') != std::string_view::npos || (paths.size() == 1 && path.empty()))) {
            list += '"';
            list += path;
            list += '"';
        } else {
            list += path;
        }
    }
    return list;
}

}  // namespace kota::sys
