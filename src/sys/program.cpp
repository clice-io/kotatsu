#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "os.h"
#include "kota/support/string_ref.h"
#include "kota/fs/path.h"
#include "kota/sys/env.h"
#include "kota/sys/process.h"
#include "kota/sys/windows.h"

#ifdef _WIN32
#include "kota/support/utf16.h"
#include "kota/fs/cygwin.h"
#else
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

Candidate examine(const std::string& path) {
#ifdef _WIN32
    // By name, without opening the file: the cheapest query, which a search
    // through every directory of PATH makes for each extension.
    auto native = fs::windows::native_path(path);
    if(!native) {
        return Candidate::Missing;
    }
    auto attributes = ::GetFileAttributesW(native->c_str());
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
    if(status->type != fs::FileType::Regular || ::access(path.c_str(), X_OK) != 0) {
        return Candidate::Unrunnable;
    }
    return Candidate::Program;
#endif
}

/// The entries of the list in variable `name`, or of `fallback` when it is
/// unset.
std::vector<std::string> list_variable(std::string_view name, std::string_view fallback) {
    auto value = get_env(name);
    return split_paths(value ? std::string_view(*value) : fallback);
}

#ifdef _WIN32

/// The names `name` is tried as in a directory: with `extensions`, or as
/// CreateProcess tries it.
std::vector<std::string> spellings(std::string_view name,
                                   std::vector<std::string> extensions,
                                   bool like_create_process) {
    if(like_create_process) {
        extensions = {".exe"};
    } else if(extensions.empty()) {
        extensions = list_variable("PATHEXT", "");
        std::erase(extensions, "");
        if(extensions.empty()) {
            extensions = {".COM", ".EXE", ".BAT", ".CMD"};
        }
    }
    auto own = fs::path::extension(name);
    bool as_is = !own.empty() && (like_create_process ||
                                  std::ranges::any_of(extensions, [&](std::string_view extension) {
                                      return string_ref(own).equals_insensitive(extension);
                                  }));
    if(as_is) {
        return {std::string(name)};
    }
    std::vector<std::string> names;
    for(const auto& extension: extensions) {
        names.push_back(std::string(name) + extension);
    }
    return names;
}

/// The program the Cygwin symlink at `path`, a program itself when it is
/// none, names: its target beside it, link after link, `.exe` added where
/// the target has none. None when a link names a POSIX path, which only its
/// runtime's mount table maps, or nothing that runs, or the links loop.
std::optional<std::string> follow_cygwin_links(std::string path) {
    // Past it, a chain is taken to loop.
    constexpr int most_links = 16;
    for(int links = 0; links <= most_links; ++links) {
        auto target = fs::cygwin::read_symlink(path);
        if(!target) {
            return path;
        }
        if(fs::cygwin::is_posix_path(*target)) {
            return std::nullopt;
        }
        auto next = fs::path::join(fs::path::parent(path), *target);
        if(examine(next) != Candidate::Program) {
            next += ".exe";
            if(examine(next) != Candidate::Program) {
                return std::nullopt;
            }
        }
        path = std::move(next);
    }
    return std::nullopt;
}

#endif

