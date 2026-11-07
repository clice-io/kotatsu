// set_thread_pool_size() sizes libuv's thread pool, over what
// UV_THREADPOOL_SIZE says, and starts it at once, putting the variable back
// as it was, set or not. A call once the pool has started takes any size and
// changes nothing. Four functions queued on a pool of two meet two at a
// time: each waits for the others to arrive, for a while, so that four would
// meet on a pool of four. A program of its own: the pool is the process's,
// started once, and a test binary's workers each run many cases.

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

#include "kotatsu/sys/env.h"
#include "kotatsu/async/async.h"

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

/// How long a function waits for the others to arrive: far longer than a
/// pool with a thread for each takes to start them all.
constexpr auto grace = std::chrono::seconds(2);

std::mutex mutex;
std::condition_variable arrivals;

/// The functions that have arrived, those running now, and the most that ran
/// at once.
int arrived = 0;
int running = 0;
int peak = 0;

void meet() {
    std::unique_lock lock(mutex);
    ++arrived;
    peak = std::max(peak, ++running);
    arrivals.notify_all();
    arrivals.wait_for(lock, grace, [] { return arrived == 4; });
    --running;
}

}  // namespace

int main() {
    constexpr std::string_view variable = "UV_THREADPOOL_SIZE";
    check(!sys::set_env(variable, "3"), "UV_THREADPOOL_SIZE could not be set");

    check(async::set_thread_pool_size(0) == async::error::invalid_argument,
          "a pool of no threads was taken");
    check(async::set_thread_pool_size(1025) == async::error::invalid_argument,
          "a pool of 1025 threads was taken");
    check(!async::set_thread_pool_size(2), "a pool of 2 threads was refused");

    std::string after;
    check(sys::get_env(after, variable), "UV_THREADPOOL_SIZE is unset after the call, not \"3\"");
    check(after == "3",
          std::format("UV_THREADPOOL_SIZE is \"{}\" after the call, not \"3\"", after));

    check(!sys::unset_env(variable), "UV_THREADPOOL_SIZE could not be unset");
    check(!async::set_thread_pool_size(1), "a pool of 1 thread was refused");
    check(!async::set_thread_pool_size(1024), "a pool of 1024 threads was refused");
    after.clear();
    check(!sys::get_env(after, variable),
          std::format("UV_THREADPOOL_SIZE is \"{}\" after the calls, not unset", after));

    auto queue_four = []() -> async::task<> {
        co_await async::when_all(async::queue(meet),
                                 async::queue(meet),
                                 async::queue(meet),
                                 async::queue(meet));
    };
    async::run(queue_four());
    check(arrived == 4, std::format("{} of 4 functions ran", arrived));
    check(peak == 2, std::format("{} functions ran at once on a pool of 2", peak));
    return failures == 0 ? 0 : 1;
}
