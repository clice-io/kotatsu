#include "child.h"

#include <string_view>
#include <utility>

#include "os.h"
#include "kota/fs/path.h"
#include "kota/fs/resolve.h"
#include "kota/sys/process.h"
#include "kota/sys/windows.h"

namespace kota::sys {

Child::Child(Child&& other) noexcept {
    swap(other);
}

Child& Child::operator=(Child&& other) noexcept {
    Child taken(std::move(other));
    swap(taken);
    return *this;
}

void Child::swap(Child& other) noexcept {
    std::swap(id, other.id);
    std::swap(handle, other.handle);
    std::swap(pipes, other.pipes);
    std::swap(exited, other.exited);
    std::swap(leads_group, other.leads_group);
#ifdef _WIN32
    std::swap(job, other.job);
    std::swap(killed_by, other.killed_by);
#endif
}

fs::File::native_handle_type Child::take_pipe(std::size_t index) noexcept {
    return index < pipes.size() ? std::exchange(pipes[index], fs::File::closed) : fs::File::closed;
}

std::expected<std::optional<ExitStatus>, std::error_code> Child::try_wait() {
    if(id < 0) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    if(auto error = reap(false)) {
        return std::unexpected(error);
    }
    return exited;
}

std::expected<ExitStatus, std::error_code> Child::wait() {
    if(id < 0) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    if(auto error = reap(true)) {
        return std::unexpected(error);
    }
    return *exited;
}

std::error_code Child::kill() {
    return kill(sigkill);
}

std::error_code Child::kill_tree() {
    return kill_tree(sigkill);
}

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
    if(file.empty()) {
        return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    }
#ifdef _WIN32
    // A separator or a drive (`C:tool`) makes it a path.
    const bool is_path =
        !fs::path::root(file).name.empty() || file.find_first_of(R"(/\)") != std::string_view::npos;
#else
    const bool is_path = file.find('/') != std::string_view::npos;
#endif
    // The child's directory, for what is relative: absolute, so that the
    // path means the same once the child is in it.
    std::string cwd;
    if(!options.cwd.empty()) {
        auto absolute = fs::absolute(options.cwd);
        if(!absolute) {
            return std::unexpected(absolute.error());
        }
        cwd = std::move(*absolute);
    }
    auto in_cwd = [&](std::string_view path) {
        return fs::path::join(cwd, path);
    };
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
#ifdef _WIN32
    auto directories = path ? split_paths(*path) : std::vector<std::string>();
#else
    auto directories = split_paths(path ? *path : default_path());
#endif
    for(auto& directory: directories) {
#ifdef _WIN32
        // An empty entry names no directory.
        if(directory.empty()) {
            continue;
        }
#endif
        // On POSIX an empty one is the current directory: the child's.
        directory = in_cwd(directory);
    }
#ifdef _WIN32
    return windows::find_create_process_program(file, directories);
#else
    return find_program(file, {.directories = std::move(directories)});
#endif
}

}  // namespace kota::sys
