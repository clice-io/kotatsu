#include <atomic>

#include "async/harness/loop_fixture.h"
#include "kota/zest/zest.h"

namespace kota {

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

ZEST_SUITE(async_io_work_request, loop_fixture) {

ZEST_CASE(queue_runs) {
    std::atomic<int> flag{0};

    auto worker = wait_work(flag, loop);
    schedule_all(worker);

    auto ec = worker.result();
    EXPECT(!ec.has_error());
    EXPECT(flag.load() == 1);
}

ZEST_CASE(queue_runs_twice) {
    std::atomic<int> flag{0};
    std::atomic<int> done{0};

    auto first = wait_work_target(flag, done, 2, loop);
    auto second = wait_work_target(flag, done, 2, loop);
    schedule_all(first, second);

    auto ec1 = first.result();
    auto ec2 = second.result();
    EXPECT(!ec1.has_error());
    EXPECT(!ec2.has_error());
    EXPECT(flag.load() == 2);
}

ZEST_CASE(queue_on_cancel_unused_on_normal_completion) {
    std::atomic<bool> hook_ran{false};
    bool has_value = false;
    int value = 0;

    auto worker = [&]() -> task<> {
        auto res = co_await queue([] { return 42; },
                                  function<void()>([&] { hook_ran.store(true); }),
                                  loop);
        has_value = res.has_value();
        if(has_value) {
            value = *res;
        }
        event_loop::current().stop();
    };

    auto worker_task = worker();
    schedule_all(worker_task);

    // Without cancellation the hook must never fire.
    EXPECT(has_value);
    EXPECT(value == 42);
    EXPECT(!hook_ran.load());
}

};  // ZEST_SUITE(async_io_work_request)

}  // namespace kota
