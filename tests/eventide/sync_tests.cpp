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
