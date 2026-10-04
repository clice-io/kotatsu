#include <array>
#include <forward_list>
#include <memory>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

#include "support/harness/tracked.h"
#include "kota/zest/zest.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

ZEST_SUITE(support_small_vector_append) {

ZEST_CASE(push_back_copies_an_lvalue) {
    small_vector<std::string, 2> v;
    std::string greeting = "hello";
    v.push_back(greeting);
    ZEXPECT(v == std::vector<std::string>{"hello"});
    ZEXPECT(greeting == "hello");
}

ZEST_CASE(push_back_moves_an_rvalue) {
    test::Census census;
    small_vector<test::Tracked, 2> v;
    test::Tracked value(4);
    v.push_back(std::move(value));
    ZEXPECT(test::values(v) == std::vector{4});
    ZEXPECT(census.moves == 1);
    ZEXPECT(census.copies == 0);
    ZEXPECT(value.value() == -1);
}

ZEST_CASE(push_back_past_the_inline_buffer_moves_to_the_heap) {
    small_vector<int, 2> v = {1, 2};
    v.push_back(3);
    ZEXPECT(v == std::vector{1, 2, 3});
    ZEXPECT(!v.inlined());
    ZEXPECT(v.capacity() == 4U);
}

ZEST_CASE(push_back_keeps_every_value_across_growth) {
    small_vector<int, 4> v;
    std::vector<int> expected;
    for(int i = 0; i < 1000; ++i) {
        v.push_back(i);
        expected.push_back(i);
    }
    ZEXPECT(v == expected);
}

ZEST_CASE(push_back_of_move_only_elements_grows) {
    small_vector<std::unique_ptr<int>, 2> v;
    for(int i = 0; i < 5; ++i) {
        v.push_back(std::make_unique<int>(i));
    }
    ZASSERT(v.size() == 5U);
    for(int i = 0; i < 5; ++i) {
        ZEST_CONTEXT("element {}", i);
        ZASSERT(v[i] != nullptr);
        ZEXPECT(*v[i] == i);
    }
}

ZEST_CASE(emplace_back_constructs_in_place) {
    small_vector<std::string, 2> v;
    v.emplace_back(3, 'x');
    auto& last = v.emplace_back("more");
    ZEXPECT(v == std::vector<std::string>{"xxx", "more"});
    ZEXPECT(&last == &v.back());
}

ZEST_CASE(emplace_back_growing_returns_the_new_element) {
    small_vector<std::string, 1> v = {"a"};
    auto& last = v.emplace_back("b");
    ZEXPECT(&last == &v[1]);
    ZEXPECT(last == "b");
}

ZEST_CASE(append_count_copies_the_value) {
    small_vector<int, 2> v = {1};
    v.append(3, 7);
    ZEXPECT(v == std::vector{1, 7, 7, 7});
    v.append(0, 5);
    ZEXPECT(v == std::vector{1, 7, 7, 7});
}

ZEST_CASE(append_contiguous_range_copies_it) {
    small_vector<int, 3> v = {1, 2, 3};
    v.append(std::array{4, 5, 6});
    ZEXPECT(v == std::vector{1, 2, 3, 4, 5, 6});
}

ZEST_CASE(append_forward_range_of_unknown_size) {
    std::forward_list<std::string> source = {"b", "c"};
    small_vector<std::string, 2> v = {"a"};
    v.append(source);
    ZEXPECT(v == std::vector<std::string>{"a", "b", "c"});
}

ZEST_CASE(append_input_range_reads_it_once) {
    std::istringstream text("4 5 6");
    small_vector<int, 2> v = {1};
    v.append(std::views::istream<int>(text));
    ZEXPECT(v == std::vector{1, 4, 5, 6});
}

ZEST_CASE(append_converting_range_converts_each) {
    std::array<const char*, 2> words = {"x", "y"};
    small_vector<std::string, 1> v;
    v.append(words);
    ZEXPECT(v == std::vector<std::string>{"x", "y"});
}

ZEST_CASE(append_initializer_list_copies_it) {
    small_vector<int, 4> v = {1, 2};
    v.append({3, 4, 5});
    ZEXPECT(v == std::vector{1, 2, 3, 4, 5});
}

ZEST_CASE(append_vector_copies_it) {
    small_vector<int, 4> source = {4, 5};
    small_vector<int, 8> v = {1, 2, 3};
    v.append(source);
    ZEXPECT(v == std::vector{1, 2, 3, 4, 5});
    ZEXPECT(source == std::vector{4, 5});
}

ZEST_CASE(append_moved_vector_empties_it) {
    test::Census census;
    small_vector<test::Tracked, 4> source = {4, 5};
    small_vector<test::Tracked, 4> v = {1};
    const auto copies = census.copies;
    v.append(std::move(source));
    ZEXPECT(test::values(v) == std::vector{1, 4, 5});
    ZEXPECT(census.copies == copies);
    ZEXPECT(source.empty());
}

ZEST_CASE(append_moved_self_changes_nothing) {
    small_vector<int, 4> v = {1, 2};
    v.append(std::move(v));
    ZEXPECT(v == std::vector{1, 2});
}

};  // ZEST_SUITE(support_small_vector_append)

}  // namespace

}  // namespace kota
