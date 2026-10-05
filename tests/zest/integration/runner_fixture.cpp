#include <chrono>
#include <cstdlib>
#include <expected>
#include <format>
#include <print>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "kota/deco/deco.h"
#include "kota/zest/async.h"
#include "kota/zest/zest.h"

// Tests that misbehave on purpose, for check_runner.cmake: each must fail on
// its own, and the tests sharing its worker must still run.

namespace kota::zest {

namespace {

ZEST_SUITE(fixture) {

ZEST_CASE(passes) {
    ZEXPECT(1 == 1);
}

ZEST_CASE(prints) {
    std::println("printed by fixture.prints");
}

ZEST_CASE(fails) {
    ZEXPECT(1 == 2);
}

ZEST_CASE(skips) {
    skip();
}

ZEST_CASE(aborts) {
    std::println("printed by fixture.aborts");
    std::abort();
}

ZEST_CASE(exits_early) {
    std::exit(0);
}

ZEST_CASE(fails_on_thread) {
    std::thread([] { ZEXPECT(1 == 2); }).join();
}

#ifdef __cpp_exceptions
ZEST_CASE(throws) {
    throw std::runtime_error("thrown by the test");
}
#endif

// Passes, but its worker then exits badly, as a leak check would make it.
// Serial, so that no crashing test shares its worker and skips the exit.
ZEST_CASE(fails_at_exit, serial = true) {
    std::atexit([] { std::_Exit(3); });
}

ZEST_CASE_GROUP(group) {
    for(int i = 0; i < 3; ++i) {
        add_case(std::format("case_{}", i), [] {});
    }
    // Two tests of one name, which the runner must refuse.
    if(std::getenv("ZEST_FIXTURE_DUPLICATE") != nullptr) {
        add_case("passes", [] {});
    }
}

};  // ZEST_SUITE(fixture)

// Run on their own with a short --timeout: a failing test spends a while
// resolving its stack trace, which a short limit would cut off.
ZEST_SUITE(fixture_hang) {

ZEST_CASE(hangs) {
    std::println("printed by fixture_hang.hangs");
    std::this_thread::sleep_for(std::chrono::hours(1));
}

ZEST_CASE(passes_after) {
    ZEXPECT(1 == 1);
}

#ifndef _WIN32
// Closes the runner's channel and goes on: the runner kills its worker, which
// is no crash of its own.
ZEST_CASE(closes_the_channel, crashes = true) {
    for(int fd = 3; fd < 1024; ++fd) {
        ::close(fd);
    }
    std::this_thread::sleep_for(std::chrono::hours(1));
}
#endif

// Passes, but under ZEST_FIXTURE_HANG_AT_EXIT its worker then hangs on its
// way out, until the runner kills it.
ZEST_CASE(hangs_at_exit) {
    if(std::getenv("ZEST_FIXTURE_HANG_AT_EXIT") != nullptr) {
        std::atexit([] { std::this_thread::sleep_for(std::chrono::hours(1)); });
    }
}

};  // ZEST_SUITE(fixture_hang)

// Failures whose reports check_runner.cmake reads line by line.
ZEST_SUITE(fixture_report) {

ZEST_CASE(comparison) {
    ZEXPECT(std::string("left") == "right");
}

ZEST_CASE(predicate) {
    ZEXPECT(contains(std::string("haystack"), "needle"));
}

ZEST_CASE(unexpected) {
    std::expected<int, std::string> result = std::unexpected(std::string("boom"));
    ZEXPECT(result);
}

ZEST_CASE(in_context) {
    ZEST_CONTEXT("while checking {}", 42);
    {
        ZEST_CONTEXT("inner");
        ZEXPECT(1 == 2);
    }
}

ZEST_CASE(negated_predicate) {
    ZEXPECT(!contains(std::string("haystack"), "hay"));
}

ZEST_CASE(static_failure) {
    ZSTATIC_EXPECT(1 + 1 == 3);
}

ZEST_CASE(stops_at_assert) {
    ZASSERT(1 == 2);
    std::println("printed after a failed assert");
}

// A failed ZEXPECT goes on with the test and runs no fatal hook.
ZEST_CASE(continues_after_expect) {
    FatalHook hook{[] { std::println("hook ran after an expect"); }};
    ZEXPECT(1 == 2);
    std::println("printed after a failed expect");
}

// Run without --snapshot-dir, which fails the snapshot.
ZEST_CASE(snapshot_in_context) {
    ZEST_CONTEXT("while taking a snapshot");
    ZEXPECT(snapshot("unchecked"));
}

#ifdef __cpp_exceptions
ZEST_CASE(throws_nothing) {
    ZEXPECT(throws([] { return std::string("no exception"); }));
}
#endif

#ifdef __cpp_exceptions
ZEST_CASE(throws_unexpectedly) {
    ZEXPECT(!throws([] { throw std::runtime_error("thrown on purpose"); }));
}
#endif

};  // ZEST_SUITE(fixture_report)

// run() of tasks that do not end as they should: each case fails, and the task
// is cancelled, which ends the run.
ZEST_SUITE(fixture_loop, LoopFixture) {

// A task that waits for ever outlasts the watchdog the test sets.

ZEST_CASE(outlasts_the_watchdog) {
    watchdog = std::chrono::milliseconds(50);
    event never;
    auto waits = [&]() -> task<> {
        co_await never.wait();
    };
    auto [waited] = run(waits());
    ZEXPECT(waited.is_cancelled());
}

// A task stops the loop under run(), then waits for ever.
ZEST_CASE(stopped_under_run) {
    event never;
    auto stops = [&]() -> task<> {
        loop.stop();
        co_await never.wait();
    };
    auto [stopped] = run(stops());
    ZEXPECT(stopped.is_cancelled());
}

// Work running on a pool thread cannot be cancelled, so the task awaiting it
// is still running a watchdog period after the watchdog cancelled it, which
// ends the worker as a failed ZASSERT does. The period gives the pool time to
// take the work: work still queued would be cancelled.
ZEST_CASE(outlives_its_cancel) {
    watchdog = std::chrono::milliseconds(500);
    auto stuck = []() -> task<> {
        co_await queue([] { std::this_thread::sleep_for(std::chrono::seconds(10)); });
    };
    run(stuck());
    std::println("printed after the watchdog ended the worker");
}

};  // ZEST_SUITE(fixture_loop)

int half(int number) {
    ZASSERT(number % 2 == 0);
    return number / 2;
}

// Failed ZASSERTs anywhere: each ends its worker after running the fatal
// hooks, the runner reports the test as failed, not crashed, and a fresh
// worker runs the next test.
ZEST_SUITE(fixture_fatal) {

// First in the suite: check_runner.cmake runs the suite without isolation,
// where this case ends the run.
ZEST_CASE(in_body) {
    FatalHook first{[] { std::println("first hook ran"); }};
    FatalHook second{[] { std::println("second hook ran"); }};
    ZASSERT(1 == 2);
    std::println("printed after a fatal assert");
}

ZEST_CASE(in_helper) {
    ZEXPECT(half(3) == 1);
    std::println("printed after a fatal helper");
}

ZEST_CASE(on_thread) {
    std::thread([] { ZASSERT(1 == 2); }).join();
    std::println("printed after a fatal thread");
}

ZEST_CASE(hook_crashes) {
    FatalHook crash{[] { std::abort(); }};
    ZASSERT(1 == 2);
}

#ifdef __cpp_exceptions
// What a hook throws is printed, and the older hooks still run.
ZEST_CASE(hook_throws) {
    FatalHook older{[] { std::println("hook older than the throw ran"); }};
    FatalHook throws{[] { throw std::runtime_error("thrown by a hook"); }};
    ZASSERT(1 == 2);
}
#endif

#ifdef __cpp_exceptions
// A report that throws still ends the worker, after its hooks.
ZEST_CASE(report_throws) {
    FatalHook hook{[] { std::println("hook after a throwing report ran"); }};
    ZASSERT(Match{
        .held = false,
        .explain = []() -> std::string { throw std::runtime_error("thrown by a report"); },
    });
}
#endif

// A hook's own failed ZASSERT ends the worker at once.
ZEST_CASE(hook_asserts) {
    FatalHook older{[] { std::println("hook older than the assert ran"); }};
    FatalHook asserts{[] { ZASSERT(2 == 3); }};
    ZASSERT(1 == 2);
}

// A hook that goes out of scope never runs.
ZEST_CASE(hook_out_of_scope) {
    {
        FatalHook gone{[] { std::println("hook out of scope ran"); }};
    }
    ZASSERT(1 == 2);
}

ZEST_CASE(passes_after) {
    ZEXPECT(1 == 1);
}

};  // ZEST_SUITE(fixture_fatal)

ZEST_SUITE(fixture_fatal_loop, LoopFixture) {

ZEST_CASE(in_coroutine) {
    auto fails = []() -> task<int> {
        ZASSERT(1 == 2);
        co_return 1;
    };
    run(fails());
    std::println("printed after a fatal coroutine");
}

};  // ZEST_SUITE(fixture_fatal_loop)

// A fatal hook that never returns, which the runner gives up on after
// --timeout.
ZEST_SUITE(fixture_hook_hang) {

ZEST_CASE(hangs) {
    FatalHook hangs{[] { std::this_thread::sleep_for(std::chrono::hours(1)); }};
    ZASSERT(1 == 2);
}

};  // ZEST_SUITE(fixture_hook_hang)

// Set by a test, to tell whether a later one shares its worker.
bool marked = false;

// Crash tests: they pass by killing their worker.
ZEST_SUITE(fixture_crash) {

ZEST_CASE(aborts, crashes = true) {
    std::println("printed by fixture_crash.aborts");
    std::abort();
}

ZEST_CASE(exits_with_an_error, crashes = true) {
    std::exit(4);
}

ZEST_CASE(finishes, crashes = true) {}

ZEST_CASE(exits_cleanly, crashes = true) {
    std::exit(0);
}

ZEST_CASE(asserts_first, crashes = true) {
    ZASSERT(1 == 2);
    std::abort();
}

ZEST_CASE(skips, crashes = true) {
    skip();
}

// A failed check before the crash fails the test: the crash cannot report it.
ZEST_CASE(expects_then_crashes, crashes = true) {
    ZEXPECT(1 == 2);
    std::abort();
}

ZEST_CASE(marks_the_worker) {
    marked = true;
}

// Crashes only in a worker that ran no other test.
ZEST_CASE(aborts_in_a_fresh_worker, crashes = true) {
    if(!marked) {
        std::abort();
    }
}

};  // ZEST_SUITE(fixture_crash)

// Workers each check one snapshot, one of them before crashing; the runner
// must count all of them as checked.
ZEST_SUITE(fixture_snapshot) {

ZEST_CASE(checked) {
    ZEXPECT(snapshot("fresh"));
}

ZEST_CASE(also_checked) {
    ZEXPECT(snapshot("fresh"));
}

ZEST_CASE(checked_before_crashing, crashes = true) {
    ZEXPECT(snapshot("fresh"));
    std::abort();
}

};  // ZEST_SUITE(fixture_snapshot)

struct FixtureOptions {
    Options zest;

    DecoFlag(help = "make every worker fail to start"; required = false)
    fail_worker_start = false;
};

}  // namespace

}  // namespace kota::zest

// Embeds zest's options the way a downstream test program does, so that its
// own flag has to reach the workers.
int main(int argc, char** argv) {
    auto args = kota::deco::util::argvify(argc, argv);
    auto parsed = kota::deco::cli::parse<kota::zest::FixtureOptions>(args);
    if(!parsed.has_value()) {
        return 1;
    }
    auto& options = parsed->options;
    if(*options.fail_worker_start && *options.zest.zest_worker) {
        return 7;
    }
    return kota::zest::run_tests(std::move(options.zest), argc, argv);
}
