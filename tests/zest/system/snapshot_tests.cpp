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

/// Turns --update-snapshots on for as long as it lives.
struct UpdatingSnapshots {
    UpdatingSnapshots() : was(detail::set_update_snapshots(true)) {}

    UpdatingSnapshots(const UpdatingSnapshots&) = delete;
    UpdatingSnapshots& operator=(const UpdatingSnapshots&) = delete;

    ~UpdatingSnapshots() {
        detail::set_update_snapshots(was);
    }

    const bool was;
};

std::string read_file(std::string_view path) {
    std::ifstream file(std::string(path), std::ios::binary);
    if(!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file), {});
}

ZEST_SUITE(zest_snapshot) {

ZEST_CASE(basic_named) {
    ZEXPECT(snapshot("hello snapshot", "basic_named"));
}

ZEST_CASE(unnamed) {
    ZEXPECT(snapshot("auto-named snapshot content"));
}

ZEST_CASE(multiple_named) {
    ZEXPECT(snapshot("first value", "multi_first"));
    ZEXPECT(snapshot("second value", "multi_second"));
    ZEXPECT(snapshot("third value", "multi_third"));
}

ZEST_CASE(multiline) {
    std::string content = "line one\nline two\nline three";
    ZEXPECT(snapshot(content, "multiline"));
}

ZEST_CASE(empty_string) {
    ZEXPECT(snapshot("", "empty_string"));
}

ZEST_CASE(special_chars) {
    ZEXPECT(snapshot("tabs\there\nnewlines\nand \"quotes\"", "special_chars"));
}

ZEST_CASE(values_render_as_debug_text) {
    std::vector<int> vector = {1, 2, 3};
    std::map<std::string, int> map = {
        {"alpha", 1},
        {"beta",  2},
    };
    ZEXPECT(snapshot(vector, "vector"));
    ZEXPECT(snapshot(map, "map"));
}

ZEST_CASE(glob_fixtures) {
    ZEXPECT(snapshot_glob(fixtures_dir(), "**/*.txt", read_file));
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
    {
        UpdatingSnapshots updating;
        ZEXPECT(snapshot("version_b", "update_mode_v"));
    }
    ZEXPECT(snapshot("version_b", "update_mode_v"));
    UpdatingSnapshots updating;
    ZEXPECT(snapshot("version_a", "update_mode_v"));
}

ZEST_CASE(second_unnamed_snapshot_fails) {
    ZEXPECT(snapshot("first unnamed value"));
    auto second = snapshot("second unnamed attempt");
    ZEXPECT(!second.held);
    ZEXPECT(contains(second.explain(), "one unnamed snapshot"));
}

ZEST_CASE(snapshot_without_a_test_fails) {
    detail::reset_snapshot_context("", "");
    auto taken = snapshot("value", "no_context");
    ZEXPECT(!taken.held);
    ZEXPECT(contains(taken.explain(), "no running test"));
}

ZEST_CASE(invalid_glob_fails) {
    auto taken = snapshot_glob(".", "[unclosed", [](std::string_view) { return std::string{}; });
    ZEXPECT(!taken.held);
    ZEXPECT(contains(taken.explain(), "invalid glob pattern"));
}

ZEST_CASE(glob_of_a_missing_directory_fails) {
    auto taken = snapshot_glob("tests/zest/system/missing", "**/*.txt", [](std::string_view) {
        return std::string{};
    });
    ZEXPECT(!taken.held);
    ZEXPECT(contains(taken.explain(), "cannot list"));
}

ZEST_CASE(null_text_fails) {
    const char* null = nullptr;
    auto taken = snapshot(null, "null");
    ZEXPECT(!taken.held);
    ZEXPECT(contains(taken.explain(), "null pointer"));
}

ZEST_CASE(glob_without_matches_fails) {
    auto taken =
        snapshot_glob(fixtures_dir(), "**/*.xyz", [](std::string_view) { return std::string{}; });
    ZEXPECT(!taken.held);
    ZEXPECT(contains(taken.explain(), "no file under"));
}

ZEST_CASE(body_with_separator) {
    std::string content = "before\n---\nafter";
    ZEXPECT(snapshot(content, "body_with_separator"));
}

ZEST_CASE(unsafe_name_fails) {
    for(auto name: {"bad/name", "bad:name"}) {
        ZEST_CONTEXT("name {}", name);
        auto taken = snapshot("value", name);
        ZEXPECT(!taken.held);
        ZEXPECT(contains(taken.explain(), "holds a character file names cannot"));
    }
}

ZEST_CASE(glob_without_a_test_fails) {
    detail::reset_snapshot_context("", "");
    auto taken =
        snapshot_glob(fixtures_dir(), "**/*.txt", [](std::string_view) { return std::string{}; });
    ZEXPECT(!taken.held);
    ZEXPECT(contains(taken.explain(), "no running test"));
}

// Snapshots are the running test's, whichever thread takes them.
ZEST_CASE(other_thread_takes_snapshots) {
    std::thread([] { ZEXPECT(snapshot("from a thread", "thread")); }).join();
}

};  // ZEST_SUITE(zest_snapshot)

}  // namespace

}  // namespace kota::zest
