#pragma once

#include <functional>
#include <source_location>
#include <string>
#include <string_view>

namespace kota::zest {

void reset_snapshot_context(std::string_view suite, std::string_view test, std::string_view file);

void set_update_snapshots(bool enabled);

bool check_snapshot(std::string_view value,
                    std::string_view name = {},
                    std::source_location loc = std::source_location::current());

bool check_snapshot_glob(std::string_view pattern,
                         const std::function<std::string(std::string_view)>& transform,
                         std::source_location loc = std::source_location::current());

}  // namespace kota::zest
