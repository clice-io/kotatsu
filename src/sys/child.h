#pragma once

// What spawn does alike on every host: the child's environment, and the
// file it runs.

#include <expected>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "kota/sys/env.h"
#include "kota/sys/spawn.h"

namespace kota::sys {

/// The child's environment as `options` ask for it: `env`, or this
/// process's, with `env_changes` made in order; unset when the child is to
/// inherit this process's as it is.
std::optional<std::vector<EnvVar>> child_environment(const SpawnOptions& options);

/// The file the child runs: `options.file` as SpawnOptions::file says, PATH
/// read from `environment`, the child's (this process's when unset).
std::expected<std::string, std::error_code>
    child_program(const SpawnOptions& options,
                  const std::optional<std::vector<EnvVar>>& environment);

}  // namespace kota::sys
