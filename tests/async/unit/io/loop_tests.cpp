#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "support/harness/throws.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/async/async.h"

namespace kota {

namespace {

// Tests of event_loop itself drive `loop` by hand; the rest go through run().
ZEST_SUITE(async_io_loop, zest::LoopFixture) {

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
    ASSERT(root.done());
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
    EXPECT(scheduled.done());
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

// A child that ends cancelled ends a root the loop owns at its co_await: the
// child's end destroys the root's frame, and the child with it, before the
// await returns. ASan builds catch a read of the freed child there.
ZEST_CASE(scheduled_temporary_ended_by_a_cancelled_child_is_destroyed) {
    auto frame_alive = std::make_shared<int>();
    std::weak_ptr<int> watch = frame_alive;
    bool child_ran = false;
    bool resumed = false;
    auto child = [&]() -> task<> {
        child_ran = true;
        co_await cancel();
    };
    auto make = [&](std::shared_ptr<int>) -> task<> {
        co_await child();
        resumed = true;
    };

    loop.schedule(make(std::move(frame_alive)));
    EXPECT(loop.run() == 0);
    EXPECT(watch.expired());
    EXPECT(child_ran);
    EXPECT(!resumed);
}
#endif

// A loop that goes before its roots' turn frees the ones it owns, a root its
// caller dropped first included, and leaves a root its caller keeps to it.
ZEST_CASE(roots_that_never_ran_go_with_their_loop) {
    auto frames = std::make_shared<int>();
    auto work = [](std::shared_ptr<int>) -> task<> {
        co_return;
    };
    task<> kept = work(frames);
    {
        event_loop other;
        other.schedule(work(frames));
        other.schedule(kept);
        task<> dropped = work(frames);
        other.schedule(dropped);
    }
    EXPECT(frames.use_count() == 2);
    kept = task<>();
    EXPECT(frames.use_count() == 1);
}

// A root cancelled before its first turn never runs, whether the cancel
// comes before it is scheduled or after, from a root the turn runs first.
ZEST_CASE(root_cancelled_before_its_first_turn_never_runs) {
    int ran = 0;
    auto make = [&]() -> task<int> {
        ran += 1;
        co_return 1;
    };
    auto early = make();
    auto late = make();
    early.cancel();
    auto canceller = [&]() -> task<> {
        late.cancel();
        co_return;
    };
    auto first = canceller();

    loop.schedule(early);
    loop.schedule(first);
    loop.schedule(late);
    loop.run();
    EXPECT(early.is_cancelled());
    EXPECT(late.is_cancelled());
    EXPECT(ran == 0);
}

// A root the loop owns and that was cancelled before its first turn never
// runs, and the loop still frees it. Its frame holds a copy of `frame`, which
// tells when it goes.
ZEST_CASE(owned_root_cancelled_before_it_starts_is_freed) {
    auto frame = std::make_shared<int>();
    std::weak_ptr<int> watch = frame;
    bool ran = false;
    auto make = [&](std::shared_ptr<int>) -> task<> {
        ran = true;
        co_return;
    };
    auto owned = make(std::move(frame));
    owned.cancel();

    loop.schedule(std::move(owned));
    EXPECT(!watch.expired());
    loop.run();
    EXPECT(watch.expired());
    EXPECT(!ran);
}

// A scheduled root its caller drops before the loop starts it is let go: it
// never runs, and the loop frees it on the turn it would have started.
ZEST_CASE(scheduled_root_dropped_before_its_turn_is_let_go) {
    auto frame = std::make_shared<int>();
    std::weak_ptr<int> watch = frame;
    bool ran = false;
    auto make = [&](std::shared_ptr<int>) -> task<> {
        ran = true;
        co_return;
    };
    {
        auto root = make(std::move(frame));
        loop.schedule(root);
    }
    EXPECT(!watch.expired());

    EXPECT(loop.run() == 0);
    EXPECT(watch.expired());
    EXPECT(!ran);
}

// A running root its caller drops is let go too: the drop cancels it, which
// ends its wait on the event, and the loop frees it as it ends.
ZEST_CASE(running_root_dropped_by_its_caller_is_let_go) {
    auto frame = std::make_shared<int>();
    std::weak_ptr<int> watch = frame;
    event never;
    bool resumed = false;
    auto make = [&](std::shared_ptr<int>) -> task<> {
        co_await never.wait();
        resumed = true;
    };
    std::optional<task<>> root(make(std::move(frame)));
    auto dropper = [&]() -> task<> {
        root.reset();
        co_return;
    };
    auto dropping = dropper();

    loop.schedule(*root);
    loop.schedule(dropping);
    EXPECT(loop.run() == 0);
    EXPECT(dropping.done());
    EXPECT(watch.expired());
    EXPECT(!resumed);
    EXPECT(!never.has_waiters());
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
    EXPECT(without_channel.is_cancelled());
    EXPECT(with_channel.result().is_cancelled());
}

#if KOTA_ENABLE_EXCEPTIONS
// Reads what was thrown; see test::exceptions_unreadable.
ZEST_CASE(failed_root_rethrows_through_result, skip = test::exceptions_unreadable) {
    auto thrower = []() -> task<int> {
        throw std::runtime_error("root");
        co_return 0;
    };
    auto root = thrower();

    loop.schedule(root);
    loop.run();
    EXPECT(root.done());
    EXPECT(test::thrown([&] { root.result(); }) == "root");
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
    EXPECT(!pending.done());
    EXPECT(!resumed);
    pending.cancel();
    EXPECT(pending.is_cancelled());
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

// The loop closes the handles still open when it goes, and a wait pending on
// one stays pending: nothing that loop queues runs any more. The timer is
// freed by its own destructor afterwards, which lets the wait go, and the
// wait's cancel ends it. The sanitizer builds catch a use of the freed timer
// or a leak there.
ZEST_CASE(wait_on_a_handle_outliving_its_loop_ends_when_cancelled) {
    std::optional<event_loop> own(std::in_place);
    auto t = timer::create(*own);
    auto waiting = t.wait();

    own->schedule(waiting);
    // The timer never started, so nothing keeps this loop running.
    EXPECT(own->run() == 0);
    ASSERT(!waiting.done());
    own.reset();
    t = timer();
    EXPECT(!waiting.done());
    waiting.cancel();
    EXPECT(waiting.is_cancelled());
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
    ASSERT(failed.has_error());
    EXPECT(failed.error() == error::io_error);
}

#if KOTA_ENABLE_EXCEPTIONS
// Reads what was thrown; see test::exceptions_unreadable.
ZEST_CASE(kota_run_rethrows_what_a_task_throws, skip = test::exceptions_unreadable) {
    auto thrower = []() -> task<int> {
        throw std::runtime_error("from run");
        co_return 0;
    };

    EXPECT(test::thrown([&] { kota::run(thrower()); }) == "from run");
}
#endif

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

// The destructor lets the loop go as assigning over the relay does.
ZEST_CASE(relay_destroyed_in_a_callback_lets_run_return) {
    bool called = false;
    std::optional<relay> held = loop.create_relay();
    held->send([&] {
        called = true;
        held.reset();
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
