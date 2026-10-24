#include <optional>
#include <string>
#include <string_view>

#include "kota/fs/path.h"
#include "kota/zest/zest.h"

namespace kota::fs::path {

namespace {

constexpr auto posix = Style::Posix;
constexpr auto windows = Style::Windows;

ZEST_SUITE(fs_path_relative) {

ZEST_CASE(relative_climbs_to_the_common_ancestor) {
    ZEXPECT(relative("/a/b/c", "/a/d", posix) == std::optional<std::string>("../b/c"));
    ZEXPECT(relative("/a/b", "/a/b", posix) == std::optional<std::string>("."));
    ZEXPECT(relative("/a/b", "/a/b/", posix) == std::optional<std::string>("."));
    ZEXPECT(relative("/a", "/a/b/c", posix) == std::optional<std::string>("../.."));
    ZEXPECT(relative("a/b", "a", posix) == std::optional<std::string>("b"));
    ZEXPECT(relative("C:\\a\\b", "C:\\a\\c", windows) == std::optional<std::string>("..\\b"));
}

ZEST_CASE(relative_compares_drive_letters_in_either_case) {
    ZEXPECT(relative("c:/a/b", "C:/a", windows) == std::optional<std::string>("b"));
    ZEXPECT(relative(R"(\\Server\Share\a)", R"(//server/share)", windows) ==
            std::optional<std::string>("a"));
}

ZEST_CASE(relative_fails_across_roots) {
    ZEXPECT(relative("/a", "b", posix) == std::nullopt);
    ZEXPECT(relative("a", "/b", posix) == std::nullopt);
    ZEXPECT(relative("C:\\a", "D:\\a", windows) == std::nullopt);
    ZEXPECT(relative("C:a", "C:\\a", windows) == std::nullopt);
    // Where `..` leads depends on what it climbs from.
    ZEXPECT(relative("a", "../b", posix) == std::nullopt);
}

ZEST_CASE(strip_prefix_takes_whole_components) {
    ZEXPECT(strip_prefix("/a/b/c", "/a/b", posix) == std::optional<std::string_view>("c"));
    ZEXPECT(strip_prefix("/a/b//c/d", "/a/b/", posix) == std::optional<std::string_view>("c/d"));
    ZEXPECT(strip_prefix("/a/b", "/a/b", posix) == std::optional<std::string_view>(""));
    ZEXPECT(strip_prefix("/a/bc", "/a/b", posix) == std::nullopt);
    ZEXPECT(strip_prefix("/a", "/a/b", posix) == std::nullopt);
    ZEXPECT(strip_prefix("c:\\a\\b", "C:/a", windows) == std::optional<std::string_view>("b"));
}

ZEST_CASE(is_within_holds_for_the_ancestor_itself) {
    ZEXPECT(is_within("/a/b", "/a", posix));
    ZEXPECT(is_within("/a", "/a", posix));
    ZEXPECT(is_within("/a/b", "/", posix));
    ZEXPECT(!is_within("/ab", "/a", posix));
    ZEXPECT(!is_within("a/b", "/a", posix));
}

};  // ZEST_SUITE(fs_path_relative)

}  // namespace

}  // namespace kota::fs::path
