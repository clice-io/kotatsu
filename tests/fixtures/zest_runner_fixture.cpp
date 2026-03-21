#include "eventide/zest/zest.h"

namespace {

TEST_SUITE(zest_runner_fixture) {

TEST_CASE(passing_case) {
    EXPECT_TRUE(true);
}

TEST_CASE(failing_case) {
    EXPECT_TRUE(false);
}

TEST_CASE(skipping_case) {
    skip();
}

};  // TEST_SUITE(zest_runner_fixture)

}  // namespace
