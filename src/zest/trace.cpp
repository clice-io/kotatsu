#include "kota/zest/detail/trace.h"

#include <algorithm>
#include <format>
#include <print>

#include "kota/support/functional.h"
#include <cpptrace/cpptrace.hpp>

namespace kota::zest {

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
    trace.print();
}

}  // namespace kota::zest

#ifdef __cpp_exceptions
#include <cpptrace/from_current.hpp>

namespace kota::zest {

bool trace_exception(function<void()> cb, bool print) {
    bool ret = false;

    CPPTRACE_TRY {
        CPPTRACE_TRY {
            cb();
        }
        CPPTRACE_CATCH(const std::exception& e) {
            if(print) {
                std::println("[ exception ] {}", e.what());
                cpptrace::from_current_exception().print();
            }
            ret = true;
        }
    }
    CPPTRACE_CATCH(...) {
        if(print) {
            std::println("[ exception ] <non-std exception>");
            cpptrace::from_current_exception().print();
        }
        ret = true;
    }
    return ret;
}

}  // namespace kota::zest
#endif
