// Once on_crash is set up, a function async::queue() runs has a crash stack
// on its thread of libuv's pool, which kotatsu does not start, as the thread
// that set it up has: on Linux and macOS an alternate signal stack of 64 KiB
// at least, on Windows a stack guarantee as large. The pool starts before
// on_crash. A program of its own: on_crash and the pool are the process's.

#include <cstddef>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

#include "kotatsu/sys/crash.h"
#include "kotatsu/async/async.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#endif

namespace {

namespace async = kotatsu::async;
namespace sys = kotatsu::sys;

int failures = 0;

/// Prints `what` unless `passed`.
void check(bool passed, std::string_view what) {
    if(!passed) {
        std::fprintf(stderr, "failed: %s\n", std::string(what).c_str());
        ++failures;
    }
}

/// The room the calling thread has for a crash handler after its stack
/// overflowed.
std::size_t crash_stack() {
#ifdef _WIN32
    // 0 asks for the guarantee the thread has.
    ULONG size = 0;
    ::SetThreadStackGuarantee(&size);
    return size;
#else
    stack_t stack = {};
    ::sigaltstack(nullptr, &stack);
    return (stack.ss_flags & SS_DISABLE) ? 0 : stack.ss_size;
#endif
}

void reported(const sys::Crash&) noexcept {}

}  // namespace

int main() {
    check(!async::set_thread_pool_size(1), "a pool of 1 thread was refused");
    check(!sys::on_crash(reported), "on_crash failed");
    check(crash_stack() >= 64 * 1024,
          std::format("the main thread has {} bytes for a crash handler", crash_stack()));
    std::size_t pooled = 0;
    auto queued = [&]() -> async::task<> {
        co_await async::queue([&] { pooled = crash_stack(); });
    };
    async::run(queued());
    check(pooled >= 64 * 1024,
          std::format("the pool's thread has {} bytes for a crash handler", pooled));
    return failures == 0 ? 0 : 1;
}
