#include <string>
#include <utility>
#include <vector>

#include "support/harness/tracked.h"
#include "kota/zest/zest.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

ZEST_SUITE(support_small_vector_swap) {

ZEST_CASE(inline_vectors_exchange_elements) {
    small_vector<int, 4> a = {1, 2};
    small_vector<int, 4> b = {3, 4, 5};
    a.swap(b);
    ZEXPECT(a == std::vector{3, 4, 5});
    ZEXPECT(b == std::vector{1, 2});
}

ZEST_CASE(heap_vectors_exchange_allocations) {
    small_vector<int, 1> a = {1, 2};
    small_vector<int, 1> b = {3, 4, 5};
    const auto* a_allocation = a.data();
    const auto* b_allocation = b.data();
    a.swap(b);
    ZEXPECT(a == std::vector{3, 4, 5});
    ZEXPECT(b == std::vector{1, 2});
    ZEXPECT(a.data() == b_allocation);
    ZEXPECT(b.data() == a_allocation);
}

ZEST_CASE(inline_with_heap_exchanges_elements) {
    small_vector<int, 2> a = {1, 2};
    small_vector<int, 2> b = {3, 4, 5, 6};
    a.swap(b);
    ZEXPECT(a == std::vector{3, 4, 5, 6});
    ZEXPECT(b == std::vector{1, 2});
}

ZEST_CASE(across_capacities_exchanges_elements) {
    small_vector<int, 2> a = {1, 2, 3};
    small_vector<int, 6> b = {7, 8};
    a.swap(b);
    ZEXPECT(a == std::vector{7, 8});
    ZEXPECT(b == std::vector{1, 2, 3});
    ZEXPECT(b.inlined());
}

ZEST_CASE(with_an_empty_vector_moves_the_elements) {
    test::Census census;
    {
        small_vector<test::Tracked, 2> a;
        small_vector<test::Tracked, 2> b = {1, 2};
        a.swap(b);
        ZEXPECT(test::values(a) == std::vector{1, 2});
        ZEXPECT(b.empty());
        ZEXPECT(census.live == 2);
    }
    ZEXPECT(census.live == 0);
}

ZEST_CASE(with_itself_changes_nothing) {
    small_vector<int, 2> v = {1, 2, 3};
    v.swap(v);
    ZEXPECT(v == std::vector{1, 2, 3});
}

ZEST_CASE(inline_with_heap_copies_no_element) {
    test::Census census;
    {
        small_vector<test::Tracked, 2> a = {1, 2};
        small_vector<test::Tracked, 2> b = {3, 4, 5, 6};
        const auto copies = census.copies;
        a.swap(b);
        ZEXPECT(test::values(a) == std::vector{3, 4, 5, 6});
        ZEXPECT(test::values(b) == std::vector{1, 2});
        ZEXPECT(census.copies == copies);
        ZEXPECT(census.live == 6);
        b.swap(a);
        ZEXPECT(test::values(a) == std::vector{1, 2});
        ZEXPECT(test::values(b) == std::vector{3, 4, 5, 6});
        ZEXPECT(census.live == 6);
    }
    ZEXPECT(census.live == 0);
}

ZEST_CASE(heap_vectors_move_no_element) {
    test::Census census;
    {
        small_vector<test::Tracked, 1> a = {1, 2};
        small_vector<test::Tracked, 1> b = {3, 4, 5};
        const auto moves = census.moves;
        a.swap(b);
        ZEXPECT(test::values(a) == std::vector{3, 4, 5});
        ZEXPECT(test::values(b) == std::vector{1, 2});
        ZEXPECT(census.moves == moves);
        ZEXPECT(census.assignments == 0);
        ZEXPECT(census.live == 5);
    }
    ZEXPECT(census.live == 0);
}

ZEST_CASE(std_swap_exchanges_elements) {
    small_vector<std::string, 2> a = {"a"};
    small_vector<std::string, 2> b = {"b", "c", "d"};
    std::swap(a, b);
    ZEXPECT(a == std::vector<std::string>{"b", "c", "d"});
    ZEXPECT(b == std::vector<std::string>{"a"});
}

};  // ZEST_SUITE(support_small_vector_swap)

}  // namespace

}  // namespace kota
