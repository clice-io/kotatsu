#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <unwind.h>
#endif

#include "fs/harness/files.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/support/config.h"
#include "kotatsu/support/scope_exit.h"
#include "kotatsu/fs/file.h"
#include "kotatsu/sys/process.h"
#include "kotatsu/sys/stacktrace.h"
#include "kotatsu/sys/thread.h"

namespace kotatsu::sys {

namespace {

/// A descriptor, closed before its temporary directory even on a failed ZASSERT.
struct Descriptor {
    explicit Descriptor(int fd) : fd(fd) {}

    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;

    ~Descriptor() {
        reset();
    }

    void reset() {
        if(fd >= 0) {
#ifdef _WIN32
            ::_close(fd);
#else
            ::close(fd);
#endif
            fd = -1;
        }
    }

    int fd;
    zest::FatalHook on_fatal{[this] { reset(); }};
};

struct Frame {
    std::uintptr_t address = 0;
    std::string module = {};
    std::uintptr_t base = 0;
};

/// `text` read as upper-case hexadecimal digits, all of it.
bool parse_hex(std::string_view text, std::uintptr_t& value) {
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return !text.empty() && error == std::errc() && end == text.data() + text.size() &&
           std::ranges::none_of(text, [](char c) { return c >= 'a' && c <= 'f'; });
}

/// `line` taken as write_stacktrace writes one: "0x<16 digits>, <path>(0x<16
/// digits>) + 0x<offset> byte(s)" or "0x<16 digits> <unknown module>", the
/// offset without leading zeros.
bool parse_frame(std::string_view line, Frame& frame) {
    if(line.size() < 18 || !line.starts_with("0x") ||
       !parse_hex(line.substr(2, 16), frame.address)) {
        return false;
    }
    auto rest = line.substr(18);
    if(rest == " <unknown module>") {
        return true;
    }
    constexpr std::string_view suffix = " byte(s)";
    auto plus = rest.rfind(") + 0x");
    if(!rest.starts_with(", ") || !rest.ends_with(suffix) || plus == std::string_view::npos ||
       plus <= 2 + 19 || rest.substr(plus - 19, 3) != "(0x") {
        return false;
    }
    auto offset = rest.substr(plus + 6, rest.size() - suffix.size() - plus - 6);
    std::uintptr_t value = 0;
    if(!parse_hex(rest.substr(plus - 16, 16), frame.base) || !parse_hex(offset, value) ||
       (offset.size() > 1 && offset.front() == '0') || value != frame.address - frame.base) {
        return false;
    }
    frame.module = std::string(rest.substr(2, plus - 19 - 2));
    return true;
}

/// A file, since a whole trace can fill a Windows pipe before it is read.
struct TraceFile {
    TraceFile() {
        auto file = fs::File::open(path, {.write = true, .create = true});
        ZASSERT(file);
        auto fd = std::move(*file).into_descriptor();
        ZASSERT(fd);
        descriptor.fd = *fd;
    }

    std::vector<Frame> read() {
        descriptor.reset();
        std::string text;
        ZASSERT(!fs::read_file(text, path));
        ZASSERT(!text.empty());
        ZASSERT(text.ends_with('\n'));
        std::vector<Frame> frames;
        std::string_view remaining = text;
        while(!remaining.empty()) {
            auto newline = remaining.find('\n');
            ZASSERT(newline != std::string_view::npos);
            auto line = remaining.substr(0, newline);
            ZEST_CONTEXT("frame {}: {}", frames.size(), line);
            ZEXPECT(!line.contains(" (interrupted)"));
            Frame frame;
            ZASSERT(parse_frame(line, frame));
            frames.push_back(std::move(frame));
            remaining.remove_prefix(newline + 1);
        }
        return frames;
    }

