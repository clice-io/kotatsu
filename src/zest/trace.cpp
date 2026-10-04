#include "kota/zest/assert/trace.h"

#include <algorithm>
#include <format>
#include <print>

#ifdef __cpp_exceptions
#include <cpptrace/from_current.hpp>
#if !defined(_MSC_VER) && __has_include(<cxxabi.h>)
#include <cxxabi.h>
#define ZEST_ITANIUM_EXCEPTIONS
#elif defined(_CPPRTTI)
#include <typeinfo>
#endif
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

/// The type of the exception being handled, which is `error` when that is
/// not null. The Itanium ABI knows the type of any exception; elsewhere only
/// RTTI names one, and only a std::exception.
std::string handled_type([[maybe_unused]] const std::exception* error) {
#ifdef ZEST_ITANIUM_EXCEPTIONS
    return cpptrace::demangle(abi::__cxa_current_exception_type()->name());
#else
#ifdef _CPPRTTI
    if(error != nullptr) {
        return typeid(*error).name();
    }
#endif
    return "an exception of unknown type";
#endif
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

bool trace_exception(function<void()> cb) {
    bool ret = false;

    CPPTRACE_TRY {
        CPPTRACE_TRY {
            cb();
        }
        CPPTRACE_CATCH(const std::exception& e) {
            std::println("[ exception ] {}", e.what());
            println_trace(cpptrace::from_current_exception());
            ret = true;
        }
    }
    CPPTRACE_CATCH(...) {
        std::println("[ exception ] <non-std exception>");
        println_trace(cpptrace::from_current_exception());
        ret = true;
    }
    return ret;
}

std::string describe_exception(std::exception_ptr exception) {
    try {
        std::rethrow_exception(exception);
    } catch(const std::exception& error) {
        return std::format("{}: {}", handled_type(&error), error.what());
    } catch(...) {
        return handled_type(nullptr);
    }
}

#endif

}  // namespace kota::zest
