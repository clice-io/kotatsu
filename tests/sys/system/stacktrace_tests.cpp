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
#include <csignal>
#include <pthread.h>
#include <unistd.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <unwind.h>
#endif

#include "fs/harness/files.h"
#include "sys/harness/context.h"
#include "sys/harness/signals.h"
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
    bool interrupted = false;
};

/// `text` read as upper-case hexadecimal digits, all of it.
bool parse_hex(std::string_view text, std::uintptr_t& value) {
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return !text.empty() && error == std::errc() && end == text.data() + text.size() &&
           std::ranges::none_of(text, [](char c) { return c >= 'a' && c <= 'f'; });
}

/// `line` taken as write_stacktrace writes one: "0x<16 digits>, <path>(0x<16
/// digits>) + 0x<offset> byte(s)" or "0x<16 digits> <unknown module>", the
/// offset without leading zeros, either followed by " (interrupted)" for the
/// address a signal interrupted.
bool parse_frame(std::string_view line, Frame& frame) {
    if(line.size() < 18 || !line.starts_with("0x") ||
       !parse_hex(line.substr(2, 16), frame.address)) {
        return false;
    }
    auto rest = line.substr(18);
    constexpr std::string_view marker = " (interrupted)";
    frame.interrupted = rest.ends_with(marker);
    if(frame.interrupted) {
        rest.remove_suffix(marker.size());
    }
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

    /// The frames written, of which `interrupted` are marked.
    std::vector<Frame> read(std::size_t interrupted = 0) {
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
            Frame frame;
            ZASSERT(parse_frame(line, frame));
            frames.push_back(std::move(frame));
            remaining.remove_prefix(newline + 1);
        }
        ZEXPECT(static_cast<std::size_t>(std::ranges::count(frames, true, &Frame::interrupted)) ==
                interrupted);
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

#ifndef _WIN32
/// What the handler of SIGTRAP below saw: a signal that sanitizers hand to
/// the handler at once, as a fault, rather than at a later call.
struct SignalTrace {
    int fd = -1;
    std::error_code error = {};
    /// The address the context says the signal interrupted.
    std::uintptr_t interrupted = 0;
    /// Where raise_trap returns to.
    std::uintptr_t raised_from = 0;
    bool handled = false;
};

SignalTrace signal_trace;

void trace_signal(int, siginfo_t*, void* context) {
    signal_trace.interrupted = test::interrupted_at(context);
    signal_trace.error = write_stacktrace(signal_trace.fd);
    signal_trace.handled = true;
}

KOTATSU_NOINLINE void raise_trap() {
    signal_trace.raised_from = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    ::pthread_kill(::pthread_self(), SIGTRAP);
}

/// Raises SIGTRAP on a thread of its own, its handler on an alternate stack
/// with `alternate`, and checks that the trace the handler wrote goes on
/// from the address the signal interrupted to raise_trap's caller.
void check_trace_through_a_signal(bool alternate) {
    TraceFile trace;
    auto previous = std::exchange(signal_trace, SignalTrace{.fd = trace.descriptor.fd});
    ScopeExit restore([previous] { signal_trace = previous; });
    test::ScopedSignalAction action(SIGTRAP, trace_signal, alternate ? SA_ONSTACK : 0);
    // Kept until the thread, which ends with it set, is joined.
    std::vector<char> room(alternate ? 64 * 1024 : 0);
    auto thread = Thread::start(1U << 20, [&] {
        if(alternate) {
            stack_t stack = {};
            stack.ss_sp = room.data();
            stack.ss_size = room.size();
            ZASSERT(::sigaltstack(&stack, nullptr) == 0);
        }
        raise_trap();
    });
    ZASSERT(thread);
    thread->join();
    ZASSERT(signal_trace.handled);
    ZASSERT(!signal_trace.error);
    auto frames = trace.read(1);
    auto marked = std::ranges::find(frames, true, &Frame::interrupted);
    ZASSERT(marked != frames.end());
    ZEXPECT(marked->address == signal_trace.interrupted);
    ZEXPECT(std::ranges::find(marked, frames.end(), signal_trace.raised_from, &Frame::address) !=
            frames.end());
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

#ifndef _WIN32
ZEST_CASE(write_stacktrace_in_a_signal_handler_goes_on_from_the_interrupted_address) {
    check_trace_through_a_signal(false);
}

ZEST_CASE(write_stacktrace_on_an_alternate_stack_goes_on_from_the_interrupted_address) {
    check_trace_through_a_signal(true);
}
#endif

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
