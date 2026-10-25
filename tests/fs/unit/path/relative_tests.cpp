#include <optional>
#include <string>
#include <string_view>

#include "kota/zest/zest.h"
#include "kota/support/small_string.h"
#include "kota/fs/path.h"

namespace kota::fs::path {

namespace {

constexpr auto posix = Style::Posix;
constexpr auto windows = Style::Windows;

/// What relative() writes, or none when it cannot tell the path.
std::optional<std::string> relative_of(std::string_view p, std::string_view base, Style style) {
    std::string out;
    if(!relative(out, p, base, style)) {
        return std::nullopt;
    }
    return out;
}

ZEST_SUITE(fs_path_relative) {

ZEST_CASE(relative_climbs_to_the_common_ancestor) {
    ZEXPECT(relative_of("/a/b/c", "/a/d", posix) == std::optional<std::string>("../b/c"));
    ZEXPECT(relative_of("/a/b", "/a/b", posix) == std::optional<std::string>("."));
    ZEXPECT(relative_of("/a/b", "/a/b/", posix) == std::optional<std::string>("."));
    ZEXPECT(relative_of("/a", "/a/b/c", posix) == std::optional<std::string>("../.."));
    ZEXPECT(relative_of("a/b", "a", posix) == std::optional<std::string>("b"));
    ZEXPECT(relative_of("C:\\a\\b", "C:\\a\\c", windows) == std::optional<std::string>("..\\b"));
}

ZEST_CASE(relative_compares_drive_letters_in_either_case) {
    ZEXPECT(relative_of("c:/a/b", "C:/a", windows) == std::optional<std::string>("b"));
    ZEXPECT(relative_of(R"(\\Server\Share\a)", R"(//server/share)", windows) ==
            std::optional<std::string>("a"));
}

ZEST_CASE(relative_fails_across_roots) {
    ZEXPECT(relative_of("/a", "b", posix) == std::nullopt);
    ZEXPECT(relative_of("a", "/b", posix) == std::nullopt);
    ZEXPECT(relative_of("C:\\a", "D:\\a", windows) == std::nullopt);
    ZEXPECT(relative_of("C:a", "C:\\a", windows) == std::nullopt);
    std::string out = "kept";
    ZEXPECT(!relative(out, "C:\\a", "D:\\a", windows));
    ZEXPECT(out == "kept");
}

ZEST_CASE(relative_past_a_dot_dot_in_the_base_fails) {
    // Where `..` leads depends on what it climbs from.
    ZEXPECT(relative_of("a", "../b", posix) == std::nullopt);
    std::string out = "kept";
    ZEXPECT(!relative(out, "a/c", "a/../b", posix));
    ZEXPECT(out == "kept");
}

ZEST_CASE(relative_writes_over_what_the_buffer_held) {
    small_string<4> out("some old text");
    ZEXPECT(relative(out, "/a/b/c", "/a/d", posix));
    ZEXPECT(std::string_view(out) == "../b/c");
    ZEXPECT(relative(out, "/a", "/a", posix));
    ZEXPECT(std::string_view(out) == ".");
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
