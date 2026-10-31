#include <cstring>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kotatsu/zest/zest.h"
#include "kotatsu/support/string_list.h"
#include "kotatsu/support/string_list_ref.h"

namespace kotatsu {

namespace {

using namespace std::literals;

using Strings = std::vector<std::string>;

/// The strings `list` points to, as a C function reads them.
Strings pointed(const string_list& list) {
    auto pointers = list.c_strs();
    // The null after the last.
    ZASSERT(pointers.data()[pointers.size()] == nullptr);
    Strings strings;
    for(auto* pointer: pointers) {
        strings.emplace_back(pointer);
    }
    return strings;
}

/// The strings of `list`, as iterating it gives them.
Strings walked(const string_list& list) {
    Strings strings;
    for(auto string: list) {
        strings.emplace_back(string);
    }
    return strings;
}

/// The strings of `list`, as a function that takes a string_list_ref reads
/// them.
Strings referred(string_list_ref list) {
    Strings strings;
    for(auto string: list) {
        strings.emplace_back(string);
    }
    return strings;
}

ZEST_SUITE(support_string_list) {

ZEST_CASE(is_a_random_access_range_of_views) {
    ZSTATIC_EXPECT(std::ranges::random_access_range<string_list>);
    ZSTATIC_EXPECT(std::ranges::sized_range<string_list>);
    ZSTATIC_EXPECT(std::same_as<std::ranges::range_reference_t<string_list>, std::string_view>);
}

ZEST_CASE(holds_the_strings_of_a_range) {
    std::vector<std::string_view> views{"a", "", "bc"};
    string_list list(views);
    ZEXPECT(list.size() == 3u);
    ZEXPECT(!list.empty());
    ZEXPECT(list[0] == "a");
    ZEXPECT(list[1] == "");
    ZEXPECT(list[2] == "bc");
    ZEXPECT(walked(list) == Strings{"a", "", "bc"});
}

ZEST_CASE(takes_a_lazy_view) {
    string_list list(std::views::iota(0, 3) |
                     std::views::transform([](int i) { return i == 1 ? "one"sv : "other"sv; }));
    ZEXPECT(walked(list) == Strings{"other", "one", "other"});
}

ZEST_CASE(points_to_each_string_followed_by_a_nul) {
    string_list list(Strings{"-c", "a b.c", ""});
    ZEXPECT(pointed(list) == Strings{"-c", "a b.c", ""});
    ZEXPECT((list.c_strs()[1] == list[1].data()));
}

ZEST_CASE(an_empty_one_points_to_a_null) {
    string_list list;
    ZEXPECT(list.empty());
    ZEXPECT(list.c_strs().empty());
    ZEXPECT(pointed(list).empty());
}

// Pushing past what the buffer holds moves the strings, and the pointers
// follow.
ZEST_CASE(push_back_keeps_the_pointers_on_the_strings) {
    string_list list;
    Strings strings;
    for(int i = 0; i < 100; ++i) {
        strings.push_back(std::string(i % 7, 'x') + std::to_string(i));
        list.push_back(strings.back());
    }
    ZEXPECT(pointed(list) == strings);
    ZEXPECT(walked(list) == strings);
}

ZEST_CASE(push_back_takes_a_string_of_its_own) {
    string_list list(Strings{"first", "second"});
    for(int i = 0; i < 50; ++i) {
        list.push_back(list[i % 2]);
    }
    ZEXPECT(list.size() == 52u);
    ZEXPECT(list[50] == "first");
    ZEXPECT(list[51] == "second");
    ZEXPECT(pointed(list).back() == "second");
}

// A view keeps what follows a NUL; the C string ends there.
ZEST_CASE(keeps_a_nul_in_a_string) {
    string_list list(Strings{"a\0b"s});
    ZEXPECT(list[0] == "a\0b"sv);
    ZEXPECT(std::strlen(list.c_strs()[0]) == 1u);
}

ZEST_CASE(copies_and_moves_point_to_their_own_strings) {
    // Short enough for a string's own buffer, which a move does not keep.
    string_list list(Strings{"a", "b"});
    string_list copy(list);
    ZEXPECT(pointed(copy) == Strings{"a", "b"});
    ZEXPECT((copy.c_strs()[0] != list.c_strs()[0]));
    string_list moved(std::move(copy));
    ZEXPECT(pointed(moved) == Strings{"a", "b"});
    string_list assigned;
    assigned = moved;
    ZEXPECT(pointed(assigned) == Strings{"a", "b"});
    assigned = std::move(moved);
    ZEXPECT(pointed(assigned) == Strings{"a", "b"});
}

ZEST_CASE(a_moved_from_one_is_empty) {
    string_list list(Strings{"a"});
    string_list moved(std::move(list));
    ZEXPECT(list.empty());
    ZEXPECT(pointed(list).empty());
    list.push_back("b");
    ZEXPECT(pointed(list) == Strings{"b"});
}

ZEST_CASE(a_string_list_ref_refers_to_it) {
    string_list list(Strings{"a", "a\0b"s});
    string_list_ref ref = list;
    ZEXPECT(ref.size() == 2u);
    ZEXPECT(referred(list) == (Strings{"a", "a\0b"s}));
}

};  // ZEST_SUITE(support_string_list)

}  // namespace

}  // namespace kotatsu
