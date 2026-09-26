// ZEST_SUITE(async_runtime_sync): core async synchronization primitives — mutex, event
// (set/wait, manual reset, interrupt), semaphore, condition_variable. Tests the
// happy-path ordering and signalling. The deferred sync-resume mechanism (grant
// abandonment on cancel, chained/same-tick deferred resumes) lives in
// sync_deferred_tests.cpp.
#include <chrono>

#include "async/harness/loop_fixture.h"
#include "kota/zest/zest.h"

namespace kota {

namespace {

using namespace std::chrono;

ZEST_SUITE(sync, loop_fixture) {

ZEST_CASE(mutex_try_lock) {
    mutex m;
    EXPECT(m.try_lock());
    EXPECT(!m.try_lock());
    m.unlock();
    EXPECT(m.try_lock());
    m.unlock();
}

ZEST_CASE(mutex_lock_order) {
    mutex m;
    int step = 0;

    auto holder = [&]() -> task<> {
        co_await m.lock();
        EXPECT(step == 0);
        step = 1;
        co_await sleep(milliseconds{5}, loop);
        m.unlock();
    };

    auto waiter = [&]() -> task<> {
        co_await sleep(milliseconds{1}, loop);
        co_await m.lock();
        EXPECT(step == 1);
        step = 2;
        m.unlock();
        loop.stop();
    };

    auto t1 = holder();
    auto t2 = waiter();
    schedule_all(t1, t2);

    EXPECT(step == 2);
}

ZEST_CASE(event_set_wait) {
    event ev;
    int fired = 0;

    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        fired = 1;
        loop.stop();
    };

    auto setter = [&]() -> task<> {
        co_await sleep(milliseconds{1}, loop);
        ev.set();
    };

    auto t1 = waiter();
    auto t2 = setter();
    schedule_all(t1, t2);

    EXPECT(fired == 1);
}

ZEST_CASE(manual_reset_all) {
    event ev(true);
    int count = 0;

    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        count += 1;
        if(count == 2) {
            loop.stop();
        }
    };

    auto t1 = waiter();
    auto t2 = waiter();
    schedule_all(t1, t2);

    EXPECT(count == 2);
}

ZEST_CASE(event_interrupt) {
    event ev;
    bool reached = false;

    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        reached = true;
    };

    auto driver = [&]() -> task<> {
        auto result = co_await waiter().catch_cancel();
        EXPECT(!result);
        EXPECT(!reached);
        loop.stop();
    };

    auto intr = [&]() -> task<> {
        co_await sleep(milliseconds{1}, loop);
        ev.interrupt();
    };

    auto t1 = driver();
    auto t2 = intr();
    schedule_all(t1, t2);
}

ZEST_CASE(interrupt_many) {
    event ev;
    int cancelled = 0;

    auto waiter = [&]() -> task<> {
        auto result = co_await ev.wait().catch_cancel();
        EXPECT(!result);
        cancelled += 1;
        if(cancelled == 2) {
            loop.stop();
        }
    };

    auto intr = [&]() -> task<> {
        co_await sleep(milliseconds{1}, loop);
        ev.interrupt();
    };

    auto t1 = waiter();
    auto t2 = waiter();
    auto t3 = intr();
    schedule_all(t1, t2, t3);

    EXPECT(cancelled == 2);
}

ZEST_CASE(interrupt_snapshot) {
    event ev;
    int cancelled = 0;
    bool second_wait_cancelled = false;

    auto waiter = [&]() -> task<> {
        auto first = co_await ev.wait().catch_cancel();
        EXPECT(!first);
        cancelled += 1;

        auto second = co_await ev.wait().catch_cancel();
        second_wait_cancelled = !second.has_value();
        loop.stop();
    };

    auto intr = [&]() -> task<> {
        co_await sleep(milliseconds{1}, loop);
        ev.interrupt();
    };

    auto setter = [&]() -> task<> {
        co_await sleep(milliseconds{2}, loop);
        ev.set();
    };

    auto t1 = waiter();
    auto t2 = intr();
    auto t3 = setter();
    schedule_all(t1, t2, t3);

    EXPECT(cancelled == 1);
    EXPECT(!second_wait_cancelled);
}

ZEST_CASE(future_wait) {
    event ev;
    bool fired = false;

    ev.interrupt();

    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        fired = true;
        loop.stop();
    };

    auto setter = [&]() -> task<> {
        co_await sleep(milliseconds{1}, loop);
        EXPECT(!fired);
        ev.set();
    };

    auto t1 = waiter();
    auto t2 = setter();
    schedule_all(t1, t2);

    EXPECT(fired);
}

ZEST_CASE(signal_state) {
    event ev;
    EXPECT(!ev.is_set());
    ev.interrupt();
    EXPECT(!ev.is_set());

    event set_ev(true);
    EXPECT(set_ev.is_set());
    set_ev.interrupt();
    EXPECT(set_ev.is_set());
}

ZEST_CASE(semaphore_acquire_release) {
    semaphore sem(1);
    int step = 0;

    auto first = [&]() -> task<> {
        co_await sem.acquire();
        step = 1;
        co_await sleep(milliseconds{5}, loop);
        sem.release();
    };

    auto second = [&]() -> task<> {
        co_await sleep(milliseconds{1}, loop);
        co_await sem.acquire();
        EXPECT(step == 1);
        step = 2;
        sem.release();
        loop.stop();
    };

    auto t1 = first();
    auto t2 = second();
    schedule_all(t1, t2);

    EXPECT(step == 2);
}

ZEST_CASE(condition_variable_wait) {
    mutex m;
    condition_variable cv;
    bool ready = false;
    int step = 0;

    auto waiter = [&]() -> task<> {
        co_await m.lock();
        step = 1;
        while(!ready) {
            co_await cv.wait(m);
        }
        step = 3;
        m.unlock();
        loop.stop();
    };

    auto notifier = [&]() -> task<> {
        co_await sleep(milliseconds{1}, loop);
        co_await m.lock();
        step = 2;
        ready = true;
        cv.notify_one();
        m.unlock();
    };

    auto t1 = waiter();
    auto t2 = notifier();
    schedule_all(t1, t2);

    EXPECT(step == 3);
}

};  // ZEST_SUITE(sync)

}  // namespace

}  // namespace kota
