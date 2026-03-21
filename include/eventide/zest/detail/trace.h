#pragma once

#include <source_location>

namespace eventide::zest {

void print_trace(std::source_location location = std::source_location::current());

}  // namespace eventide::zest
