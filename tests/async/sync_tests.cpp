#include <chrono>

#include "eventide/zest/zest.h"
#include "eventide/async/loop.h"
#include "eventide/async/sync.h"
#include "eventide/async/watcher.h"

namespace eventide {

namespace {

using namespace std::chrono;

TEST_SUITE(sync) {

TEST_CASE(mutex_try_lock) {
    mutex m;
    EXPECT_TRUE(m.try_lock());
    EXPECT_FALSE(m.try_lock());
    m.unlock();
    EXPECT_TRUE(m.try_lock());
    m.unlock();
}

TEST_CASE(mutex_lock_order) {
    event_loop loop;
    mutex m;
    int step = 0;

    auto holder = [&]() -> task<> {
        co_await m.lock();
        EXPECT_EQ(step, 0);
        step = 1;
        co_await sleep(milliseconds{5}, loop);
        m.unlock();
    };

    auto waiter = [&]() -> task<> {
        co_await sleep(milliseconds{1}, loop);
        co_await m.lock();
        EXPECT_EQ(step, 1);
        step = 2;
        m.unlock();
        loop.stop();
    };

    auto t1 = holder();
    auto t2 = waiter();
    loop.schedule(t1);
    loop.schedule(t2);
    loop.run();

    EXPECT_EQ(step, 2);
}

TEST_CASE(event_set_wait) {
    event_loop loop;
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
    loop.schedule(t1);
    loop.schedule(t2);
    loop.run();

    EXPECT_EQ(fired, 1);
}

TEST_CASE(manual_reset_all) {
    event_loop loop;
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
    loop.schedule(t1);
    loop.schedule(t2);
    loop.run();

    EXPECT_EQ(count, 2);
}

TEST_CASE(event_interrupt) {
    event_loop loop;
    event ev;
    bool reached = false;

    auto waiter = [&]() -> task<> {
        co_await ev.wait();
        reached = true;
    };

    auto driver = [&]() -> task<> {
        auto result = co_await waiter().catch_cancel();
        EXPECT_FALSE(result.has_value());
        EXPECT_FALSE(reached);
        loop.stop();
    };

    auto intr = [&]() -> task<> {
        co_await sleep(milliseconds{1}, loop);
        ev.interrupt();
    };

    auto t1 = driver();
    auto t2 = intr();
    loop.schedule(t1);
    loop.schedule(t2);
    loop.run();
}

TEST_CASE(interrupt_many) {
    event_loop loop;
    event ev;
    int cancelled = 0;

    auto waiter = [&]() -> task<> {
        auto result = co_await ev.wait().catch_cancel();
        EXPECT_FALSE(result.has_value());
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
    loop.schedule(t1);
    loop.schedule(t2);
    loop.schedule(t3);
    loop.run();

    EXPECT_EQ(cancelled, 2);
}

TEST_CASE(interrupt_snapshot) {
    event_loop loop;
    event ev;
    int cancelled = 0;
    bool second_wait_cancelled = false;

    auto waiter = [&]() -> task<> {
        auto first = co_await ev.wait().catch_cancel();
        EXPECT_FALSE(first.has_value());
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
    loop.schedule(t1);
    loop.schedule(t2);
    loop.schedule(t3);
    loop.run();

    EXPECT_EQ(cancelled, 1);
    EXPECT_FALSE(second_wait_cancelled);
}

TEST_CASE(future_wait) {
    event_loop loop;
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
        EXPECT_FALSE(fired);
        ev.set();
    };

    auto t1 = waiter();
    auto t2 = setter();
    loop.schedule(t1);
    loop.schedule(t2);
    loop.run();

    EXPECT_TRUE(fired);
}

TEST_CASE(signal_state) {
    event ev;
    EXPECT_FALSE(ev.is_set());
    ev.interrupt();
    EXPECT_FALSE(ev.is_set());

    event set_ev(true);
    EXPECT_TRUE(set_ev.is_set());
    set_ev.interrupt();
    EXPECT_TRUE(set_ev.is_set());
}

TEST_CASE(semaphore_acquire_release) {
    event_loop loop;
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
        EXPECT_EQ(step, 1);
        step = 2;
        sem.release();
        loop.stop();
    };

    auto t1 = first();
    auto t2 = second();
    loop.schedule(t1);
    loop.schedule(t2);
    loop.run();

    EXPECT_EQ(step, 2);
}

TEST_CASE(condition_variable_wait) {
    event_loop loop;
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
    loop.schedule(t1);
    loop.schedule(t2);
    loop.run();

    EXPECT_EQ(step, 3);
}

};  // TEST_SUITE(sync)

}  // namespace

}  // namespace eventide