    test::TempDir dir;
    std::string path = dir.file("trace");
    Descriptor descriptor{-1};
};

KOTATSU_NOINLINE int write_deep_trace(int fd, int depth, std::error_code& error) {
    volatile int frame = depth;
    if(depth == 1) {
        error = write_stacktrace(fd);
        return frame;
    }
    auto below = write_deep_trace(fd, depth - 1, error);
    // The volatile read after the recursive call keeps it from becoming a tail call.
    return below + frame;
}

#if defined(__linux__) || defined(__APPLE__)
struct UnwindTrace {
    int fd = -1;
    std::error_code error = {};
    std::array<std::uintptr_t, 256> addresses{};
    std::size_t count = 0;
    bool captured = false;
};

UnwindTrace unwind_trace;

KOTATSU_NOINLINE void capture_both() {
    unwind_trace.error = write_stacktrace(unwind_trace.fd);
    ::_Unwind_Backtrace(
        [](_Unwind_Context* context, void*) {
            ZASSERT(unwind_trace.count < unwind_trace.addresses.size());
            unwind_trace.addresses[unwind_trace.count++] = ::_Unwind_GetIP(context);
            return _URC_NO_REASON;
        },
        nullptr);
    // Keep the _Unwind_Backtrace call in this frame, too.
    unwind_trace.captured = true;
}
#endif

// Ordinary calls, including a new thread and frames inside library sort routines.
ZEST_SUITE(sys_stacktrace) {

ZEST_CASE(write_stacktrace_to_a_negative_descriptor_fails) {
    ZEXPECT(write_stacktrace(-1) == std::errc::bad_file_descriptor);
}

ZEST_CASE(write_stacktrace_names_this_executable_and_ends) {
    TraceFile trace;
    ZASSERT(!write_stacktrace(trace.descriptor.fd));
    auto frames = trace.read();
    ZASSERT(!frames.empty());
    ZEXPECT(frames.size() < 256U);
    std::string executable;
    ZASSERT(!executable_path(executable));
    ZASSERT(!frames.front().module.empty());
    ZEXPECT(std::filesystem::equivalent(test::std_path(frames.front().module),
                                        test::std_path(executable)));
    ZEXPECT(frames.front().base == executable_base());
}

ZEST_CASE(write_stacktrace_on_a_thread_formats_frames_and_ends) {
    TraceFile trace;
    std::error_code error;
    auto thread = Thread::start(1U << 20, [&] { error = write_stacktrace(trace.descriptor.fd); });
    ZASSERT(thread);
    thread->join();
    ZASSERT(!error);
    auto frames = trace.read();
    ZEXPECT(!frames.empty());
    ZEXPECT(frames.size() < 256U);
}

ZEST_CASE(write_stacktrace_limits_a_deep_stack_to_256_frames) {
    TraceFile trace;
    std::error_code error;
    ZEXPECT(write_deep_trace(trace.descriptor.fd, 300, error) == 300 * 301 / 2);
    ZASSERT(!error);
    auto frames = trace.read();
    ZEXPECT(frames.size() == 256U);
}

#if defined(__linux__) || defined(__APPLE__)
ZEST_CASE(write_stacktrace_through_sort_comparators_matches_unwind) {
    TraceFile trace;
    auto previous = std::exchange(unwind_trace, UnwindTrace{.fd = trace.descriptor.fd});
    ScopeExit restore([previous] { unwind_trace = previous; });
    std::array outer{3, 1, 2};
    std::sort(outer.begin(), outer.end(), [](int left, int right) {
        if(!unwind_trace.captured) {
            std::array inner{2, 1};
            std::qsort(inner.data(),
                       inner.size(),
                       sizeof(int),
                       [](const void* one, const void* two) {
                           if(!unwind_trace.captured) {
                               capture_both();
                           }
                           auto first = *static_cast<const int*>(one);
                           auto second = *static_cast<const int*>(two);
                           return (first > second) - (first < second);
                       });
        }
        return left < right;
    });
    ZASSERT(unwind_trace.captured);
    ZASSERT(!unwind_trace.error);
    // Built without exceptions, a macOS program may have no unwind tables,
    // which _Unwind_Backtrace needs and frame pointers do not.
    if(unwind_trace.count == 0) {
        zest::skip();
        return;
    }
    auto frames = trace.read();
    ZASSERT(frames.size() > 1U);
    ZASSERT(unwind_trace.count > 1U);
    ZEXPECT(frames.size() < 256U);
    ZEXPECT(unwind_trace.count < unwind_trace.addresses.size());
    auto count = std::min(frames.size(), unwind_trace.count);
    // Linux stops before a zero return address, which _Unwind_Backtrace may report;
    // macOS stops when the next frame record leaves the stack, at the dyld boundary.
    // Allow at most two terminal frames' difference, comparing the common prefix.
    ZEXPECT(std::max(frames.size(), unwind_trace.count) - count <= 2U);
    // Frame zero returns to two different call sites in capture_both().
    for(std::size_t index = 1; index < count; ++index) {
        ZEST_CONTEXT("frame {}", index);
        ZEXPECT(frames[index].address == unwind_trace.addresses[index]);
    }
}
#endif

};  // ZEST_SUITE(sys_stacktrace)

}  // namespace

}  // namespace kotatsu::sys
