#include <algorithm>
#include <iterator>
#include <ranges>
#include <string_view>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/fs/path.h"

namespace kota::fs::path {

namespace {

using namespace std::literals;

constexpr auto posix = Style::Posix;
constexpr auto windows = Style::Windows;

using Names = std::vector<std::string_view>;

/// What a view of a path gives, in order.
template <std::ranges::forward_range View>
Names collected(View view) {
    Names names;
    for(auto name: view) {
        names.push_back(name);
    }
    return names;
}

ZEST_SUITE(fs_path_iterate) {

ZEST_CASE(views_are_forward_ranges) {
    ZSTATIC_EXPECT(std::ranges::view<components_view>);
    ZSTATIC_EXPECT(std::ranges::forward_range<components_view>);
    ZSTATIC_EXPECT(std::ranges::view<ancestors_view>);
    ZSTATIC_EXPECT(std::ranges::forward_range<ancestors_view>);
}

ZEST_CASE(components_are_the_names_after_the_root) {
    ZEXPECT(collected(components("/a//b/./c/", posix)) == Names{"a", "b", ".", "c"});
    ZEXPECT(collected(components("a\\b", posix)) == Names{"a\\b"});
    ZEXPECT(collected(components(R"(C:\x\y)", windows)) == Names{"x", "y"});
    ZEXPECT(collected(components("C:\\a/b", windows)) == Names{"a", "b"});
    ZEXPECT(collected(components("C:a", windows)) == Names{"a"});
    ZEXPECT(collected(components(R"(\\server\share\a)", windows)) == Names{"a"});
    // Only `\` separates past `\\?\`.
    ZEXPECT(collected(components(R"(\\?\C:\a/b\..)", windows)) == Names{"a/b", ".."});
    ZEXPECT(collected(components(R"(\\?\UNC\server\share\a\b)", windows)) == Names{"a", "b"});
    ZEXPECT(components("/", posix).empty());
    ZEXPECT(components("", posix).empty());
}

ZEST_CASE(components_of_a_root_alone_are_none) {
    ZEXPECT(components(R"(C:\)", windows).empty());
    ZEXPECT(components("C:", windows).empty());
    ZEXPECT(components(R"(\\server\share\)", windows).empty());
}

ZEST_CASE(components_view_the_path_they_read) {
    std::string_view p = "/usr//include/c++";
    auto names = components(p, posix);
    ZEXPECT(std::ranges::distance(names) == 3);
    ZEXPECT(names.front() == "usr");
    auto found = std::ranges::find(names, "include"sv);
    ZASSERT((found != names.end()));
    ZEXPECT((*found).data() - p.data() == 6);
}

ZEST_CASE(ancestors_walk_up_from_the_path_itself) {
    ZEXPECT(collected(ancestors("a/b/c", posix)) == Names{"a/b/c", "a/b", "a"});
    ZEXPECT(collected(ancestors("a/b/", posix)) == Names{"a/b/", "a"});
    ZEXPECT(collected(ancestors("/a", posix)) == Names{"/a", "/"});
    ZEXPECT(collected(ancestors("/", posix)) == Names{"/"});
    ZEXPECT(collected(ancestors("a", posix)) == Names{"a"});
    ZEXPECT(collected(ancestors(R"(C:\a)", windows)) == Names{R"(C:\a)", R"(C:\)"});
    ZEXPECT(collected(ancestors(R"(C:\)", windows)) == Names{R"(C:\)"});
    ZEXPECT(collected(ancestors("C:a", windows)) == Names{"C:a", "C:"});
    ZEXPECT(collected(ancestors(R"(\\server\share\a)", windows)) ==
            Names{R"(\\server\share\a)", R"(\\server\share\)"});
    ZEXPECT(ancestors("", posix).empty());
}

ZEST_CASE(ancestors_find_the_nearest_match) {
    auto chain = ancestors("/src/build/obj/a.o", posix);
    auto found = std::ranges::find_if(chain, [](std::string_view p) {
        return filename(p, posix) == "build";
    });
    ZASSERT((found != chain.end()));
    ZEXPECT(*found == "/src/build");
}

};  // ZEST_SUITE(fs_path_iterate)

}  // namespace

}  // namespace kota::fs::path
