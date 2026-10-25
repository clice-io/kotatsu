#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "os.h"
#include "kota/support/small_string.h"
#include "kota/support/string_list_ref.h"
#include "kota/support/string_ref.h"
#include "kota/fs/path.h"
#include "kota/sys/env.h"
#include "kota/sys/process.h"
#include "kota/sys/windows.h"

#ifdef _WIN32
#include "kota/support/utf16.h"
#include "kota/fs/cygwin.h"
#else
#include <fcntl.h>

#include "kota/fs/status.h"
#endif

namespace kota::sys {

namespace {

/// What a candidate file is to a search.
enum class Candidate : std::uint8_t {
    Missing,
    /// There, but no program: a directory, or a file without execute
    /// permission.
    Unrunnable,
    Program,
};

Candidate examine(std::string_view path) {
#ifdef _WIN32
    // By name, without opening the file: the cheapest query, which a search
    // through every directory of PATH makes for each extension.
    small_wstring<264> native;
    if(fs::windows::to_native(native, path)) {
        return Candidate::Missing;
    }
    auto attributes = ::GetFileAttributesW(native.c_str());
    if(attributes == INVALID_FILE_ATTRIBUTES) {
        return Candidate::Missing;
    }
    return attributes & FILE_ATTRIBUTE_DIRECTORY ? Candidate::Unrunnable : Candidate::Program;
#else
    auto status = fs::status(path);
    if(!status) {
        // A directory on the way that cannot be searched hides a program, as
        // execvp's EACCES says.
        return status.error() == std::errc::permission_denied ? Candidate::Unrunnable
                                                              : Candidate::Missing;
    }
    small_string<256> c_path(path);
    // By the effective IDs, which exec goes by, as access does not.
    if(status->type != fs::FileType::Regular ||
       ::faccessat(AT_FDCWD, c_path.c_str(), X_OK, AT_EACCESS) != 0) {
        return Candidate::Unrunnable;
    }
    return Candidate::Program;
#endif
}

#ifdef _WIN32

/// The names `name` is tried as in a directory: with `extensions`, or as
/// CreateProcess tries it.
std::vector<std::string> spellings(std::string_view name,
                                   string_list_ref extensions,
                                   bool like_create_process) {
    std::vector<std::string> tried;
    if(like_create_process) {
        tried = {".exe"};
    } else if(!extensions.empty()) {
        for(auto extension: extensions) {
            tried.emplace_back(extension);
        }
    } else {
        small_string<256> pathext;
        if(get_env(pathext, "PATHEXT")) {
            for(auto extension: split_paths(pathext)) {
                if(!extension.empty()) {
                    tried.emplace_back(extension);
                }
            }
        }
        if(tried.empty()) {
            tried = {".COM", ".EXE", ".BAT", ".CMD"};
        }
    }
    auto own = fs::path::extension(name);
    bool as_is = !own.empty() && (like_create_process ||
                                  std::ranges::any_of(tried, [&](std::string_view extension) {
                                      return string_ref(own).equals_insensitive(extension);
                                  }));
    if(as_is) {
        return {std::string(name)};
    }
    std::vector<std::string> names;
    for(const auto& extension: tried) {
        names.push_back(std::string(name) + extension);
    }
    return names;
}

/// Follows the Cygwin symlink at `path` to the program it names, in place:
/// its target beside it, link after link, `.exe` added where the target has
/// none; a program that is no link stays. False, `path` the link it stopped
/// at, when a link names a POSIX path, which only its runtime's mount table
/// maps, or nothing that runs, or the links loop.
bool follow_cygwin_links(buffer_ref path) {
    // Past it, a chain is taken to loop.
    constexpr int most_links = 16;
    small_string<256> target;
    small_string<256> link;
    for(int links = 0; links <= most_links; ++links) {
        if(!fs::cygwin::read_symlink(target, path.view())) {
            return true;
        }
        if(fs::cygwin::is_posix_path(target)) {
            return false;
        }
        link.assign(path.view());
        fs::path::pop(path);
        fs::path::push(path, target);
        if(examine(path.view()) != Candidate::Program) {
            path.append(".exe");
            if(examine(path.view()) != Candidate::Program) {
                path.assign(link);
                return false;
            }
        }
    }
    return false;
}

#endif

/// Writes into `out` the program `name` names, looked for in `directories`
/// by find_program's rules, or on Windows CreateProcess's.
template <typename Directories>
std::error_code find(buffer_ref out,
                     std::string_view name,
                     const Directories& directories,
                     [[maybe_unused]] string_list_ref extensions,
                     [[maybe_unused]] bool like_create_process) {
    if(name.empty()) {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }
    // The first file found that cannot be run, which permission_denied
    // names.
    small_string<256> unrunnable;
    // Whether the candidate in `out` is a program, which it then names.
    auto runs = [&] {
        switch(examine(out.view())) {
            case Candidate::Program: {
#ifdef _WIN32
                if(like_create_process || follow_cygwin_links(out)) {
                    return true;
                }
                break;
#else
                return true;
#endif
            }
            case Candidate::Unrunnable: break;
            case Candidate::Missing: return false;
        }
        if(unrunnable.empty()) {
            unrunnable.assign(out.view());
        }
        return false;
    };
    auto not_found = [&] {
        if(unrunnable.empty()) {
            return std::make_error_code(std::errc::no_such_file_or_directory);
        }
        out.assign(unrunnable);
        return std::make_error_code(std::errc::permission_denied);
    };

#ifdef _WIN32
    auto names = spellings(name, extensions, like_create_process);
    // A separator or a drive (`C:tool`, in that drive's current directory)
    // makes it a path.
    if(!fs::path::root(name).name.empty() ||
       name.find_first_of(R"(/\)") != std::string_view::npos) {
        for(const auto& spelling: names) {
            out.assign(spelling);
            if(runs()) {
                return {};
            }
        }
        return not_found();
    }
#else
    if(name.find('/') != std::string_view::npos) {
        out.assign(name);
        return runs() ? std::error_code() : not_found();
    }
#endif

    for(std::string_view directory: directories) {
#ifdef _WIN32
        if(directory.empty()) {
            continue;
        }
        for(const auto& spelling: names) {
            out.assign(directory);
            fs::path::push(out, spelling);
            if(runs()) {
                return {};
            }
        }
#else
        // An empty entry is the current directory.
        out.assign(directory.empty() ? "." : directory);
        fs::path::push(out, name);
        if(runs()) {
            return {};
        }
#endif
    }
    return not_found();
}

}  // namespace

std::error_code find_program(buffer_ref out, std::string_view name, const ProgramSearch& search) {
    if(!search.directories.empty()) {
        return find(out, name, search.directories, search.extensions, false);
    }
    // Unset, it is empty on Windows, and confstr(_CS_PATH)'s directories on
    // POSIX.
    small_string<1024> path;
    [[maybe_unused]] const bool set = get_env(path, "PATH");
#ifndef _WIN32
    if(!set) {
        path.assign(default_path());
    }
#endif
    return find(out, name, split_paths(path), search.extensions, false);
}

#ifdef _WIN32

namespace {

/// Writes into `out` a directory a Win32 call `read` gives, which asks for
/// the size with the NUL when the buffer is too small.
std::error_code known_directory(buffer_ref out, auto read) {
    small_wstring<264> path;
    path.resize(MAX_PATH);
    while(true) {
        auto size = read(static_cast<DWORD>(path.size()), path.data());
        if(size == 0) {
            return fs::windows::last_error();
        }
        if(size < path.size()) {
            from_utf16(out, std::wstring_view(path.data(), size));
            return {};
        }
        path.resize(size);
    }
}

}  // namespace

std::expected<std::vector<std::string>, std::error_code> system_directories() {
    std::vector<std::string> directories(3);
    if(auto error = known_directory(directories[0], [](DWORD size, wchar_t* buffer) {
           return ::GetSystemDirectoryW(buffer, size);
       })) {
        return std::unexpected(error);
    }
    if(auto error = known_directory(directories[2], [](DWORD size, wchar_t* buffer) {
           return ::GetWindowsDirectoryW(buffer, size);
       })) {
        return std::unexpected(error);
    }
    directories[1] = directories[2];
    fs::path::push(directories[1], "System");
    return directories;
}

std::expected<std::vector<std::string>, std::error_code> windows::create_process_directories() {
    std::string executable;
    if(auto error = executable_path(executable)) {
        return std::unexpected(error);
    }
    std::vector<std::string> directories;
    directories.emplace_back(fs::path::parent(executable));
    if(auto error = known_directory(directories.emplace_back(), [](DWORD size, wchar_t* buffer) {
           return ::GetCurrentDirectoryW(size, buffer);
       })) {
        return std::unexpected(error);
    }
    auto system = system_directories();
    if(!system) {
        return std::unexpected(system.error());
    }
    directories.insert(directories.end(), system->begin(), system->end());
    small_string<1024> path;
    if(get_env(path, "PATH")) {
        for(auto entry: split_paths(path)) {
            directories.emplace_back(entry);
        }
    }
    return directories;
}

std::error_code windows::find_create_process_program(buffer_ref out,
                                                     std::string_view name,
                                                     string_list_ref directories) {
    return find(out, name, directories, {}, true);
}

#endif

}  // namespace kota::sys
