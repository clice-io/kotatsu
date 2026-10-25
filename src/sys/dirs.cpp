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

std::expected<std::string, std::error_code> known_folder(const KNOWNFOLDERID& id) {
    wchar_t* path = nullptr;
    auto result = ::SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path);
    // Freed whether or not it succeeded.
    std::string folder;
    if(SUCCEEDED(result)) {
        from_utf16(folder, std::wstring_view(path));
    }
    ::CoTaskMemFree(path);
    if(FAILED(result)) {
        // A Win32 error wrapped in an HRESULT, or an HRESULT of its own.
        if(HRESULT_FACILITY(result) == FACILITY_WIN32) {
            return std::unexpected(fs::windows::to_error_code(HRESULT_CODE(result)));
        }
        return std::unexpected(std::error_code(static_cast<int>(result), std::system_category()));
    }
    return folder;
}

#else

/// Directory `relative` in the home directory.
std::expected<std::string, std::error_code> in_home(std::string_view relative) {
    auto home = home_dir();
    if(home) {
        fs::path::push(*home, relative);
    }
    return home;
}

#endif

#if !defined(_WIN32) && !defined(__APPLE__)

/// The path in XDG variable `name` when it is absolute, else `fallback` in
/// the home directory.
std::expected<std::string, std::error_code> xdg_dir(std::string_view name,
                                                    std::string_view fallback) {
    if(auto value = get_env(name); value && fs::path::is_absolute(*value)) {
        return std::move(*value);
    }
    return in_home(fallback);
}

#endif

}  // namespace

std::expected<std::string, std::error_code> home_dir() {
#ifdef _WIN32
    if(auto profile = get_env("USERPROFILE"); profile && !profile->empty()) {
        return std::move(*profile);
    }
    return known_folder(FOLDERID_Profile);
#else
    if(auto home = get_env("HOME"); home && !home->empty()) {
        return std::move(*home);
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
            return std::unexpected(std::error_code(error, std::generic_category()));
        }
        if(found == nullptr) {
            return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
        }
        return std::string(found->pw_dir);
    }
#endif
}

std::expected<std::string, std::error_code> cache_dir() {
#ifdef _WIN32
    return known_folder(FOLDERID_LocalAppData);
#elif defined(__APPLE__)
    return in_home("Library/Caches");
#else
    return xdg_dir("XDG_CACHE_HOME", ".cache");
#endif
}

std::expected<std::string, std::error_code> config_dir() {
#ifdef _WIN32
    return known_folder(FOLDERID_RoamingAppData);
#elif defined(__APPLE__)
    return in_home("Library/Application Support");
#else
    return xdg_dir("XDG_CONFIG_HOME", ".config");
#endif
}

std::expected<std::string, std::error_code> data_dir() {
#ifdef _WIN32
    return known_folder(FOLDERID_LocalAppData);
#elif defined(__APPLE__)
    return in_home("Library/Application Support");
#else
    return xdg_dir("XDG_DATA_HOME", ".local/share");
#endif
}

}  // namespace kota::sys
