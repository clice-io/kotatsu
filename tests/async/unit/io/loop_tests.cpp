#include <chrono>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/async/async.h"

namespace kota {

namespace {

// Tests of event_loop itself drive `loop` by hand; the rest go through run().
ZEST_SUITE(async_io_loop, test::LoopFixture) {

ZEST_CASE(current_is_the_running_loop) {
    EXPECT(!event_loop::has_current());
    event_loop* seen = nullptr;
    auto probe = [&]() -> task<bool> {
        seen = &event_loop::current();
        co_return event_loop::has_current();
    };

    auto [result] = run(probe());
    ASSERT(result.has_value());
    EXPECT(*result);
    EXPECT(seen == &loop);
    EXPECT(!event_loop::has_current());
}

ZEST_CASE(run_without_work_returns_at_once) {
    event_loop own;
    EXPECT(own.run() == 0);
}

ZEST_CASE(scheduled_reference_stays_with_the_caller) {
    auto make = []() -> task<int> {
        co_await yield();
        co_return 7;
    };
    auto root = make();

    loop.schedule(root);
    EXPECT(loop.run() == 0);
    ASSERT(root->is_finished());
    EXPECT(root.result() == 7);
}

ZEST_CASE(task_scheduled_while_running_runs_on_a_later_turn) {
    std::vector<int> order;
    auto later = [&]() -> task<> {
        order.push_back(2);
        co_return;
    };
    auto scheduled = later();
    auto first = [&]() -> task<> {
        loop.schedule(scheduled);
        order.push_back(1);
        co_return;
    };
    auto root = first();

    loop.schedule(root);
    EXPECT(loop.run() == 0);
    EXPECT(scheduled->is_finished());
    EXPECT(order == std::vector{1, 2});
}

// MSVC's coroutine codegen under ASan cannot destroy a frame from its final
// suspension, which is how the loop frees a root it owns.
#if !KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
ZEST_CASE(scheduled_temporary_is_destroyed_by_the_loop) {
    auto frame_alive = std::make_shared<int>();
    std::weak_ptr<int> watch = frame_alive;
    auto make = [](std::shared_ptr<int>) -> task<> {
        co_await yield();
    };

    loop.schedule(make(std::move(frame_alive)));
    EXPECT(!watch.expired());
    EXPECT(loop.run() == 0);
    EXPECT(watch.expired());
}

// Cancelled while it runs, a root the loop owns ends at its next co_await:
// awaiting the child finalizes the root, which destroys its frame, and the
// child with it, before the await returns. ASan builds catch a read of the
// freed child there.
ZEST_CASE(scheduled_temporary_cancelled_while_running_ends_at_its_next_await) {
    auto frame_alive = std::make_shared<int>();
    std::weak_ptr<int> watch = frame_alive;
    async_node* self = nullptr;
    bool child_ran = false;
    bool resumed = false;
    auto child = [&]() -> task<> {
        child_ran = true;
        co_return;
    };
    auto make = [&](std::shared_ptr<int>) -> task<> {
        self->cancel();
        co_await child();
        resumed = true;
    };
    auto root = make(std::move(frame_alive));
    self = root.operator->();

    loop.schedule(std::move(root));
    EXPECT(loop.run() == 0);
    EXPECT(watch.expired());
    EXPECT(!child_ran);
    EXPECT(!resumed);
}
#endif

ZEST_CASE(task_cancelled_before_it_starts_never_runs) {
    bool ran = false;
    auto make = [&]() -> task<int> {
        ran = true;
        co_return 1;
    };
    auto root = make();
    root->cancel();

    loop.schedule(root);
    loop.run();
    EXPECT(root->is_cancelled());
    EXPECT(!ran);
}

ZEST_CASE(finished_roots_report_through_result) {
    auto ok = []() -> task<int, error> {
        co_return 3;
    };
    auto failing = []() -> task<int, error> {
        co_await fail(error::io_error);
    };
    auto good = ok();
    auto bad = failing();

    loop.schedule(good);
    loop.schedule(bad);
    loop.run();
    auto good_result = good.result();
    ASSERT(good_result.has_value());
    EXPECT(*good_result == 3);
    auto bad_result = bad.result();
    ASSERT(bad_result.has_error());
    EXPECT(bad_result.error() == error::io_error);
}

ZEST_CASE(cancelled_roots_report_through_value_and_result) {
    auto plain = []() -> task<int> {
        co_await cancel();
        co_return 1;
    };
    auto caught = []() -> task<int, void, cancellation> {
        co_await cancel();
        co_return 1;
    };
    auto without_channel = plain();
    auto with_channel = caught();

    loop.schedule(without_channel);
    loop.schedule(with_channel);
    loop.run();
    EXPECT(without_channel->is_cancelled());
    // Without a cancel channel, value() is all a cancelled root can report.
    EXPECT(!without_channel.value().has_value());
    EXPECT(with_channel.result().is_cancelled());
}

#if KOTA_ENABLE_EXCEPTIONS
ZEST_CASE(failed_root_rethrows_through_result) {
    auto thrower = []() -> task<int> {
        throw std::runtime_error("root");
        co_return 0;
    };
    auto root = thrower();

    loop.schedule(root);
    loop.run();
    EXPECT(root->is_failed());
    EXPECT_THROWS(root.result());
}
#endif

ZEST_CASE(stop_ends_run_with_work_still_pending) {
    bool resumed = false;
    auto sleeper = [&]() -> task<> {
        co_await sleep(std::chrono::hours(1));
        resumed = true;
    };
    auto stopper = [&]() -> task<> {
        loop.stop();
        co_return;
    };
    auto pending = sleeper();
    auto stopping = stopper();

    loop.schedule(pending);
    loop.schedule(stopping);
    // run() says whether work was left when it returned.
    EXPECT(loop.run() != 0);
    EXPECT(!pending->is_finished());
    EXPECT(!resumed);
    pending->cancel();
    EXPECT(pending->is_cancelled());
}

ZEST_CASE(on_destroy_callbacks_run_when_the_loop_goes) {
    int called = 0;
    {
        event_loop own;
        own.on_destroy([&] { called += 1; });
        own.on_destroy([&] { called += 10; });
        EXPECT(called == 0);
    }
    EXPECT(called == 11);
}

// The loop drops what relays sent but it never delivered.
ZEST_CASE(relay_callbacks_left_when_the_loop_goes_never_run) {
    bool called = false;
    {
        event_loop own;
        auto r = own.create_relay();
        r.send([&] { called = true; });
    }
    EXPECT(!called);
}

ZEST_CASE(kota_run_returns_every_value) {
    auto one = []() -> task<int> {
        co_return 1;
    };
    auto failing = []() -> task<int, error> {
        co_await fail(error::io_error);
    };

    auto [value, failed] = kota::run(one(), failing());
    ASSERT(value.has_value());
    EXPECT(*value == 1);
    ASSERT(failed.has_value());
    ASSERT(failed->has_error());
    EXPECT(failed->error() == error::io_error);
}

ZEST_CASE(relay_runs_callbacks_on_the_loop_in_order) {
    auto sender = [&]() -> task<std::vector<int>> {
        std::vector<int> order;
        event done;
        auto r = loop.create_relay();
        for(int i = 0; i < 5; ++i) {
            r.send([&, i] {
                order.push_back(i);
                if(i == 4) {
                    done.set();
                }
            });
        }
        co_await done.wait();
        co_return order;
    };

    auto [result] = run(sender());
    ASSERT(result.has_value());
    EXPECT(*result == std::vector{0, 1, 2, 3, 4});
}

// The relay holds the loop open until the last one goes: run() returns only
// after the callback that destroys it, and would have returned before the
// callback ran without the hold.
ZEST_CASE(relay_keeps_the_loop_running_until_the_last_is_gone) {
    bool called = false;
    auto first = loop.create_relay();
    auto second = loop.create_relay();
    first = relay{};
    second.send([&] {
        called = true;
        second = relay{};
    });

    EXPECT(loop.run() == 0);
    EXPECT(called);
}

ZEST_CASE(inert_relay_ignores_send) {
    bool called = false;
    relay inert;
    inert.send([&] { called = true; });
    auto live = loop.create_relay();
    auto moved = std::move(live);
    live.send([&] { called = true; });
    moved = relay{};

    EXPECT(loop.run() == 0);
    EXPECT(!called);
}

ZEST_CASE(relay_callbacks_sent_before_it_goes_still_run) {
    auto sender = [&]() -> task<int> {
        int count = 0;
        event done;
        {
            auto r = loop.create_relay();
            r.send([&] { count += 1; });
            r.send([&] {
                count += 1;
                done.set();
            });
        }
        co_await done.wait();
        co_return count;
    };

    auto [result] = run(sender());
    ASSERT(result.has_value());
    EXPECT(*result == 2);
}

ZEST_CASE(relay_send_from_a_callback_runs_later) {
    auto sender = [&]() -> task<std::vector<int>> {
        std::vector<int> order;
        event done;
        auto r = loop.create_relay();
        r.send([&] {
            order.push_back(1);
            r.send([&] {
                order.push_back(3);
                done.set();
            });
            order.push_back(2);
        });
        co_await done.wait();
        co_return order;
    };

    auto [result] = run(sender());
    ASSERT(result.has_value());
    EXPECT(*result == std::vector{1, 2, 3});
}

ZEST_CASE(relay_move_assignment_keeps_the_old_callbacks) {
    auto sender = [&]() -> task<std::vector<int>> {
        std::vector<int> order;
        event done;
        auto r = loop.create_relay();
        r.send([&] { order.push_back(1); });
        r = loop.create_relay();
        r.send([&] {
            order.push_back(2);
            done.set();
        });
        co_await done.wait();
        co_return order;
    };

    auto [result] = run(sender());
    ASSERT(result.has_value());
    EXPECT(*result == std::vector{1, 2});
}

};  // ZEST_SUITE(async_io_loop)

}  // namespace

}  // namespace kota
