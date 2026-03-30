#include <atomic>

#include "loop_fixture.h"
#include "eventide/zest/zest.h"

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

TEST_SUITE(work_request_io, loop_fixture) {

TEST_CASE(queue_runs) {
    std::atomic<int> flag{0};

    auto worker = wait_work(flag, loop);
    schedule_all(worker);

    auto ec = worker.result();
    EXPECT_FALSE(ec.has_error());
    EXPECT_EQ(flag.load(), 1);
}

TEST_CASE(queue_runs_twice) {
    std::atomic<int> flag{0};
    std::atomic<int> done{0};

    auto first = wait_work_target(flag, done, 2, loop);
    auto second = wait_work_target(flag, done, 2, loop);
    schedule_all(first, second);

    auto ec1 = first.result();
    auto ec2 = second.result();
    EXPECT_FALSE(ec1.has_error());
    EXPECT_FALSE(ec2.has_error());
    EXPECT_EQ(flag.load(), 2);
}

};  // TEST_SUITE(work_request_io)

}  // namespace eventide
