// A relay let go on another thread, the loop's last hold, while the loop
// winds down: the loop must not return from run(), and go, before the relay
// is done with it. The linker's --wrap puts a wrapper before uv_async_send,
// which holds the relay's thread at the wakeup it sends as it lets go, where
// a preemption could, and catches a loop that goes meanwhile. A program of
// its own, since the wrapper replaces a function every test binary shares;
// built where the linker wraps (ELF).

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <semaphore>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "kotatsu/async/io/loop.h"

struct uv_async_s;

extern "C" int __real_uv_async_send(uv_async_s* handle);

namespace {

using kotatsu::async::event_loop;
using kotatsu::async::relay;

int failures = 0;

/// Whether `passed`; prints `what` when not.
bool check(bool passed, std::string_view what) {
    if(!passed) {
        std::fprintf(stderr, "failed: %s\n", std::string(what).c_str());
        ++failures;
    }
    return passed;
}

/// The relay's thread holds its next uv_async_send.
thread_local bool hold_next_send = false;

/// The held send has begun.
std::binary_semaphore holding{0};

std::mutex mutex;
std::condition_variable changed;
bool loop_gone = false;

/// How long the held send waits for the loop to go, which it must not: far
/// longer than a loop free to go takes.
constexpr auto grace = std::chrono::milliseconds(500);

/// Lets go of a relay of a loop on another thread, through `let_go`, while
/// the loop runs a callback the relay sent: the callback returns only once
/// the relay's last wakeup is under way, and the loop, then seeing no relay,
/// winds down.
template <typename LetGo>
void let_go_while_the_loop_winds_down(LetGo let_go) {
    loop_gone = false;
    std::thread worker;
    {
        event_loop loop;
        worker = std::thread([held = loop.create_relay(), let_go]() mutable {
            held.send([] { holding.acquire(); });
            hold_next_send = true;
            let_go(held);
        });
        loop.run();
    }
    {
        std::lock_guard lock(mutex);
        loop_gone = true;
    }
    changed.notify_all();
    worker.join();
}

}  // namespace

extern "C" int __wrap_uv_async_send(uv_async_s* handle) {
    if(!std::exchange(hold_next_send, false)) {
        return __real_uv_async_send(handle);
    }
    holding.release();
    std::unique_lock lock(mutex);
    // A wait to show that something does not happen: the loop does not go.
    if(!check(!changed.wait_for(lock, grace, [] { return loop_gone; }),
              "the loop went before the relay's last wakeup")) {
        // The handle went with the loop.
        return 0;
    }
    return __real_uv_async_send(handle);
}

int main() {
    let_go_while_the_loop_winds_down([](relay& held) { relay gone = std::move(held); });
    let_go_while_the_loop_winds_down([](relay& held) { held = relay(); });
    return failures == 0 ? 0 : 1;
}
