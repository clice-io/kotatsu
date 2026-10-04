#pragma once

#include <exception>
#include <source_location>
#include <string>

#include <kota/support/functional.h>

namespace kota::zest {

void print_trace(std::source_location location = std::source_location::current());

#ifdef __cpp_exceptions

/// Runs `cb`; prints what it throws, with the stack it was thrown from, and
/// returns whether it threw.
bool trace_exception(function<void()> cb);

/// What `exception` is: its type, and its what() if it is a std::exception.
std::string describe_exception(std::exception_ptr exception);

#endif

}  // namespace kota::zest
