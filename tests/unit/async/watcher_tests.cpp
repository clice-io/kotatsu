#include <chrono>

#include "loop_fixture.h"
#include "eventide/zest/zest.h"

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
    co_await sleep(1, loop);
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

TEST_SUITE(watcher_io, loop_fixture) {

TEST_CASE(timer_wait) {
    auto t = timer::create(loop);
    t.start(std::chrono::milliseconds{1}, std::chrono::milliseconds{0});

    auto waiter = wait_timer(t);
    schedule_all(waiter);
}

TEST_CASE(idle_wait) {
    auto w = idle::create(loop);
    w.start();

    auto waiter = wait_idle(w);
    schedule_all(waiter);

    w.stop();
}

TEST_CASE(sleep_once) {
    auto sleeper = wait_sleep(loop);
    schedule_all(sleeper);
}

TEST_CASE(timer_repeat_twice) {
    auto t = timer::create(loop);
    t.start(std::chrono::milliseconds{1}, std::chrono::milliseconds{1});

    auto waiter = wait_timer_twice(t);
    schedule_all(waiter);
}

TEST_CASE(prepare_wait) {
    auto w = prepare::create(loop);
    w.start();

    auto waiter = wait_prepare(w);
    schedule_all(waiter);

    w.stop();
}

TEST_CASE(check_wait) {
    auto w = check::create(loop);
    w.start();

    auto waiter = wait_check(w);
    schedule_all(waiter);

    w.stop();
}

};  // TEST_SUITE(watcher_io)

}  // namespace eventide
