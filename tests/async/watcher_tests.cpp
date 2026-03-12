#include <chrono>

#include "eventide/zest/zest.h"
#include "eventide/async/loop.h"
#include "eventide/async/watcher.h"

namespace eventide {

namespace {

task<> wait_timer(timer& t) {
    co_await t.wait();
    event_loop::current().stop();
    co_return;
}

task<> wait_idle(idle& w) {
    co_await w.wait();
    event_loop::current().stop();
    co_return;
}

task<> wait_sleep(event_loop& loop) {
    co_await sleep(std::chrono::milliseconds{1}, loop);
    event_loop::current().stop();
    co_return;
}

task<> wait_prepare(prepare& w) {
    co_await w.wait();
    event_loop::current().stop();
    co_return;
}

task<> wait_check(check& w) {
    co_await w.wait();
    event_loop::current().stop();
    co_return;
}

task<> wait_timer_twice(timer& t) {
    co_await t.wait();
    co_await t.wait();
    t.stop();
    event_loop::current().stop();
    co_return;
}

}  // namespace

TEST_SUITE(watcher_io) {

TEST_CASE(timer_wait) {
    event_loop loop;

    auto t = timer::create(loop);
    t.start(std::chrono::milliseconds{1}, std::chrono::milliseconds{0});

    auto waiter = wait_timer(t);
    loop.schedule(waiter);
    loop.run();
}

TEST_CASE(idle_wait) {
    event_loop loop;

    auto w = idle::create(loop);
    w.start();

    auto waiter = wait_idle(w);
    loop.schedule(waiter);
    loop.run();

    w.stop();
}

TEST_CASE(sleep_once) {
    event_loop loop;

    auto sleeper = wait_sleep(loop);
    loop.schedule(sleeper);
    loop.run();
}

TEST_CASE(timer_repeat_twice) {
    event_loop loop;

    auto t = timer::create(loop);
    t.start(std::chrono::milliseconds{1}, std::chrono::milliseconds{1});

    auto waiter = wait_timer_twice(t);
    loop.schedule(waiter);
    loop.run();
}

TEST_CASE(prepare_wait) {
    event_loop loop;

    auto w = prepare::create(loop);
    w.start();

    auto waiter = wait_prepare(w);
    loop.schedule(waiter);
    loop.run();

    w.stop();
}

TEST_CASE(check_wait) {
    event_loop loop;

    auto w = check::create(loop);
    w.start();

    auto waiter = wait_check(w);
    loop.schedule(waiter);
    loop.run();

    w.stop();
}

};  // TEST_SUITE(watcher_io)

}  // namespace eventide
