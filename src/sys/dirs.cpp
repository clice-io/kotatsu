#include "kota/sys/dirs.h"

#include <string_view>

#include "os.h"
#include "kota/fs/path.h"
#include "kota/sys/env.h"

#ifdef _WIN32
// The known folders' GUIDs, defined here rather than taken from uuid.lib.
#include <initguid.h>
#include <knownfolders.h>
#include <shlobj.h>

#include "kota/support/utf16.h"
#else
#include <pwd.h>
#endif

namespace kota::sys {

namespace {

#ifdef _WIN32

std::error_code known_folder(buffer_ref out, const KNOWNFOLDERID& id) {
    wchar_t* path = nullptr;
    auto result = ::SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path);
    // Freed whether or not it succeeded.
    if(SUCCEEDED(result)) {
        from_utf16(out, std::wstring_view(path));
    }
    ::CoTaskMemFree(path);
    if(FAILED(result)) {
        // A Win32 error wrapped in an HRESULT, or an HRESULT of its own.
        if(HRESULT_FACILITY(result) == FACILITY_WIN32) {
            return fs::windows::to_error_code(HRESULT_CODE(result));
        }
        return std::error_code(static_cast<int>(result), std::system_category());
    }
    return {};
}

#else

/// Directory `relative` in the home directory.
std::error_code in_home(buffer_ref out, std::string_view relative) {
    if(auto error = home_dir(out)) {
        return error;
    }
    fs::path::push(out, relative);
    return {};
}

#endif

#if !defined(_WIN32) && !defined(__APPLE__)

/// The path in XDG variable `name` when it is absolute, else `fallback` in
/// the home directory.
std::error_code xdg_dir(buffer_ref out, std::string_view name, std::string_view fallback) {
    if(get_env(out, name) && fs::path::is_absolute(out.view())) {
        return {};
    }
    return in_home(out, fallback);
}

#endif

}  // namespace

std::error_code home_dir(buffer_ref out) {
#ifdef _WIN32
    if(get_env(out, "USERPROFILE") && !out.empty()) {
        return {};
    }
    return known_folder(out, FOLDERID_Profile);
#else
    if(get_env(out, "HOME") && !out.empty()) {
        return {};
    }
    passwd entry{};
    passwd* found = nullptr;
    std::string buffer(1024, '\0');
    while(true) {
        auto error = ::getpwuid_r(::getuid(), &entry, buffer.data(), buffer.size(), &found);
        if(error == ERANGE) {
            buffer.resize(buffer.size() * 2);
            continue;
        }
        if(error != 0) {
            return std::error_code(error, std::generic_category());
        }
        if(found == nullptr) {
            return std::make_error_code(std::errc::no_such_file_or_directory);
        }
        out.assign(found->pw_dir);
        return {};
    }
#endif
}

std::error_code cache_dir(buffer_ref out) {
#ifdef _WIN32
    return known_folder(out, FOLDERID_LocalAppData);
#elif defined(__APPLE__)
    return in_home(out, "Library/Caches");
#else
    return xdg_dir(out, "XDG_CACHE_HOME", ".cache");
#endif
}

std::error_code config_dir(buffer_ref out) {
#ifdef _WIN32
    return known_folder(out, FOLDERID_RoamingAppData);
#elif defined(__APPLE__)
    return in_home(out, "Library/Application Support");
#else
    return xdg_dir(out, "XDG_CONFIG_HOME", ".config");
#endif
}

std::error_code data_dir(buffer_ref out) {
#ifdef _WIN32
    return known_folder(out, FOLDERID_LocalAppData);
#elif defined(__APPLE__)
    return in_home(out, "Library/Application Support");
#else
    return xdg_dir(out, "XDG_DATA_HOME", ".local/share");
#endif
}

}  // namespace kota::sys
