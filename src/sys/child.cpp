#include "child.h"

#include <string_view>

#include "kota/fs/path.h"
#include "kota/sys/process.h"
#include "kota/sys/windows.h"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace kota::sys {

std::optional<std::vector<EnvVar>> child_environment(const SpawnOptions& options) {
    if(!options.env && options.env_changes.empty()) {
        return std::nullopt;
    }
    auto variables = options.env ? *options.env : environment();
    for(const auto& change: options.env_changes) {
        std::erase_if(variables, [&](const EnvVar& variable) {
            return env_name_equal(variable.name, change.name);
        });
        if(change.value) {
            variables.push_back({.name = change.name, .value = *change.value});
        }
    }
    return variables;
}

std::expected<std::string, std::error_code>
    child_program(const SpawnOptions& options,
                  const std::optional<std::vector<EnvVar>>& environment) {
    std::string_view file = options.file;
#ifdef _WIN32
    // A separator or a drive (`C:tool`) makes it a path.
    const bool is_path =
        !fs::path::root(file).name.empty() || file.find_first_of(R"(/\)") != std::string_view::npos;
#else
    const bool is_path = file.find('/') != std::string_view::npos;
#endif
    // The child's directory, for what is relative.
    auto in_cwd = [&](std::string_view path) {
        return options.cwd.empty() || fs::path::is_absolute(path)
                   ? std::string(path)
                   : fs::path::join(options.cwd, path);
    };
    if(file.empty()) {
        return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    }
    if(is_path) {
        return in_cwd(file);
    }

    std::optional<std::string> path;
    if(environment) {
        for(const auto& variable: *environment) {
            if(env_name_equal(variable.name, "PATH")) {
                path = variable.value;
            }
        }
    } else {
        path = get_env("PATH");
    }
    std::vector<std::string> directories;
#ifdef _WIN32
    if(path) {
        directories = split_paths(*path);
    }
#else
    if(!path) {
        // What execvp searches without one.
        path = std::string(::confstr(_CS_PATH, nullptr, 0), '\0');
        ::confstr(_CS_PATH, path->data(), path->size());
        path->pop_back();
    }
    directories = split_paths(*path);
#endif
    for(auto& directory: directories) {
#ifndef _WIN32
        // The current directory.
        if(directory.empty()) {
            directory = ".";
        }
#endif
        directory = in_cwd(directory);
    }
#ifdef _WIN32
    return windows::find_create_process_program(file, directories);
#else
    return find_program(file, {.directories = std::move(directories)});
#endif
}

}  // namespace kota::sys
