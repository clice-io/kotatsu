#include <cstddef>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <algorithm>
#include <csignal>
#endif

#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/sys/crash.h"
#include "kotatsu/sys/thread.h"

namespace kotatsu::sys {

namespace {

constexpr std::size_t crash_stack_size = 64 * 1024;

/// Runs `fn` on a thread of its own and waits for it to end.
template <typename Fn>
void on_a_thread(Fn fn) {
    auto thread = Thread::start(1U << 20, fn);
    ZASSERT(thread);
    thread->join();
}

#ifndef _WIN32
stack_t alternate_stack() {
    stack_t stack = {};
    ::sigaltstack(nullptr, &stack);
    return stack;
}

/// The alternate stack a thread had as it ended, after reserve_crash_stack's
/// put its own back, and the one to set again then, which ASan, that set it
/// up, unmaps as the thread ends: a thread_local, which a thread destroys
/// after those it made later.
struct AtThreadEnd {
    stack_t* seen = nullptr;
    stack_t original = alternate_stack();

    AtThreadEnd() = default;
    AtThreadEnd(const AtThreadEnd&) = delete;
    AtThreadEnd& operator=(const AtThreadEnd&) = delete;

    ~AtThreadEnd() {
        if(seen != nullptr) {
            *seen = alternate_stack();
        }
        // macOS checks the size even of a stack it is to disable.
        original.ss_size = std::max<std::size_t>(original.ss_size, MINSIGSTKSZ);
        ::sigaltstack(&original, nullptr);
    }
};

thread_local AtThreadEnd at_thread_end;

/// Sets `room` as the calling thread's alternate stack.
void set_alternate_stack(std::vector<char>& room) {
    stack_t stack = {};
    stack.ss_sp = room.data();
    stack.ss_size = room.size();
    ZASSERT(::sigaltstack(&stack, nullptr) == 0);
}
#endif

// A thread's crash stack; on_crash itself, which sets the process's signal
// actions, is the integration tests'.
ZEST_SUITE(sys_crash) {

ZEST_CASE(reserve_crash_stack_gives_a_thread_its_room_once) {
    on_a_thread([] {
#ifdef _WIN32
        ZASSERT(!reserve_crash_stack());
        ULONG size = 0;
        ::SetThreadStackGuarantee(&size);
        ZEXPECT(size >= crash_stack_size);
        ZEXPECT(!reserve_crash_stack());
#else
        // Set up before the crash stack, to put ASan's back after it.
        [[maybe_unused]] auto& end = at_thread_end;
        ZASSERT(!reserve_crash_stack());
        auto first = alternate_stack();
        ZEXPECT(!(first.ss_flags & SS_DISABLE));
        ZEXPECT(first.ss_size >= crash_stack_size);
        ZEXPECT(!reserve_crash_stack());
        ZEXPECT(alternate_stack().ss_sp == first.ss_sp);
#endif
    });
}

#ifndef _WIN32
ZEST_CASE(reserve_crash_stack_keeps_a_larger_alternate_stack) {
    std::vector<char> room(2 * crash_stack_size);
    on_a_thread([&] {
        [[maybe_unused]] auto& end = at_thread_end;
        set_alternate_stack(room);
        ZASSERT(!reserve_crash_stack());
        ZEXPECT(alternate_stack().ss_sp == room.data());
    });
}

ZEST_CASE(reserve_crash_stack_puts_the_alternate_stack_back_as_the_thread_ends) {
    // Smaller than the crash stack, which takes its place.
    std::vector<char> room(crash_stack_size / 2);
    stack_t seen = {};
    on_a_thread([&] {
        at_thread_end.seen = &seen;
        set_alternate_stack(room);
        ZASSERT(!reserve_crash_stack());
        ZEXPECT(alternate_stack().ss_sp != room.data());
    });
    ZEXPECT(seen.ss_sp == room.data());
    ZEXPECT(!(seen.ss_flags & SS_DISABLE));
}
#endif

};  // ZEST_SUITE(sys_crash)

}  // namespace

}  // namespace kotatsu::sys
