#include "kota/zest/assert/trace.h"

#include <algorithm>
#include <format>
#include <print>

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

    CPPTRACE_TRY {
        CPPTRACE_TRY {
            cb();
        }
        CPPTRACE_CATCH(const std::exception& e) {
            if(print) {
                std::println("[ exception ] {}", e.what());
                println_trace(cpptrace::from_current_exception());
            }
            ret = true;
        }
    }
    CPPTRACE_CATCH(...) {
        if(print) {
            std::println("[ exception ] <non-std exception>");
            println_trace(cpptrace::from_current_exception());
        }
        ret = true;
    }
    return ret;
}

#endif

}  // namespace kota::zest
