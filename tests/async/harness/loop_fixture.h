#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <span>
#include <tuple>
#include <utility>

#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::test {

/// What run() reports for a task: its value or error, or that it was cancelled.
template <typename Task>
using run_result_t = outcome<typename Task::value_type, typename Task::error_type, cancellation>;

/// The task run() awaits for `Task`: the same frame, with its cancellation
/// caught.
template <typename Task>
using caught_t = task<typename Task::value_type, typename Task::error_type, cancellation>;

/// Owns the event loop a test runs its tasks on.
struct LoopFixture {
    event_loop loop;

    /// How long run() waits for its tasks. Past it the test fails and the
    /// tasks still running are cancelled, so a hang ends the test, not the
    /// job; tasks that are still running one more period later abort the
    /// process, since nothing can safely free their frames.
    constexpr static std::chrono::seconds watchdog{10};

    /// Runs `tasks` on `loop`, started in the order given, until every one of
    /// them has finished, and returns what each ended with. The loop stops as
    /// soon as the last task finishes, whatever handles are still open. A task
    /// that throws rethrows here once all of them have finished. Every task's
    /// frame lives until run() returns, so a test may still cancel() a task
    /// that has finished.
    template <typename... Tasks>
        requires (sizeof...(Tasks) > 0)
    std::tuple<run_result_t<Tasks>...> run(Tasks... tasks) {
        constexpr auto count = sizeof...(Tasks);
        std::tuple<caught_t<Tasks>...> frames{std::move(tasks).catch_cancel()...};
        std::tuple<std::optional<run_result_t<Tasks>>...> results;
        std::array<async_node*, count> running;
        std::size_t remaining = count;
        bool expired = false;

        auto trackers = [&]<std::size_t... I>(std::index_sequence<I...>) {
            running = {std::get<I>(frames).operator->()...};
            return std::array<task<>, count>{
                track(std::get<I>(frames), std::get<I>(results), running[I], remaining)...};
        }(std::index_sequence_for<Tasks...>{});
        auto guard = watch(running, remaining, expired);

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
                cancel_running(running);
                loop.run();
            }
        }
        // Ends the watchdog's sleep.
        guard->cancel();

        {
            ZEST_CONTEXT("tasks still running after {} were cancelled by the watchdog", watchdog);
            EXPECT(!expired);
        }
        for(auto& tracker: trackers) {
            // Rethrows what the task threw.
            tracker.result();
        }
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return std::tuple<run_result_t<Tasks>...>(std::move(*std::get<I>(results))...);
        }(std::index_sequence_for<Tasks...>{});
    }

private:
    /// Counts a task off on every way out of its tracker, a throw included,
    /// and stops the loop after the last one.
    struct Countdown {
        async_node*& running;
        std::size_t& remaining;
        event_loop& loop;

        ~Countdown() {
            running = nullptr;
            remaining -= 1;
            if(remaining == 0) {
                loop.stop();
            }
        }
    };

    /// Hands the frame back to where run() keeps it once the tracker is
    /// done with it, a throw included.
    template <typename Task>
    struct GiveBack {
        Task& owner;
        typename Task::awaiter& awaiting;

        ~GiveBack() {
            owner = std::move(awaiting.awaitee);
        }
    };

    template <typename Task>
    task<> track(Task& owned,
                 std::optional<run_result_t<Task>>& result,
                 async_node*& running,
                 std::size_t& remaining) {
        Countdown countdown{running, remaining, loop};
        typename Task::awaiter awaiting{std::move(owned)};
        GiveBack<Task> give_back{owned, awaiting};
        result.emplace(co_await awaiting);
    }

    /// Cancelling one task can finish others, which clears their slots.
    static void cancel_running(std::span<async_node*> running) {
        for(auto* node: running) {
            if(node) {
                node->cancel();
            }
        }
    }

    task<> watch(std::span<async_node*> running, const std::size_t& remaining, bool& expired) {
        co_await sleep(watchdog, loop);
        // The loop may fire the timer in the turn the last task finished.
        if(remaining == 0) {
            co_return;
        }
        expired = true;
        cancel_running(running);
        // Still here once more: a cancellation that cannot complete, such as
        // work stuck on a pool thread.
        co_await sleep(watchdog, loop);
        if(remaining == 0) {
            co_return;
        }
        ZEST_CONTEXT("tasks still running {} after the watchdog cancelled them", watchdog);
        // Reports the failure before the abort: remaining is not 0 here.
        EXPECT(remaining == 0U);
        std::abort();
    }
};

}  // namespace kota::test
