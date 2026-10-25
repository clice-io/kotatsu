#pragma once

// What spawn does alike on every host: the child's environment, its
// arguments, and the file it runs.

#include <csignal>
#include <expected>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "kota/sys/env.h"
#include "kota/sys/spawn.h"

namespace kota::sys {

#ifdef _WIN32
/// SIGKILL, which Windows's C runtime does not name: kill and kill_tree take
/// it there too.
constexpr inline int sigkill = 9;
#else
inline constexpr int sigkill = SIGKILL;
#endif

/// The child's environment as `options` ask for it: `env`, or this
/// process's, with `env_changes` made in order; unset when the child is to
/// inherit this process's as it is.
std::optional<std::vector<EnvVar>> child_environment(const SpawnOptions& options);

/// Whether a string `options` gives the child holds a NUL, which no system
/// call takes in one: the child would get the text before it. On Windows
/// windows_command_line too.
bool holds_nul(const SpawnOptions& options);

/// The child's argv: `arg0`, or `program`, then `args`.
std::vector<std::string> child_arguments(const SpawnOptions& options);

/// The file the child runs: `options.program` as SpawnOptions::program says, PATH
/// read from `environment`, the child's (this process's when unset). A path
/// relative to `options.cwd` is made absolute, for a child that changes to
/// that directory before it runs the file.
std::expected<std::string, std::error_code>
    child_program(const SpawnOptions& options,
                  const std::optional<std::vector<EnvVar>>& environment);

}  // namespace kota::sys
