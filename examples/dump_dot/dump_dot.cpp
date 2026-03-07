/// dump_dot.cpp — Comprehensive dump_dot() demo covering every async node kind.
///
/// Constructs a graph that exercises all NodeKind variants at once:
///   Task, Mutex, Semaphore, Event, ConditionVariable,
///   MutexWaiter, EventWaiter, WhenAll, WhenAny, Scope, SystemIO
///
/// Every sync primitive has multiple waiters queued so the waiter linked-list
/// is clearly visible in the rendered graph.
///
/// After 5 ms an observer task (buried in the middle of the tree) calls
/// dump_dot(), which walks UP to the root and then renders the full graph.
///
/// Usage:
///   ./eventide_example_dump_dot > graph.dot
///   dot -Tpng graph.dot -o graph.png

#include <chrono>
#include <print>

#include "eventide/async/cancellation.h"
#include "eventide/async/loop.h"
#include "eventide/async/sync.h"
#include "eventide/async/task.h"
#include "eventide/async/watcher.h"
#include "eventide/async/when.h"

using namespace eventide;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Shared state — sync primitives whose waiter queues we want visible in graph.
// ---------------------------------------------------------------------------
static mutex mtx;
static semaphore sem{0};  // starts empty → all acquirers block
static event evt{false};  // starts unset → all waiters block
static condition_variable cv;
static mutex cv_mtx;  // dedicated mutex for cv.wait()

// The observer stores its own node pointer here so it can call dump_dot()
// from the middle of the tree.
static async_node* observer_node = nullptr;

// ---------------------------------------------------------------------------
// Leaf helpers — each one blocks on a different primitive.
// ---------------------------------------------------------------------------

/// Holds the mutex and sleeps → all contenders queue as MutexWaiter.
task<> mtx_holder(event_loop& loop) {
    co_await mtx.lock();
    co_await sleep(100ms, loop);
    mtx.unlock();
}

/// Tries to lock the same mutex → blocks as MutexWaiter.
task<> mtx_contender(event_loop& loop) {
    co_await mtx.lock();
    co_await sleep(10ms, loop);
    mtx.unlock();
}

/// Blocks on the semaphore → produces an EventWaiter.
task<> sem_acquirer(event_loop& loop) {
    co_await sem.acquire();
    co_await sleep(10ms, loop);
}

/// Blocks on the event → produces an EventWaiter.
task<> evt_waiter(event_loop& loop) {
    co_await evt.wait();
    co_await sleep(10ms, loop);
}

/// Blocks on condition_variable.wait(cv_mtx) → produces an EventWaiter on cv.
/// Each cv_waiter needs its own lock/unlock cycle; cv_mtx is acquired then
/// released inside cv.wait(), so multiple waiters can enter sequentially
/// as long as each one gets the lock before the snapshot.
/// We use a tiny sleep to stagger them so they queue one by one.
task<> cv_waiter(event_loop& loop, int delay_us) {
    co_await sleep(std::chrono::milliseconds{delay_us}, loop);
    co_await cv_mtx.lock();
    co_await cv.wait(cv_mtx);
    cv_mtx.unlock();
}

/// A cancellable long sleep — will be cancelled by `when_any`.
task<int> slow_work(event_loop& loop) {
    co_await sleep(200ms, loop);
    co_return 1;
}

/// Fast work — wins the `when_any` race.
task<int> fast_work(event_loop& loop) {
    co_await sleep(100ms, loop);
    co_return 2;
}

// ---------------------------------------------------------------------------
// Observer — dumps the graph from the middle of the tree after 5 ms.
// ---------------------------------------------------------------------------

task<> observer(event_loop& loop) {
    co_await sleep(5ms, loop);
    if(observer_node) {
        std::println("{}", observer_node->dump_dot());
    }
    // Keep alive until everything else finishes.
    co_await sleep(300ms, loop);
}

// ---------------------------------------------------------------------------
// Composite branches — exercise when_all, when_any, async_scope.
// ---------------------------------------------------------------------------

