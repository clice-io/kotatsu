#include <format>

#include "kota/zest/zest.h"

namespace kota::zest {

namespace {

TEST_SUITE(zest_case_group) {

// Only here so that skipped_group's attrs have suite attrs to merge with.
TEST_SUITE_ATTRS(serial = true);

// Each case snapshots its own index, which pins both the name a body runs
// under and the body a name runs.
TEST_CASE_GROUP(dynamic_cases) {
    for(int i = 0; i < 3; ++i) {
        add_case(std::format("case_{}", i), [i] { EXPECT_SNAPSHOT(std::format("case {}", i)); });
    }
}

// The group-level skip attr must merge with suite_attrs and keep the case
// from ever running.
TEST_CASE_GROUP(skipped_group, skip = true) {
    add_case("never_runs", [] { failure(); });
}

};  // TEST_SUITE(zest_case_group)

}  // namespace

}  // namespace kota::zest
