#include <array>
#include <list>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

#include "support/harness/tracked.h"
#include "kota/zest/zest.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

ZEST_SUITE(support_small_vector_insert) {

ZEST_CASE(insert_one_shifts_the_rest) {
    small_vector<int, 4> v = {1, 3};
    auto it = v.insert(v.begin() + 1, 2);
    EXPECT(v == std::vector{1, 2, 3});
    ASSERT(it == v.begin() + 1);
    EXPECT(*it == 2);
}

ZEST_CASE(insert_one_at_either_end) {
    small_vector<int, 8> v = {1, 2};
    v.insert(v.begin(), 0);
    v.insert(v.end(), 3);
    EXPECT(v == std::vector{0, 1, 2, 3});
}

ZEST_CASE(insert_one_into_an_empty_vector) {
    small_vector<int, 4> v;
    auto it = v.insert(v.begin(), 42);
    EXPECT(v == std::vector{42});
    EXPECT(it == v.begin());
}

ZEST_CASE(insert_one_at_capacity_reallocates) {
    small_vector<std::string, 2> v = {"a", "c"};
    auto it = v.insert(v.begin() + 1, std::string("b"));
    EXPECT(v == std::vector<std::string>{"a", "b", "c"});
    EXPECT(!v.inlined());
    ASSERT(it == v.begin() + 1);
    EXPECT(*it == "b");
}

ZEST_CASE(insert_rvalue_moves_it) {
    test::Census census;
    small_vector<test::Tracked, 4> v = {1, 3};
    test::Tracked value(2);
    const auto copies = census.copies;
    v.insert(v.begin() + 1, std::move(value));
    EXPECT(test::values(v) == std::vector{1, 2, 3});
    EXPECT(census.copies == copies);
    EXPECT(value.value() == -1);
}

ZEST_CASE(insert_count_fewer_than_the_elements_after) {
    small_vector<int, 8> v = {1, 2, 3, 4};
    auto it = v.insert(v.begin() + 1, 2, 9);
    EXPECT(v == std::vector{1, 9, 9, 2, 3, 4});
    EXPECT(it == v.begin() + 1);
}

ZEST_CASE(insert_count_more_than_the_elements_after) {
    small_vector<int, 8> v = {1, 2, 3};
    auto it = v.insert(v.begin() + 2, 3, 9);
    EXPECT(v == std::vector{1, 2, 9, 9, 9, 3});
    EXPECT(it == v.begin() + 2);
}

ZEST_CASE(insert_count_reallocating) {
    small_vector<int, 2> v = {1, 5};
    v.insert(v.begin() + 1, 3, 9);
    EXPECT(v == std::vector{1, 9, 9, 9, 5});
}

ZEST_CASE(insert_count_of_zero_changes_nothing) {
    test::Census census;
    small_vector<test::Tracked, 4> v = {1, 2};
    const auto copies = census.copies;
    const auto moves = census.moves;
    auto it = v.insert(v.begin() + 1, 0, test::Tracked(9));
    EXPECT(test::values(v) == std::vector{1, 2});
    EXPECT(it == v.begin() + 1);
    EXPECT(census.copies == copies);
    EXPECT(census.moves == moves);
    EXPECT(census.assignments == 0);
}

ZEST_CASE(insert_range_fewer_than_the_elements_after) {
    small_vector<std::string, 8> v = {"a", "d", "e", "f"};
    v.insert(v.begin() + 1, std::array<std::string, 2>{"b", "c"});
    EXPECT(v == std::vector<std::string>{"a", "b", "c", "d", "e", "f"});
}

ZEST_CASE(insert_range_more_than_the_elements_after) {
    small_vector<std::string, 8> v = {"a", "b", "f"};
    v.insert(v.begin() + 2, std::array<std::string, 3>{"c", "d", "e"});
    EXPECT(v == std::vector<std::string>{"a", "b", "c", "d", "e", "f"});
}

ZEST_CASE(insert_range_reallocating) {
    small_vector<int, 4> v = {1, 5};
    v.insert(v.begin() + 1, std::array{2, 3, 4});
    v.insert(v.begin(), std::array{-1, 0});
    EXPECT(v == std::vector{-1, 0, 1, 2, 3, 4, 5});
}

ZEST_CASE(insert_range_that_is_not_random_access) {
    std::list<std::string> source = {"b", "c", "d"};
    small_vector<std::string, 8> v = {"a", "e"};
    v.insert(v.begin() + 1, source);
    EXPECT(v == std::vector<std::string>{"a", "b", "c", "d", "e"});

    small_vector<std::string, 8> w = {"a", "b", "c", "d", "e"};
    w.insert(w.begin() + 1, std::list<std::string>{"x"});
    EXPECT(w == std::vector<std::string>{"a", "x", "b", "c", "d", "e"});
}

ZEST_CASE(insert_input_range_inserts_each_as_it_comes) {
    std::istringstream text("2 3");
    small_vector<int, 2> v = {1, 4};
    auto it = v.insert(v.begin() + 1, std::views::istream<int>(text));
    EXPECT(v == std::vector{1, 2, 3, 4});
    EXPECT(it == v.begin() + 1);
}

ZEST_CASE(insert_empty_range_changes_nothing) {
    test::Census census;
    small_vector<test::Tracked, 4> v = {1, 2, 3};
    const auto moves = census.moves;
    auto it = v.insert(v.begin() + 1, std::vector<test::Tracked>{});
    EXPECT(it == v.begin() + 1);
    it = v.insert(v.begin(), std::ranges::subrange(v.begin(), v.begin()));
    EXPECT(it == v.begin());
    EXPECT(test::values(v) == std::vector{1, 2, 3});
    EXPECT(census.moves == moves);
    EXPECT(census.assignments == 0);
}

ZEST_CASE(insert_takes_a_const_iterator) {
    small_vector<int, 4> v = {1, 3};
    auto it = v.insert(v.cbegin() + 1, 2);
    EXPECT(it == v.begin() + 1);
    it = v.emplace(v.cend(), 4);
    EXPECT(it == v.begin() + 3);
    EXPECT(v == std::vector{1, 2, 3, 4});
}

ZEST_CASE(insert_of_an_element_that_is_a_range_inserts_it_whole) {
    small_vector<std::vector<int>, 2> v;
    std::vector<int> row = {5};
    v.insert(v.begin(), row);
    v.insert(v.end(), std::vector<int>{6, 7});
    ASSERT(v.size() == 2U);
    EXPECT(v[0] == std::vector{5});
    EXPECT(v[1] == std::vector{6, 7});
}

ZEST_CASE(insert_initializer_list_inserts_its_elements) {
    small_vector<int, 4> v = {1, 5};
    v.insert(v.begin() + 1, {2, 3, 4});
    EXPECT(v == std::vector{1, 2, 3, 4, 5});
}

ZEST_CASE(emplace_constructs_at_the_position) {
    small_vector<std::string, 4> v;
    v.emplace(v.begin(), "first");
    v.emplace(v.end(), "last");
    auto it = v.emplace(v.begin() + 1, 3, 'x');
    EXPECT(v == std::vector<std::string>{"first", "xxx", "last"});
    EXPECT(it == v.begin() + 1);
}

ZEST_CASE(insert_keeps_every_element_alive_once) {
    test::Census census;
    {
        small_vector<test::Tracked, 4> v = {1, 2, 3};
        v.insert(v.begin() + 1, 2, test::Tracked(8));
        v.insert(v.begin(), std::array<test::Tracked, 2>{5, 6});
        EXPECT(test::values(v) == std::vector{5, 6, 1, 8, 8, 2, 3});
        EXPECT(census.live == 7);
    }
    EXPECT(census.live == 0);
}

};  // ZEST_SUITE(support_small_vector_insert)

}  // namespace

}  // namespace kota
