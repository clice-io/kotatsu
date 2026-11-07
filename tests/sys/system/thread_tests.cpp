#include <atomic>
#include <cstddef>
#include <cstdint>
#include <latch>
#include <limits>
#include <memory>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/support/config.h"
#include "kotatsu/sys/thread.h"

namespace kotatsu::sys {

namespace {

constexpr std::size_t mebibyte = std::size_t(1) << 20;

/// The size of the calling thread's stack, its reserve on Windows.
std::size_t own_stack_size() {
#ifdef _WIN32
    ULONG_PTR low = 0;
    ULONG_PTR high = 0;
    ::GetCurrentThreadStackLimits(&low, &high);
    return high - low;
#elif defined(__APPLE__)
    return ::pthread_get_stacksize_np(::pthread_self());
#else
    pthread_attr_t attributes;
    ::pthread_getattr_np(::pthread_self(), &attributes);
    std::size_t size = 0;
    ::pthread_attr_getstacksize(&attributes, &size);
    ::pthread_attr_destroy(&attributes);
    return size;
#endif
}

/// Takes `depth` frames of 64 KiB each from the stack, more than the
/// default stack of a thread holds on macOS (512 KiB) and Windows (1 MiB);
/// their bytes' sum.
KOTATSU_NOINLINE std::uint64_t deep(int depth) {
    volatile unsigned char frame[64 * 1024];
    for(std::size_t i = 0; i < sizeof(frame); i += 4096) {
        frame[i] = static_cast<unsigned char>(depth);
    }
    // After the call, so that it is no tail call.
    std::uint64_t below = depth > 1 ? deep(depth - 1) : 0;
    return below + frame[0];
}

/// What a detached thread leaves, which it still holds while it counts its
/// latch down.
struct Left {
    std::latch done{1};

    int value = 0;
};

ZEST_SUITE(sys_thread) {

// Twice Linux's default of 8 MiB, which a size ignored would give.
ZEST_CASE(start_runs_the_function_on_a_stack_of_the_size_asked) {
    std::size_t size = 0;
    std::uint64_t sum = 0;
    auto thread = Thread::start(16 * mebibyte, [&] {
        size = own_stack_size();
        // 12 MiB of it.
        sum = deep(192);
    });
    ZASSERT(thread);
    thread->join();
    ZEXPECT(size >= 16 * mebibyte);
    ZEXPECT(sum == 192U * 193U / 2U);
}

ZEST_CASE(start_takes_a_size_below_the_least_and_off_a_page) {
    bool ran = false;
    auto thread = Thread::start(1000, [&] { ran = true; });
    ZASSERT(thread);
    thread->join();
    ZEXPECT(ran);
}

// A move-only function, which std::function would not take.
ZEST_CASE(start_takes_a_function_that_only_moves) {
    auto value = std::make_unique<int>(7);
    int seen = 0;
    auto thread = Thread::start(mebibyte, [&seen, value = std::move(value)] { seen = *value; });
    ZASSERT(thread);
    thread->join();
    ZEXPECT(seen == 7);
}

// No system maps a stack of half the address space; the largest size
// rounds up to 0.
ZEST_CASE(start_with_a_stack_too_large_fails) {
    constexpr auto largest = std::numeric_limits<std::size_t>::max();
    for(auto size: {largest / 2, largest}) {
        ZEST_CONTEXT("{} bytes", size);
        bool ran = false;
        auto thread = Thread::start(size, [&] { ran = true; });
        ZASSERT(!thread);
        auto error = thread.error();
        ZEXPECT((error == std::errc::resource_unavailable_try_again ||
                 error == std::errc::not_enough_memory || error == std::errc::invalid_argument));
        ZEXPECT(!ran);
    }
}

#if KOTATSU_ENABLE_EXCEPTIONS
ZEST_CASE(an_exception_that_leaves_the_function_ends_the_process, crashes = true) {
    auto thread = Thread::start(mebibyte, [] { throw 1; });
    ZASSERT(thread);
    thread->join();
}
#endif

ZEST_CASE(destruction_joins_the_thread) {
    std::atomic<bool> ended = false;
    {
        auto thread = Thread::start(mebibyte, [&] { ended = true; });
        ZASSERT(thread);
    }
    ZEXPECT(ended.load());
}

ZEST_CASE(a_moved_thread_is_joined_by_its_new_owner) {
    std::atomic<bool> ended = false;
    auto thread = Thread::start(mebibyte, [&] { ended = true; });
    ZASSERT(thread);
    Thread moved(std::move(*thread));
    moved.join();
    ZEXPECT(ended.load());
}

// The second waits until the first is joined.
ZEST_CASE(assignment_joins_the_thread_it_held) {
    std::atomic<bool> first_ended = false;
    std::atomic<bool> second_ended = false;
    std::latch go(1);
    auto first = Thread::start(mebibyte, [&] { first_ended = true; });
    auto second = Thread::start(mebibyte, [&] {
        go.wait();
        second_ended = true;
    });
    ZASSERT(first);
    ZASSERT(second);
    *first = std::move(*second);
    ZEXPECT(first_ended.load());
    ZEXPECT(!second_ended.load());
    go.count_down();
    first->join();
    ZEXPECT(second_ended.load());
}

ZEST_CASE(a_detached_thread_runs_on) {
    auto left = std::make_shared<Left>();
    auto thread = Thread::start(mebibyte, [left] {
        left->value = 7;
        left->done.count_down();
    });
    ZASSERT(thread);
    thread->detach();
    left->done.wait();
    ZEXPECT(left->value == 7);
}

};  // ZEST_SUITE(sys_thread)

}  // namespace

}  // namespace kotatsu::sys
