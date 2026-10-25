#include <algorithm>
#include <array>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/support/small_string.h"
#include "kota/support/string_list.h"

namespace kota {

namespace {

using namespace std::literals;

using Strings = std::vector<std::string>;

/// The strings of `list`, as a function that takes one reads them.
Strings copied(StringListRef list) {
    Strings strings;
    for(auto s: list) {
        strings.emplace_back(s);
    }
    return strings;
}

ZEST_SUITE(support_string_list) {

ZEST_CASE(is_a_random_access_range) {
    ZSTATIC_EXPECT(std::ranges::random_access_range<StringListRef>);
    ZSTATIC_EXPECT(std::ranges::sized_range<StringListRef>);
    ZSTATIC_EXPECT(std::ranges::borrowed_range<StringListRef>);
}

ZEST_CASE(refers_to_a_list_of_any_string_type) {
    Strings strings{"a", "bc"};
    std::vector<std::string_view> views{"a", "bc"};
    std::array<const char*, 2> pointers{"a", "bc"};
    std::array<small_string<4>, 2> small_strings{small_string<4>("a"), small_string<4>("bc")};
    ZEXPECT(copied(strings) == Strings{"a", "bc"});
    ZEXPECT(copied(views) == Strings{"a", "bc"});
    ZEXPECT(copied(pointers) == Strings{"a", "bc"});
    ZEXPECT(copied(small_strings) == Strings{"a", "bc"});
}

ZEST_CASE(refers_to_a_braced_list) {
    std::string owned = "owned";
    ZEXPECT(copied({"a", owned, "c"sv}) == Strings{"a", "owned", "c"});
    ZEXPECT(copied({}) == Strings{});
}

ZEST_CASE(size_and_index_read_the_list) {
    Strings strings{"a", "bc", "def"};
    StringListRef list(strings);
    ZEXPECT(list.size() == 3u);
    ZEXPECT(!list.empty());
    ZEXPECT(list[0] == "a");
    ZEXPECT(list[2] == "def");
    // A view of each string, not a copy.
    ZEXPECT((list[1].data() == strings[1].data()));
    std::vector<std::string> none;
    ZEXPECT(StringListRef(none).empty());
}

ZEST_CASE(iterators_walk_it_both_ways) {
    std::vector<std::string_view> views{"a", "bc", "def"};
    StringListRef list(views);
    auto it = list.begin();
    ZEXPECT(*it == "a");
    ZEXPECT(it[2] == "def");
    ZEXPECT(*(it + 1) == "bc");
    it += 2;
    ZEXPECT(*it == "def");
    --it;
    ZEXPECT(*it == "bc");
    ZEXPECT(list.end() - list.begin() == 3);
    ZEXPECT((list.begin() < list.end()));
}

ZEST_CASE(ranges_algorithms_take_it) {
    std::array<const char*, 4> pointers{"b", "a", "ccc", "a"};
    StringListRef list(pointers);
    ZEXPECT(std::ranges::count(list, "a"sv) == 2);
    auto size = [](std::string_view s) {
        return s.size();
    };
    ZEXPECT(std::ranges::max(list, {}, size) == "ccc");
    auto found = std::ranges::find(list, "ccc"sv);
    ZEXPECT(found - list.begin() == 2);
    std::vector<std::string_view> reversed;
    std::ranges::copy(list | std::views::reverse, std::back_inserter(reversed));
    ZEXPECT(reversed == std::vector<std::string_view>{"a", "ccc", "a", "b"});
}

};  // ZEST_SUITE(support_string_list)

}  // namespace

}  // namespace kota
