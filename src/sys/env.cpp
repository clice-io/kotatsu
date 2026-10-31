#include "kotatsu/sys/env.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>

#include "os.h"
#include "kotatsu/support/small_string.h"
#include "kotatsu/support/string_ref.h"

#ifdef _WIN32
#include "kotatsu/support/utf16.h"
#elif defined(__APPLE__)
#include <crt_externs.h>
#else
extern char** environ;
#endif

namespace kotatsu::sys {

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

bool get_env(buffer_ref out, std::string_view name) {
    if(!valid_name(name)) {
        return false;
    }
#ifdef _WIN32
    small_wstring<64> wide_name;
    if(to_utf16(wide_name, name)) {
        return false;
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
            return false;
        }
        // Too small, it asks for the size with the NUL.
        if(size < value.size()) {
            from_utf16(out, std::wstring_view(value.data(), size));
            return true;
        }
        value.resize(size);
    }
#else
    small_string<64> key(name);
    std::lock_guard guard(lock);
    const char* value = std::getenv(key.c_str());
    if(value == nullptr) {
        return false;
    }
    out.assign(value);
    return true;
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
    small_string<64> key(name);
    small_string<256> text(value);
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
    small_string<64> key(name);
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

split_paths_view::iterator::iterator(std::string_view list, fs::path::Style style) :
    rest(list), style(style) {
    // An empty Windows list has no entry, an empty POSIX one an empty one.
    if(style == fs::path::Style::Windows && list.empty()) {
        done = true;
        return;
    }
    ++*this;
}

split_paths_view::iterator& split_paths_view::iterator::operator++() {
    if(last) {
        done = true;
        return *this;
    }
    bool windows = style == fs::path::Style::Windows;
    // The end of the entry: the first separator outside quotes.
    bool inside = false;
    quoted = false;
    std::size_t end = 0;
    for(; end < rest.size(); ++end) {
        char c = rest[end];
        if(windows && c == '"') {
            inside = !inside;
            quoted = true;
        } else if(c == (windows ? ';' : ':') && !inside) {
            break;
        }
    }
    entry = rest.substr(0, end);
    if(quoted) {
        unquoted.clear();
        for(char c: entry) {
            if(c != '"') {
                unquoted.push_back(c);
            }
        }
    }
    last = end == rest.size();
    rest.remove_prefix(std::min(end + 1, rest.size()));
    return *this;
}

std::error_code join_paths(buffer_ref out, string_list_ref paths, fs::path::Style style) {
    bool posix = style == fs::path::Style::Posix;
    out.clear();
    for(std::size_t i = 0; i < paths.size(); ++i) {
        auto path = paths[i];
        if(path.find(posix ? ':' : '"') != std::string_view::npos) {
            return std::make_error_code(std::errc::invalid_argument);
        }
        if(i != 0) {
            out.push_back(posix ? ':' : ';');
        }
        // Quoted, a lone empty entry is not the empty list.
        bool quote = !posix && (path.find(';') != std::string_view::npos ||
                                (paths.size() == 1 && path.empty()));
        if(quote) {
            out.push_back('"');
        }
        out.append(path);
        if(quote) {
            out.push_back('"');
        }
    }
    return {};
}

}  // namespace kotatsu::sys
