#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/async/async.h"

// zest's support for tests of kota::async code, which takes the kota::zest::async target:
// plain zest does not bring kota::async to its users.

namespace kota::zest {

/// What LoopFixture::run() reports for a task: its value or error, or that it was cancelled.
template <typename Task>
using run_result_t = outcome<typename Task::value_type, typename Task::error_type, cancellation>;

/// A suite fixture owning the event loop its tests run their tasks on:
/// `ZEST_SUITE(name, kota::zest::LoopFixture)`.
struct LoopFixture {
    event_loop loop;

    /// How long run() waits for its tasks. Past it the test fails and the tasks still running
    /// are cancelled, so a hang ends the test, not the run; tasks still running one more period
    /// later abort the process, since nothing can safely free their frames. A suite or a test
    /// sets it before run() to give its tasks longer.
    std::chrono::milliseconds watchdog = std::chrono::seconds(10);

    /// Runs `tasks` on `loop`, started in the order given, until every one of them has
    /// finished, and returns what each ended with. The loop stops as soon as the last task
    /// finishes, whatever handles are still open. A task that throws rethrows here once all of
    /// them have finished. A task passed as an lvalue stays the test's, which can cancel() it
    /// while it runs; the frames of the others live until run() returns.
    template <typename... Tasks>
        requires (sizeof...(Tasks) > 0)
    std::tuple<run_result_t<std::remove_cvref_t<Tasks>>...> run(Tasks&&... tasks) {
        std::tuple<caught_t<std::remove_cvref_t<Tasks>>...> frames{
            catching(std::forward<Tasks>(tasks))...};
        std::tuple<std::optional<run_result_t<std::remove_cvref_t<Tasks>>>...> results;
        std::size_t remaining = sizeof...(Tasks);
        bool expired = false;

        auto cancel_running = [&] {
            // A task that has finished ignores the cancel.
            std::apply([](auto&... frame) { (frame.cancel(), ...); }, frames);
        };
        auto trackers = [&]<std::size_t... I>(std::index_sequence<I...>) {
            return std::array<task<>, sizeof...(Tasks)>{
                track(std::get<I>(frames), std::get<I>(results), remaining)...};
        }(std::index_sequence_for<Tasks...>{});
        auto watch = [&]() -> task<> {
            co_await sleep(watchdog, loop);
            // The loop may fire the timer in the turn the last task finished.
            if(remaining == 0) {
                co_return;
            }
            expired = true;
            cancel_running();
            // Still here once more: a cancellation that cannot complete, such
            // as work stuck on a pool thread.
            co_await sleep(watchdog, loop);
            if(remaining == 0) {
                co_return;
            }
            ZEST_CONTEXT("tasks still running {} after the watchdog cancelled them", watchdog);
            // Reports the failure before the abort: remaining is not 0 here.
            EXPECT(remaining == 0U);
            std::abort();
        };
        auto guard = watch();

        for(auto& tracker: trackers) {
            loop.schedule(tracker);
        }
        loop.schedule(guard);
        loop.run();
        if(remaining != 0) {
            // A test stopped the loop itself. Fail, then cancel what still
            // runs and go on, so that every task ends with a result.
            {
                ZEST_CONTEXT("run(): the loop was stopped with {} tasks running", remaining);
                // Reports the failure: remaining is not 0 here.
                EXPECT(remaining == 0U);
            }
            while(remaining != 0) {
                cancel_running();
                loop.run();
            }
        }
        // Ends the watchdog's sleep.
        guard.cancel();

        {
            ZEST_CONTEXT("tasks still running after {} were cancelled by the watchdog", watchdog);
            EXPECT(!expired);
        }
        for(auto& tracker: trackers) {
            // Rethrows what the task threw.
            tracker.result();
        }
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return std::tuple<run_result_t<std::remove_cvref_t<Tasks>>...>(
                std::move(*std::get<I>(results))...);
        }(std::index_sequence_for<Tasks...>{});
    }

private:
    /// The task run() awaits for `Task`: one that catches its cancellation.
    template <typename Task>
    using caught_t = task<typename Task::value_type, typename Task::error_type, cancellation>;

    /// Counts a task off on every way out of its tracker, a throw included,
    /// and stops the loop after the last one.
    struct Countdown {
        std::size_t& remaining;
        event_loop& loop;

        ~Countdown() {
            remaining -= 1;
            if(remaining == 0) {
                loop.stop();
            }
        }
    };

    template <typename T, typename E, typename C>
    static caught_t<task<T, E, C>> catching(task<T, E, C>&& owned) {
        return std::move(owned).catch_cancel();
    }

    /// A task the test keeps is awaited through one that ends as it does.
    template <typename T, typename E>
    static caught_t<task<T, E>> catching(task<T, E>& kept) {
        return await_kept(kept).catch_cancel();
    }

    template <typename T, typename E>
    static task<T, E> await_kept(task<T, E>& kept) {
        if constexpr(!std::is_void_v<T>) {
            co_return co_await kept;
        } else if constexpr(!std::is_void_v<E>) {
            co_await or_fail(co_await kept);
        } else {
            co_await kept;
        }
    }

    template <typename Frame, typename Result>
    task<> track(Frame& frame, std::optional<Result>& result, std::size_t& remaining) {
        Countdown countdown{remaining, loop};
        result.emplace(co_await frame);
    }
};

}  // namespace kota::zest
