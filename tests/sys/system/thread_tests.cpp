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

ZEST_SUITE(sys_thread) {

ZEST_CASE(start_runs_the_function_on_a_stack_of_the_size_asked) {
    std::size_t size = 0;
    std::uint64_t sum = 0;
    auto thread = Thread::start(8 * mebibyte, [&] {
        size = own_stack_size();
        // 6 MiB of it.
        sum = deep(96);
    });
    ZASSERT(thread);
    thread->join();
    ZEXPECT(size >= 8 * mebibyte);
    ZEXPECT(sum == 96U * 97U / 2U);
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

// No system maps a stack of half the address space, and each says so with
// an error of its own.
ZEST_CASE(start_with_a_stack_too_large_fails) {
    bool ran = false;
    auto thread = Thread::start(std::numeric_limits<std::size_t>::max() / 2, [&] { ran = true; });
    ZASSERT(!thread);
    ZEXPECT(thread.error() != std::error_code());
    ZEXPECT(!ran);
}

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

ZEST_CASE(assignment_joins_the_thread_it_held) {
    std::atomic<int> ended = 0;
    auto first = Thread::start(mebibyte, [&] { ++ended; });
    auto second = Thread::start(mebibyte, [&] { ++ended; });
    ZASSERT(first);
    ZASSERT(second);
    *first = std::move(*second);
    // The first has ended, joined.
    ZEXPECT(ended.load() >= 1);
    first->join();
    ZEXPECT(ended.load() == 2);
}

// The latch is the thread's too, which may still be in count_down when the
// wait returns.
ZEST_CASE(a_detached_thread_runs_on) {
    auto ran = std::make_shared<std::latch>(1);
    auto thread = Thread::start(mebibyte, [ran] { ran->count_down(); });
    ZASSERT(thread);
    thread->detach();
    ran->wait();
}

};  // ZEST_SUITE(sys_thread)

}  // namespace

}  // namespace kotatsu::sys
