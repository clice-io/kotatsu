#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "kota/zest/zest.h"

// The EXPECT_SNAPSHOT_JSON family names ::kota::codec::json in its expansion but
// does not include it; this suite is built even when the JSON codec is not.
#ifdef KOTA_TEST_HAS_JSON
#include "kota/codec/json/json.h"
#endif

namespace kota::zest {

namespace {

namespace fs = std::filesystem;

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
    ASSERT_SNAPSHOT("hello snapshot", "basic_named");
}

ZEST_CASE(unnamed) {
    ASSERT_SNAPSHOT("auto-named snapshot content");
}

ZEST_CASE(multiple_named) {
    ASSERT_SNAPSHOT("first value", "multi_first");
    ASSERT_SNAPSHOT("second value", "multi_second");
    ASSERT_SNAPSHOT("third value", "multi_third");
}

ZEST_CASE(multiline) {
    std::string content = "line one\nline two\nline three";
    ASSERT_SNAPSHOT(content, "multiline");
}

ZEST_CASE(empty_string) {
    ASSERT_SNAPSHOT("", "empty_string");
}

ZEST_CASE(special_chars) {
    ASSERT_SNAPSHOT("tabs\there\nnewlines\nand \"quotes\"", "special_chars");
}

#ifdef KOTA_TEST_HAS_JSON

ZEST_CASE(json_vector) {
    auto vec = std::vector<int>{1, 2, 3};
    ASSERT_SNAPSHOT_JSON(vec, "json_vector");
}

ZEST_CASE(json_map) {
    auto m = std::map<std::string, int>{
        {"alpha", 1},
        {"beta",  2}
    };
    ASSERT_SNAPSHOT_JSON(m, "json_map");
}

#endif

ZEST_CASE(glob_fixtures) {
    ASSERT_SNAPSHOT_GLOB(fixtures_dir(), "**/*.txt", read_file);
}

ZEST_CASE(mismatch_detection) {
    EXPECT(!check_snapshot("original value", "mismatch_detect"));
    auto result = check_snapshot("different value", "mismatch_detect");
    EXPECT(result);
    EXPECT(!check_snapshot("original value", "mismatch_detect"));
}

ZEST_CASE(update_mode) {
    EXPECT(!check_snapshot("version_a", "update_mode_v"));
    set_update_snapshots(true);
    auto result = check_snapshot("version_b", "update_mode_v");
    set_update_snapshots(false);
    EXPECT(!result);
    auto result2 = check_snapshot("version_b", "update_mode_v");
    EXPECT(!result2);
    set_update_snapshots(true);
    EXPECT(!check_snapshot("version_a", "update_mode_v"));
    set_update_snapshots(false);
}

ZEST_CASE(duplicate_unnamed_error) {
    auto r1 = check_snapshot("first unnamed value");
    EXPECT(!r1);
    auto r2 = check_snapshot("second unnamed attempt");
    EXPECT(r2);
}

ZEST_CASE(missing_context_error) {
    reset_snapshot_context("", "", "");
    auto result = check_snapshot("value", "no_context");
    EXPECT(result);
}

ZEST_CASE(invalid_glob_error) {
    auto result =
        check_snapshot_glob(".", "[unclosed", [](std::string_view) { return std::string{}; });
    EXPECT(result);
}

ZEST_CASE(glob_no_matches) {
    auto result = check_snapshot_glob("nonexistent_dir", "**/*.xyz", [](std::string_view) {
        return std::string{};
    });
    EXPECT(result);
}

ZEST_CASE(body_with_separator) {
    std::string content = "before\n---\nafter";
    ASSERT_SNAPSHOT(content, "body_with_separator");
}

ZEST_CASE(unsafe_name_chars) {
    auto r1 = check_snapshot("value", "bad/name");
    EXPECT(r1);
    auto r2 = check_snapshot("value", "bad:name");
    EXPECT(r2);
}

ZEST_CASE(glob_empty_context) {
    reset_snapshot_context("", "", "");
    auto result = check_snapshot_glob(fixtures_dir(), "**/*.txt", [](std::string_view) {
        return std::string{};
    });
    EXPECT(result);
}

};  // ZEST_SUITE(zest_snapshot)

}  // namespace

}  // namespace kota::zest
