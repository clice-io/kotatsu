#include <chrono>
#include <cstdlib>
#include <format>
#include <print>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

// Tests that misbehave on purpose, for check_runner.cmake: each must fail on
// its own, and the tests sharing its worker must still run.

namespace kota::zest {

namespace {

TEST_SUITE(fixture) {

TEST_CASE(passes) {
    EXPECT_EQ(1, 1);
}

TEST_CASE(prints) {
    std::println("printed by fixture.prints");
}

TEST_CASE(fails) {
    EXPECT_EQ(1, 2);
}

TEST_CASE(skips) {
    skip();
}

TEST_CASE(aborts) {
    std::println("printed by fixture.aborts");
    std::abort();
}

TEST_CASE(exits_early) {
    std::exit(0);
}

TEST_CASE(fails_on_thread) {
    std::thread([] { EXPECT_EQ(1, 2); }).join();
}

// clang-cl's ASan hands exception handlers a broken reference to the
// exception, so there a throwing test crashes its worker when the runner reads
// the message; it still fails, as CRASHED.
#if defined(_WIN32) && defined(__clang__)
#if __has_feature(address_sanitizer)
#define ZEST_FIXTURE_BROKEN_CATCH
#endif
#endif

#if defined(__cpp_exceptions) && !defined(ZEST_FIXTURE_BROKEN_CATCH)
TEST_CASE(throws) {
    throw std::runtime_error("thrown by the test");
}
#endif

// Passes, but its worker then exits badly, as a leak check would make it.
// Serial, so that no crashing test shares its worker and skips the exit.
TEST_CASE(fails_at_exit, serial = true) {
    std::atexit([] { std::_Exit(3); });
}

TEST_CASE_GROUP(group) {
    for(int i = 0; i < 3; ++i) {
        add_case(std::format("case_{}", i), [] {});
    }
    // Two tests of one name, which the runner must refuse.
    if(std::getenv("ZEST_FIXTURE_DUPLICATE") != nullptr) {
        add_case("passes", [] {});
    }
}

};  // TEST_SUITE(fixture)

// Run on their own with a short --timeout: a failing test spends a while
// resolving its stack trace, which a short limit would cut off.
TEST_SUITE(fixture_hang) {

TEST_CASE(hangs) {
    std::println("printed by fixture_hang.hangs");
    std::this_thread::sleep_for(std::chrono::hours(1));
}

TEST_CASE(passes_after) {
    EXPECT_EQ(1, 1);
}

};  // TEST_SUITE(fixture_hang)

// Two workers each check one snapshot; the runner must count both as checked.
TEST_SUITE(fixture_snapshot) {

TEST_CASE(checked) {
    EXPECT_SNAPSHOT("fresh");
}

TEST_CASE(also_checked) {
    EXPECT_SNAPSHOT("fresh");
}

};  // TEST_SUITE(fixture_snapshot)

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