/// when_all branch: 1 holder + 3 contenders → Mutex has 3 MutexWaiters.
task<> branch_mutex(event_loop& loop) {
    co_await when_all(mtx_holder(loop),
                      mtx_contender(loop),
                      mtx_contender(loop),
                      mtx_contender(loop));
}

/// when_any branch: races slow_work vs fast_work. Loser gets cancelled.
task<std::size_t> branch_when_any(event_loop& loop) {
    co_return co_await when_any(slow_work(loop), fast_work(loop));
}

/// async_scope branch: dynamically spawns tasks that block on sem / event / cv.
/// Multiple waiters per primitive to demonstrate the waiter linked-list.
task<> branch_scope(event_loop& loop) {
    async_scope scope;

    // 3 acquirers on the semaphore (all block — count is 0).
    scope.spawn(sem_acquirer(loop));
    scope.spawn(sem_acquirer(loop));
    scope.spawn(sem_acquirer(loop));

    // 3 waiters on the event (all block — event is unset).
    scope.spawn(evt_waiter(loop));
    scope.spawn(evt_waiter(loop));
    scope.spawn(evt_waiter(loop));

    // 2 waiters on the condition variable.
    // Stagger by 1 ms so each one acquires cv_mtx in turn before the snapshot.
    scope.spawn(cv_waiter(loop, 0));
    scope.spawn(cv_waiter(loop, 1));

    co_await scope;
}

/// Cancellation branch: a long-running task wrapped with a cancellation token.
task<> branch_cancel(event_loop& loop) {
    cancellation_source source;

    auto target = [&]() -> task<> {
        co_await with_token(source.token(), slow_work(loop));
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(50ms, loop);
        source.cancel();
    };

    co_await when_all(target(), canceler());
}

// ---------------------------------------------------------------------------
// Root driver — ties everything together.
// ---------------------------------------------------------------------------

task<> driver(event_loop& loop) {
    auto obs = observer(loop);
    observer_node = obs.operator->();

    // Release blocked primitives after the snapshot.
    auto releaser = [&]() -> task<> {
        co_await sleep(10ms, loop);  // wait for snapshot
        sem.release(3);              // unblock all 3 sem_acquirers
        evt.set();                   // unblock all 3 evt_waiters
        cv.notify_all();             // unblock all 2 cv_waiters
    };

    //  Full tree (at snapshot time):
    //
    //  driver (Task)
    //    └─ when_all (WhenAll)
    //         ├─ observer (Task) ← dump_dot() fires here
    //         ├─ releaser (Task → SystemIO)
    //         ├─ branch_mutex (Task)
    //         │    └─ WhenAll
    //         │         ├─ mtx_holder (Task → SystemIO)  [holds mutex]
    //         │         ├─ mtx_contender ×3 (Task → MutexWaiter)
    //         │         └─ Mutex (ellipse, 3 waiters linked)
    //         ├─ branch_when_any (Task)
    //         │    └─ WhenAny
    //         │         ├─ slow_work (Task → SystemIO)
    //         │         └─ fast_work (Task → SystemIO)
    //         ├─ branch_scope (Task)
    //         │    └─ Scope
    //         │         ├─ sem_acquirer ×3 (Task → EventWaiter)
    //         │         │   └─ Semaphore (ellipse, 3 waiters linked)
    //         │         ├─ evt_waiter ×3 (Task → EventWaiter)
    //         │         │   └─ Event (ellipse, 3 waiters linked)
    //         │         └─ cv_waiter ×2 (Task → EventWaiter on cv)
    //         │             └─ ConditionVariable (ellipse, 2 waiters linked)
    //         └─ branch_cancel (Task)
    //              └─ WhenAll
    //                   ├─ target (Task → with_token → slow_work → SystemIO)
    //                   └─ canceler (Task → SystemIO)

    co_await when_all(std::move(obs),
                      releaser(),
                      branch_mutex(loop),
                      branch_when_any(loop),
                      branch_scope(loop),
                      branch_cancel(loop));
}

int main() {
    event_loop loop;
    auto t = driver(loop);
    loop.schedule(t);
    loop.run();
    return 0;
}
