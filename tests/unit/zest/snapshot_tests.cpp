#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "kota/zest/snapshot_json.h"
#include "kota/zest/zest.h"

namespace kota::zest {

namespace {

namespace fs = std::filesystem;

std::string read_file(std::string_view path) {
    std::ifstream file(std::string(path), std::ios::binary);
    if(!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file), {});
}

TEST_SUITE(snapshot) {

TEST_CASE(basic_named) {
    ASSERT_SNAPSHOT("hello snapshot", "basic_named");
}

TEST_CASE(unnamed) {
    ASSERT_SNAPSHOT("auto-named snapshot content");
}

TEST_CASE(multiple_named) {
    ASSERT_SNAPSHOT("first value", "multi_first");
    ASSERT_SNAPSHOT("second value", "multi_second");
    ASSERT_SNAPSHOT("third value", "multi_third");
}

TEST_CASE(multiline) {
    std::string content = "line one\nline two\nline three";
    ASSERT_SNAPSHOT(content, "multiline");
}

TEST_CASE(empty_string) {
    ASSERT_SNAPSHOT("", "empty_string");
}

TEST_CASE(special_chars) {
    ASSERT_SNAPSHOT("tabs\there\nnewlines\nand \"quotes\"", "special_chars");
}

TEST_CASE(json_vector) {
    auto vec = std::vector<int>{1, 2, 3};
    ASSERT_SNAPSHOT_JSON(vec, "json_vector");
}

TEST_CASE(json_map) {
    auto m = std::map<std::string, int>{
        {"alpha", 1},
        {"beta",  2}
    };
    ASSERT_SNAPSHOT_JSON(m, "json_map");
}

TEST_CASE(glob_fixtures) {
    ASSERT_SNAPSHOT_GLOB("fixtures/**/*.txt", read_file);
}

TEST_CASE(mismatch_detection, serial = true) {
    EXPECT_FALSE(check_snapshot("original value", "mismatch_detect"));
    auto result = check_snapshot("different value", "mismatch_detect");
    EXPECT_TRUE(result);
    EXPECT_FALSE(check_snapshot("original value", "mismatch_detect"));
}

TEST_CASE(update_mode, serial = true) {
    EXPECT_FALSE(check_snapshot("version_a", "update_mode_v"));
    set_update_snapshots(true);
    auto result = check_snapshot("version_b", "update_mode_v");
    set_update_snapshots(false);
    EXPECT_FALSE(result);
    auto result2 = check_snapshot("version_b", "update_mode_v");
    EXPECT_FALSE(result2);
    set_update_snapshots(true);
    EXPECT_FALSE(check_snapshot("version_a", "update_mode_v"));
    set_update_snapshots(false);
}

TEST_CASE(duplicate_unnamed_error) {
    auto r1 = check_snapshot("first unnamed value");
    EXPECT_FALSE(r1);
    auto r2 = check_snapshot("second unnamed attempt");
    EXPECT_TRUE(r2);
}

TEST_CASE(missing_context_error) {
    reset_snapshot_context("", "", "");
    auto result = check_snapshot("value", "no_context");
    EXPECT_TRUE(result);
}

TEST_CASE(invalid_glob_error) {
    auto result = check_snapshot_glob("[unclosed", [](std::string_view) { return std::string{}; });
    EXPECT_TRUE(result);
}

TEST_CASE(glob_no_matches) {
    auto result = check_snapshot_glob("nonexistent_dir/**/*.xyz",
                                      [](std::string_view) { return std::string{}; });
    EXPECT_TRUE(result);
}

TEST_CASE(body_with_separator) {
    std::string content = "before\n---\nafter";
    ASSERT_SNAPSHOT(content, "body_with_separator");
}

TEST_CASE(unsafe_name_chars) {
    auto r1 = check_snapshot("value", "bad/name");
    EXPECT_TRUE(r1);
    auto r2 = check_snapshot("value", "bad:name");
    EXPECT_TRUE(r2);
}

TEST_CASE(glob_empty_context) {
    reset_snapshot_context("", "", "");
    auto result =
        check_snapshot_glob("fixtures/**/*.txt", [](std::string_view) { return std::string{}; });
    EXPECT_TRUE(result);
}

};  // TEST_SUITE(snapshot)

}  // namespace

}  // namespace kota::zest
