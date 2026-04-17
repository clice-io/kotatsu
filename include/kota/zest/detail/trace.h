#pragma once

#include <source_location>

namespace kota::zest {

void print_trace(std::source_location location = std::source_location::current());

}  // namespace kota::zest
