#include <atomic>

#include "eventide/zest/zest.h"
#include "eventide/async/async.h"

namespace eventide {

namespace {

task<void, error> wait_work(std::atomic<int>& flag, event_loop& loop) {
    auto ec = co_await queue([&]() { flag.fetch_add(1); }, loop);
    event_loop::current().stop();
    co_await or_fail(ec);
}

task<void, error>
    wait_work_target(std::atomic<int>& flag, std::atomic<int>& done, int target, event_loop& loop) {
    auto ec = co_await queue([&]() { flag.fetch_add(1); }, loop);
    if(done.fetch_add(1) + 1 == target) {
        event_loop::current().stop();
    }
    co_await or_fail(ec);
}

}  // namespace

TEST_SUITE(work_request_io) {

TEST_CASE(queue_runs) {
    event_loop loop;
    std::atomic<int> flag{0};

    auto worker = wait_work(flag, loop);
    loop.schedule(worker);
    loop.run();

    auto ec = worker.result();
    EXPECT_FALSE(ec.has_error());
    EXPECT_EQ(flag.load(), 1);
}

TEST_CASE(queue_runs_twice) {
    event_loop loop;
    std::atomic<int> flag{0};
    std::atomic<int> done{0};

    auto first = wait_work_target(flag, done, 2, loop);
    auto second = wait_work_target(flag, done, 2, loop);

    loop.schedule(first);
    loop.schedule(second);
    loop.run();

    auto ec1 = first.result();
    auto ec2 = second.result();
    EXPECT_FALSE(ec1.has_error());
    EXPECT_FALSE(ec2.has_error());
    EXPECT_EQ(flag.load(), 2);
}

TEST_CASE(queue_batch_runs_all) {
    event_loop loop;
    constexpr int N = 16;
    std::atomic<int> counter{0};

    auto worker = [&](event_loop& loop) -> task<void, error> {
        co_await queue_batch(N, [&counter](std::size_t) { counter.fetch_add(1); }, loop).or_fail();
        loop.stop();
    }(loop);

    loop.schedule(worker);
    loop.run();

    auto ec = worker.result();
    EXPECT_FALSE(ec.has_error());
    EXPECT_EQ(counter.load(), N);
}

TEST_CASE(queue_batch_indices_correct) {
    event_loop loop;
    constexpr int N = 8;
    std::atomic<int> sum{0};

    auto worker = [&](event_loop& loop) -> task<void, error> {
        co_await queue_batch(
            N,
            [&sum](std::size_t i) { sum.fetch_add(static_cast<int>(i)); },
            loop)
            .or_fail();
        loop.stop();
    }(loop);

    loop.schedule(worker);
    loop.run();

    auto ec = worker.result();
    EXPECT_FALSE(ec.has_error());
    // 0+1+2+...+7 = 28
    EXPECT_EQ(sum.load(), 28);
}

TEST_CASE(queue_batch_zero_count) {
    event_loop loop;

    auto worker = [](event_loop& loop) -> task<void, error> {
        co_await queue_batch(0, [](std::size_t) {}, loop).or_fail();
        loop.stop();
    }(loop);

    loop.schedule(worker);
    loop.run();

    auto ec = worker.result();
    EXPECT_FALSE(ec.has_error());
}

TEST_CASE(queue_batch_single_item) {
    event_loop loop;
    std::atomic<int> flag{0};

    auto worker = [&](event_loop& loop) -> task<void, error> {
        co_await queue_batch(1, [&flag](std::size_t) { flag.store(1); }, loop).or_fail();
        loop.stop();
    }(loop);

    loop.schedule(worker);
    loop.run();

    auto ec = worker.result();
    EXPECT_FALSE(ec.has_error());
    EXPECT_EQ(flag.load(), 1);
}

};  // TEST_SUITE(work_request_io)

}  // namespace eventide
