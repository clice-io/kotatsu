#include "kota/zest/detail/trace.h"

#include <algorithm>

#include "cpptrace/cpptrace.hpp"

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
