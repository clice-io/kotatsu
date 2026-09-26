#pragma once

#include <array>
#include <cassert>
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
    /// that throws rethrows here once all of them have finished.
    template <typename... Tasks>
        requires (sizeof...(Tasks) > 0)
    std::tuple<run_result_t<Tasks>...> run(Tasks... tasks) {
        constexpr auto count = sizeof...(Tasks);
        std::tuple<std::optional<run_result_t<Tasks>>...> results;
        std::array<async_node*, count> running = {tasks.operator->()...};
        std::size_t remaining = count;
        bool expired = false;

        auto trackers = [&]<std::size_t... I>(std::index_sequence<I...>) {
            return std::array<task<>, count>{
                track(std::move(tasks), std::get<I>(results), running[I], remaining)...};
        }(std::index_sequence_for<Tasks...>{});
        auto guard = watch(running, remaining, expired);

        for(auto& tracker: trackers) {
            loop.schedule(tracker);
        }
        loop.schedule(guard);
        loop.run();
        assert(remaining == 0 && "run(): the loop was stopped while tasks were running");
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

    template <typename Task>
    task<> track(Task task,
                 std::optional<run_result_t<Task>>& result,
                 async_node*& running,
                 std::size_t& remaining) {
        Countdown countdown{running, remaining, loop};
        result.emplace(co_await std::move(task).catch_cancel());
    }

    task<> watch(std::span<async_node*> running, const std::size_t& remaining, bool& expired) {
        co_await sleep(watchdog, loop);
        expired = true;
        // Cancelling one task can finish others, which clears their slots.
        for(auto* node: running) {
            if(node) {
                node->cancel();
            }
        }
        // Still here once more: a cancellation that cannot complete, such as
        // work stuck on a pool thread.
        co_await sleep(watchdog, loop);
        ZEST_CONTEXT("tasks still running {} after the watchdog cancelled them", watchdog);
        EXPECT(remaining == 0U);
        std::abort();
    }
};

}  // namespace kota::test
