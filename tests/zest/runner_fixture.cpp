#include <chrono>
#include <cstdlib>
#include <format>
#include <print>
#include <stdexcept>
#include <thread>

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
    std::abort();
}

TEST_CASE(hangs) {
    std::this_thread::sleep_for(std::chrono::hours(1));
}

TEST_CASE(exits_early) {
    std::exit(0);
}

TEST_CASE(fails_on_thread) {
    std::thread([] { EXPECT_EQ(1, 2); }).join();
}

#ifdef __cpp_exceptions
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
}

};  // TEST_SUITE(fixture)

}  // namespace

}  // namespace kota::zest
