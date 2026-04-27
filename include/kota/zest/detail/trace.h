#pragma once

#include <source_location>

#include <kota/support/functional.h>

namespace kota::zest {

void print_trace(std::source_location location = std::source_location::current());
#ifdef __cpp_exceptions
bool trace_exception(function<void()> cb, bool print);
#endif

}  // namespace kota::zest
