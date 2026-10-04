#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "kota/zest/zest.h"

namespace kota::zest {

namespace {

std::string fixtures_dir() {
    return "tests/zest/system/fixtures";
}

std::string read_file(std::string_view path) {
    std::ifstream file(std::string(path), std::ios::binary);
    if(!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file), {});
}

ZEST_SUITE(zest_snapshot) {

ZEST_CASE(basic_named) {
    ZASSERT(snapshot("hello snapshot", "basic_named"));
}

ZEST_CASE(unnamed) {
    ZASSERT(snapshot("auto-named snapshot content"));
}

ZEST_CASE(multiple_named) {
    ZASSERT(snapshot("first value", "multi_first"));
    ZASSERT(snapshot("second value", "multi_second"));
    ZASSERT(snapshot("third value", "multi_third"));
}

ZEST_CASE(multiline) {
    std::string content = "line one\nline two\nline three";
    ZASSERT(snapshot(content, "multiline"));
}

ZEST_CASE(empty_string) {
    ZASSERT(snapshot("", "empty_string"));
}

ZEST_CASE(special_chars) {
    ZASSERT(snapshot("tabs\there\nnewlines\nand \"quotes\"", "special_chars"));
}

ZEST_CASE(values_render_as_debug_text) {
    std::vector<int> vector = {1, 2, 3};
    std::map<std::string, int> map = {
        {"alpha", 1},
        {"beta",  2},
    };
    ZASSERT(snapshot(vector, "vector"));
    ZASSERT(snapshot(map, "map"));
}

ZEST_CASE(glob_fixtures) {
    ZASSERT(snapshot_glob(fixtures_dir(), "**/*.txt", read_file));
}

ZEST_CASE(mismatch_detection) {
    ZEXPECT(snapshot("original value", "mismatch_detect"));
    auto mismatch = snapshot("different value", "mismatch_detect");
    ZEXPECT(!mismatch.held);
    ZEXPECT(contains(mismatch.explain(), "-  original value"));
    ZEXPECT(contains(mismatch.explain(), "+  different value"));
    ZEXPECT(snapshot("original value", "mismatch_detect"));
}

ZEST_CASE(update_mode) {
    ZEXPECT(snapshot("version_a", "update_mode_v"));
    detail::set_update_snapshots(true);
    ZEXPECT(snapshot("version_b", "update_mode_v"));
    detail::set_update_snapshots(false);
    ZEXPECT(snapshot("version_b", "update_mode_v"));
    detail::set_update_snapshots(true);
    ZEXPECT(snapshot("version_a", "update_mode_v"));
    detail::set_update_snapshots(false);
}

ZEST_CASE(duplicate_unnamed_error) {
    ZEXPECT(snapshot("first unnamed value"));
    auto second = snapshot("second unnamed attempt");
    ZEXPECT(!second.held);
    ZEXPECT(contains(second.explain(), "one unnamed snapshot"));
}

ZEST_CASE(missing_context_error) {
    detail::reset_snapshot_context("", "");
    auto taken = snapshot("value", "no_context");
    ZEXPECT(!taken.held);
    ZEXPECT(contains(taken.explain(), "no running test"));
}

ZEST_CASE(invalid_glob_error) {
    auto taken = snapshot_glob(".", "[unclosed", [](std::string_view) { return std::string{}; });
    ZEXPECT(!taken.held);
    ZEXPECT(contains(taken.explain(), "invalid glob pattern"));
}

ZEST_CASE(glob_no_matches) {
    auto taken =
        snapshot_glob(fixtures_dir(), "**/*.xyz", [](std::string_view) { return std::string{}; });
    ZEXPECT(!taken.held);
    ZEXPECT(contains(taken.explain(), "no file under"));
}

ZEST_CASE(body_with_separator) {
    std::string content = "before\n---\nafter";
    ZASSERT(snapshot(content, "body_with_separator"));
}

ZEST_CASE(unsafe_name_chars) {
    ZEXPECT(!snapshot("value", "bad/name"));
    ZEXPECT(!snapshot("value", "bad:name"));
}

ZEST_CASE(glob_empty_context) {
    detail::reset_snapshot_context("", "");
    auto taken =
        snapshot_glob(fixtures_dir(), "**/*.txt", [](std::string_view) { return std::string{}; });
    ZEXPECT(!taken.held);
}

// Snapshots are the running test's, whichever thread takes them.
ZEST_CASE(taken_on_another_thread) {
    std::thread([] { ZEXPECT(snapshot("from a thread", "thread")); }).join();
}

};  // ZEST_SUITE(zest_snapshot)

}  // namespace

}  // namespace kota::zest