/// The program `name` names, looked for in `directories` by find_program's
/// rules, or on Windows CreateProcess's.
std::expected<std::string, std::error_code>
    find(std::string_view name,
         std::span<const std::string> directories,
         [[maybe_unused]] std::vector<std::string> extensions,
         [[maybe_unused]] bool like_create_process) {
    if(name.empty()) {
        return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    }
    bool found_unrunnable = false;
    // The first program among `candidates`.
    auto first = [&](const std::vector<std::string>& candidates) -> std::optional<std::string> {
        for(const auto& candidate: candidates) {
            switch(examine(candidate)) {
                case Candidate::Program: {
#ifdef _WIN32
                    if(like_create_process) {
                        return candidate;
                    }
                    if(auto program = follow_cygwin_links(candidate)) {
                        return program;
                    }
                    found_unrunnable = true;
                    break;
#else
                    return candidate;
#endif
                }
                case Candidate::Unrunnable: found_unrunnable = true; break;
                case Candidate::Missing: break;
            }
        }
        return std::nullopt;
    };
    auto not_found = [&] {
        return std::unexpected(std::make_error_code(found_unrunnable
                                                        ? std::errc::permission_denied
                                                        : std::errc::no_such_file_or_directory));
    };

#ifdef _WIN32
    auto names = spellings(name, std::move(extensions), like_create_process);
    // A separator or a drive (`C:tool`, in that drive's current directory)
    // makes it a path.
    if(!fs::path::root(name).name.empty() ||
       name.find_first_of(R"(/\)") != std::string_view::npos) {
        if(auto program = first(names)) {
            return std::move(*program);
        }
        return not_found();
    }
#else
    if(name.find('/') != std::string_view::npos) {
        if(auto program = first({std::string(name)})) {
            return std::move(*program);
        }
        return not_found();
    }
#endif

    for(const auto& directory: directories) {
#ifdef _WIN32
        if(directory.empty()) {
            continue;
        }
        std::vector<std::string> candidates;
        for(const auto& spelling: names) {
            candidates.push_back(fs::path::join(directory, spelling));
        }
        if(auto program = first(candidates)) {
            return std::move(*program);
        }
#else
        // An empty entry is the current directory.
        if(auto program = first({fs::path::join(directory.empty() ? "." : directory, name)})) {
            return std::move(*program);
        }
#endif
    }
    return not_found();
}

}  // namespace

std::expected<std::string, std::error_code> find_program(std::string_view name,
                                                         const ProgramSearch& search) {
    if(!search.directories.empty()) {
        return find(name, search.directories, search.extensions, false);
    }
#ifdef _WIN32
    auto directories = list_variable("PATH", "");
#else
    auto directories = list_variable("PATH", default_path());
#endif
    return find(name, directories, search.extensions, false);
}

#ifdef _WIN32

namespace {

/// A directory a Win32 call `read` gives, which asks for the size with the
/// NUL when the buffer is too small.
std::expected<std::string, std::error_code> known_directory(auto read) {
    std::wstring path(MAX_PATH, L'\0');
    while(true) {
        auto size = read(static_cast<DWORD>(path.size()), path.data());
        if(size == 0) {
            return std::unexpected(fs::windows::last_error());
        }
        if(size < path.size()) {
            path.resize(size);
            return from_utf16(std::wstring_view(path));
        }
        path.resize(size);
    }
}

}  // namespace

std::expected<std::vector<std::string>, std::error_code> system_directories() {
    auto system = known_directory(
        [](DWORD size, wchar_t* buffer) { return ::GetSystemDirectoryW(buffer, size); });
    if(!system) {
        return std::unexpected(system.error());
    }
    auto windows = known_directory(
        [](DWORD size, wchar_t* buffer) { return ::GetWindowsDirectoryW(buffer, size); });
    if(!windows) {
        return std::unexpected(windows.error());
    }
    auto system16 = fs::path::join(*windows, "System");
    return std::vector<std::string>{std::move(*system), std::move(system16), std::move(*windows)};
}

std::expected<std::vector<std::string>, std::error_code> windows::create_process_directories() {
    auto executable = executable_path();
    if(!executable) {
        return std::unexpected(executable.error());
    }
    auto current = known_directory(
        [](DWORD size, wchar_t* buffer) { return ::GetCurrentDirectoryW(size, buffer); });
    if(!current) {
        return std::unexpected(current.error());
    }
    auto system = system_directories();
    if(!system) {
        return std::unexpected(system.error());
    }
    std::vector<std::string> directories = {std::string(fs::path::parent(*executable)),
                                            std::move(*current)};
    directories.insert(directories.end(), system->begin(), system->end());
    for(auto& entry: list_variable("PATH", "")) {
        directories.push_back(std::move(entry));
    }
    return directories;
}

std::expected<std::string, std::error_code>
    windows::find_create_process_program(std::string_view name,
                                         std::span<const std::string> directories) {
    return find(name, directories, {}, true);
}

#endif

}  // namespace kota::sys
