#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "kotatsu/zest/zest.h"
#include "kotatsu/support/small_string.h"
#include "kotatsu/fs/path.h"

namespace kotatsu::fs::path {

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

/// What resolve() writes.
std::string resolved(std::string_view p, std::string_view base, DotDot dot_dot, Style style) {
    std::string out;
    resolve(out, p, base, dot_dot, style);
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

ZEST_CASE(resolve_joins_a_relative_path_onto_the_base) {
    ZEXPECT(resolved("c", "/a/b", DotDot::Fold, posix) == "/a/b/c");
    ZEXPECT(resolved("../c", "/a/b", DotDot::Fold, posix) == "/a/c");
    ZEXPECT(resolved("./c/", "/a/b/", DotDot::Fold, posix) == "/a/b/c");
    ZEXPECT(resolved("", "/a/b", DotDot::Fold, posix) == "/a/b");
    ZEXPECT(resolved("c", "a", DotDot::Fold, posix) == "a/c");
    ZEXPECT(resolved("..\\c", "C:\\a\\b", DotDot::Fold, windows) == "C:\\a\\c");
}

ZEST_CASE(resolve_takes_an_absolute_path_alone) {
    ZEXPECT(resolved("/x/./y", "/a", DotDot::Fold, posix) == "/x/y");
    ZEXPECT(resolved("D:\\x", "C:\\a", DotDot::Fold, windows) == "D:\\x");
    // A rooted `\x` keeps the base's drive.
    ZEXPECT(resolved("\\x", "C:\\a", DotDot::Fold, windows) == "C:\\x");
}

ZEST_CASE(resolve_keeps_dot_dot_when_asked) {
    ZEXPECT(resolved("../c", "/a/b", DotDot::Keep, posix) == "/a/b/../c");
    ZEXPECT(resolved("./c", "/a/./b", DotDot::Keep, posix) == "/a/b/c");
}

ZEST_CASE(resolve_inverts_relative) {
    for(auto [p, base]: {
            std::pair{"/a/b/c", "/a/d"  },
            {"/a",     "/a/b/c"},
            {"/a/b",   "/a/b"  }
    }) {
        ZEST_CONTEXT("{} from {}", p, base);
        std::string way;
        ZASSERT(relative(way, p, base, posix));
        ZEXPECT(resolved(way, base, DotDot::Fold, posix) == p);
    }
}

ZEST_CASE(resolve_takes_a_path_that_views_its_buffer) {
    std::string out = "../c";
    resolve(out, out, "/a/b", DotDot::Fold, posix);
    ZEXPECT(out == "/a/c");
    out = "/a/b";
    resolve(out, "c", out, DotDot::Fold, posix);
    ZEXPECT(out == "/a/b/c");
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

}  // namespace kotatsu::fs::path
