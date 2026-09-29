#include <array>
#include <list>
#include <memory>
#include <ranges>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "support/harness/tracked.h"
#include "kota/zest/zest.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

ZEST_SUITE(support_small_vector_assign) {

ZEST_CASE(copy_assignment_to_itself_changes_nothing) {
    small_vector<int, 4> v = {1, 2, 3};
    const auto& same = v;
    v = same;
    EXPECT(v == std::vector{1, 2, 3});
}

ZEST_CASE(copy_assignment_reuses_elements_and_buffer) {
    test::Census census;
    small_vector<test::Tracked, 2> target = {1, 2, 3, 4};
    small_vector<test::Tracked, 2> source = {5, 6};
    const auto* allocation = target.data();
    const auto copies = census.copies;
    target = source;
    EXPECT(test::values(target) == std::vector{5, 6});
    EXPECT(target.data() == allocation);
    EXPECT(census.copies == copies);
    EXPECT(census.assignments == 2);
    EXPECT(census.live == 4);
}

ZEST_CASE(copy_assignment_constructs_what_it_lacks) {
    test::Census census;
    small_vector<test::Tracked, 4> target = {1};
    small_vector<test::Tracked, 4> source = {5, 6, 7};
    const auto copies = census.copies;
    target = source;
    EXPECT(test::values(target) == std::vector{5, 6, 7});
    EXPECT(census.assignments == 1);
    EXPECT(census.copies - copies == 2);
    EXPECT(target.inlined());
}

ZEST_CASE(copy_assignment_grows_past_the_capacity) {
    small_vector<int, 2> target = {1};
    small_vector<int, 2> source = {4, 5, 6, 7};
    target = source;
    EXPECT(target == std::vector{4, 5, 6, 7});
    EXPECT(!target.inlined());
    EXPECT(source == std::vector{4, 5, 6, 7});
}

ZEST_CASE(copy_assignment_across_capacities) {
    small_vector<int, 2> source = {1, 2, 3};
    small_vector<int, 8> target = {9};
    target = source;
    EXPECT(target == std::vector{1, 2, 3});
    EXPECT(target.inlined());
}

ZEST_CASE(move_assignment_to_itself_changes_nothing) {
    small_vector<int, 4> v = {1, 2, 3};
    auto& same = v;
    v = std::move(same);
    EXPECT(v == std::vector{1, 2, 3});
}

ZEST_CASE(move_assignment_takes_the_allocation) {
    small_vector<int, 2> target = {1};
    small_vector<int, 2> source = {3, 4, 5, 6};
    const auto* allocation = source.data();
    target = std::move(source);
    EXPECT(target == std::vector{3, 4, 5, 6});
    EXPECT(target.data() == allocation);
    EXPECT(source.empty());
    EXPECT(source.inlined());
    EXPECT(source.capacity() == 2U);
}

ZEST_CASE(move_assignment_frees_the_old_allocation) {
    test::Census census;
    {
        small_vector<test::Tracked, 1> target = {1, 2, 3};
        small_vector<test::Tracked, 1> source = {4, 5};
        target = std::move(source);
        EXPECT(test::values(target) == std::vector{4, 5});
        EXPECT(census.live == 2);
    }
    EXPECT(census.live == 0);
}

ZEST_CASE(move_assignment_of_inline_elements_moves_each) {
    test::Census census;
    small_vector<test::Tracked, 4> target = {1, 2, 3};
    small_vector<test::Tracked, 4> source = {7};
    const auto moves = census.moves;
    target = std::move(source);
    EXPECT(test::values(target) == std::vector{7});
    EXPECT(census.assignments == 1);
    EXPECT(census.moves == moves);
    EXPECT(census.live == 1);
    EXPECT(source.empty());
}

ZEST_CASE(move_assignment_of_move_only_elements_moves_them) {
    small_vector<std::unique_ptr<int>, 2> target;
    target.push_back(std::make_unique<int>(1));
    small_vector<std::unique_ptr<int>, 2> source;
    source.push_back(std::make_unique<int>(7));
    target = std::move(source);
    ASSERT(target.size() == 1U);
    ASSERT(target[0] != nullptr);
    EXPECT(*target[0] == 7);
    EXPECT(source.empty());
}

ZEST_CASE(move_assignment_across_capacities_leaves_the_source_its_buffer) {
    small_vector<int, 2> source = {1, 2, 3};
    small_vector<int, 8> target = {10};
    const auto* allocation = source.data();
    target = std::move(source);
    EXPECT(target == std::vector{1, 2, 3});
    EXPECT(target.data() == allocation);
    EXPECT(source.capacity() == 2U);
    source.push_back(7);
    EXPECT(source.inlined());
}

ZEST_CASE(assign_of_a_typed_vector_moves_it) {
    small_vector<int, 2> source = {1, 2};
    small_vector<int, 8> target = {9, 10, 11};
    target.assign(std::move(source));
    EXPECT(target == std::vector{1, 2});
    EXPECT(target.inlined());
    EXPECT(source.empty());
    EXPECT(source.capacity() == 2U);
}

ZEST_CASE(assign_count_grows_into_a_new_allocation) {
    small_vector<int, 2> v = {1};
    v.assign(5, 9);
    EXPECT(v == std::vector{9, 9, 9, 9, 9});
    EXPECT(!v.inlined());
}

ZEST_CASE(assign_count_within_capacity_keeps_the_buffer) {
    small_vector<int, 2> v = {1, 2, 3, 4};
    const auto* allocation = v.data();
    const auto capacity = v.capacity();
    v.assign(3, 9);
    EXPECT(v == std::vector{9, 9, 9});
    EXPECT(v.data() == allocation);
    EXPECT(v.capacity() == capacity);
    v.assign(4, 8);
    EXPECT(v == std::vector{8, 8, 8, 8});
}

ZEST_CASE(assign_count_destroys_the_surplus) {
    test::Census census;
    small_vector<test::Tracked, 4> v = {1, 2, 3, 4};
    v.assign(2, test::Tracked(7));
    EXPECT(test::values(v) == std::vector{7, 7});
    EXPECT(census.live == 2);
}

ZEST_CASE(assign_range_replaces_the_elements) {
    small_vector<int, 4> v = {1, 2, 3, 4, 5};
    v.assign(std::array{7, 8, 9});
    EXPECT(v == std::vector{7, 8, 9});

    std::list<int> list = {4, 5};
    v.assign(list);
    EXPECT(v == std::vector{4, 5});
}

ZEST_CASE(assign_input_range_reads_it_once) {
    std::istringstream text("3 2 1");
    small_vector<int, 2> v = {9};
    v.assign(std::views::istream<int>(text));
    EXPECT(v == std::vector{3, 2, 1});
}

ZEST_CASE(assign_initializer_list_replaces_the_elements) {
    small_vector<int, 4> v;
    v.assign({100, 200});
    EXPECT(v == std::vector{100, 200});
    v = {1, 2, 3, 4, 5};
    EXPECT(v == std::vector{1, 2, 3, 4, 5});
}

};  // ZEST_SUITE(support_small_vector_assign)

}  // namespace

}  // namespace kota
