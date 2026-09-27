/// dump_dot.cpp — Comprehensive dump_dot() demo covering every async node kind.
///
/// Constructs a graph that exercises all NodeKind variants at once:
///   Task, Mutex, Semaphore, Event, ConditionVariable,
///   Waiter, WhenAll, WhenAny, TaskGroup, SystemIO
///
/// Every sync primitive has multiple waiters queued so the waiter linked-list
/// is clearly visible in the rendered graph.
///
/// After 5 ms an observer task (buried in the middle of the tree) calls
/// dump_dot() on the root task, which renders the full graph.
///
/// Usage:
///   ./dump_dot > graph.dot
///   dot -Tpng graph.dot -o graph.png

#include <chrono>
#include <print>

#include "kota/async/async.h"

using namespace kota;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Shared state — sync primitives whose waiter queues we want visible in graph.
// ---------------------------------------------------------------------------
static mutex mtx;
static semaphore sem{0};  // starts empty → all acquirers block
static event evt{false};  // starts unset → all waiters block
static condition_variable cv;
static mutex cv_mtx;  // dedicated mutex for cv.wait()

// main() stores the root task here so the observer can call dump_dot() on it
// from the middle of the tree.
static task<>* root_task = nullptr;

// ---------------------------------------------------------------------------
// Leaf helpers — each one blocks on a different primitive.
// ---------------------------------------------------------------------------

/// Holds the mutex and sleeps → all contenders queue as Waiters.
task<> mtx_holder(event_loop& loop) {
    co_await mtx.lock();
    co_await sleep(100ms, loop);
    mtx.unlock();
}

/// Tries to lock the same mutex → blocks as a Waiter.
task<> mtx_contender(event_loop& loop) {
    co_await mtx.lock();
    co_await sleep(10ms, loop);
    mtx.unlock();
}

/// Blocks on the semaphore → produces a Waiter.
task<> sem_acquirer(event_loop& loop) {
    co_await sem.acquire();
    co_await sleep(10ms, loop);
}

/// Blocks on the event → produces a Waiter.
task<> evt_waiter(event_loop& loop) {
    co_await evt.wait();
    co_await sleep(10ms, loop);
}

/// Blocks on condition_variable.wait(cv_mtx) → produces a Waiter on cv.
/// Each cv_waiter needs its own lock/unlock cycle; cv_mtx is acquired then
/// released inside cv.wait(), so multiple waiters can enter sequentially
/// as long as each one gets the lock before the snapshot.
/// We use a tiny sleep to stagger them so they queue one by one.
task<> cv_waiter(event_loop& loop, int delay_ms) {
    co_await sleep(delay_ms, loop);
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
    if(root_task) {
        std::println("{}", dump_dot(*root_task));
    }
    // Keep alive until everything else finishes.
    co_await sleep(300ms, loop);
}

// ---------------------------------------------------------------------------
// Composite branches — exercise when_all, when_any, task_group.
// ---------------------------------------------------------------------------

/// when_all branch: 1 holder + 3 contenders → Mutex has 3 Waiters.
task<> branch_mutex(event_loop& loop) {
    co_await when_all(mtx_holder(loop),
                      mtx_contender(loop),
                      mtx_contender(loop),
                      mtx_contender(loop));
}

/// when_any branch: races slow_work vs fast_work. Loser gets cancelled.
task<std::variant<int, int>> branch_when_any(event_loop& loop) {
    co_return co_await when_any(slow_work(loop), fast_work(loop));
}

/// task_group branch: dynamically spawns tasks that block on sem / event / cv.
/// Multiple waiters per primitive to demonstrate the waiter linked-list.
task<> branch_task_group(event_loop& loop) {
    task_group<> group;

    // 3 acquirers on the semaphore (all block — count is 0).
    group.spawn(sem_acquirer(loop));
    group.spawn(sem_acquirer(loop));
    group.spawn(sem_acquirer(loop));

    // 3 waiters on the event (all block — event is unset).
    group.spawn(evt_waiter(loop));
    group.spawn(evt_waiter(loop));
    group.spawn(evt_waiter(loop));

    // 2 waiters on the condition variable.
    // Stagger by 1 ms so each one acquires cv_mtx in turn before the snapshot.
    group.spawn(cv_waiter(loop, 0));
    group.spawn(cv_waiter(loop, 1));

    co_await group.join();
}

/// Cancellation branch: a long-running task wrapped with a cancellation token.
task<> branch_cancel(event_loop& loop) {
    cancellation_source source;

    auto target = [&]() -> task<> {
        co_await with_token(slow_work(loop), source.token());
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
    //         │         ├─ mtx_contender ×3 (Task → Waiter)
    //         │         └─ Mutex (ellipse, 3 waiters linked)
    //         ├─ branch_when_any (Task)
    //         │    └─ WhenAny
    //         │         ├─ slow_work (Task → SystemIO)
    //         │         └─ fast_work (Task → SystemIO)
    //         ├─ branch_task_group (Task)
    //         │    └─ task_group (spawned tasks)
    //         │         ├─ sem_acquirer ×3 (Task → Waiter)
    //         │         │   └─ Semaphore (ellipse, 3 waiters linked)
    //         │         ├─ evt_waiter ×3 (Task → Waiter)
    //         │         │   └─ Event (ellipse, 3 waiters linked)
    //         │         └─ cv_waiter ×2 (Task → Waiter on cv)
    //         │             └─ ConditionVariable (ellipse, 2 waiters linked)
    //         └─ branch_cancel (Task)
    //              └─ WhenAll
    //                   ├─ target (Task → with_token → slow_work → SystemIO)
    //                   └─ canceler (Task → SystemIO)

    co_await when_all(std::move(obs),
                      releaser(),
                      branch_mutex(loop),
                      branch_when_any(loop),
                      branch_task_group(loop),
                      branch_cancel(loop));
}

int main() {
    event_loop loop;
    auto t = driver(loop);
    root_task = &t;
    loop.schedule(t);
    loop.run();
    return 0;
}
