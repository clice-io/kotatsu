#include "kota/zest/assert/trace.h"

#include <algorithm>
#include <exception>
#include <format>
#include <print>
#include <string>

#ifdef __cpp_exceptions
#include <cpptrace/from_current.hpp>
#endif

#include "kota/support/functional.h"
#include <cpptrace/cpptrace.hpp>

namespace kota::zest {

namespace {

void println_trace(const cpptrace::stacktrace& trace) {
    for(const auto& frame: trace.frames) {
        std::println("{}", frame.to_string());
    }
}

#ifdef __cpp_exceptions

/// The message of the exception being handled; call only inside a handler.
std::string current_exception_message() {
    try {
        throw;
    } catch(const std::exception& e) {
        return e.what();
    } catch(...) {
        return "<non-std exception>";
    }
}

#endif

}  // namespace

void print_trace(std::source_location location) {
    auto trace = cpptrace::generate_trace();
    auto& frames = trace.frames;
    if(frames.size() > 1) {
        frames.erase(frames.begin());
    }
    auto it = std::ranges::find_if(frames, [&](const cpptrace::stacktrace_frame& frame) {
        return frame.filename != location.file_name();
    });
    if(it != frames.begin()) {
        frames.erase(it, frames.end());
    }
    println_trace(trace);
}

#ifdef __cpp_exceptions

bool trace_exception(function<void()> cb, bool print) {
    bool ret = false;

    // Catch everything here and take the message in a plain handler: under
    // clang-cl's ASan, a reference caught by CPPTRACE_CATCH, whose handler
    // sits inside nested lambdas, points into the stack instead of at the
    // exception.
    CPPTRACE_TRY {
        cb();
    }
    CPPTRACE_CATCH(...) {
        if(print) {
            std::println("[ exception ] {}", current_exception_message());
            println_trace(cpptrace::from_current_exception());
        }
        ret = true;
    }
    return ret;
}

#endif

}  // namespace kota::zest
