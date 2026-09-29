#include <string>
#include <type_traits>
#include <vector>

#include "support/harness/tracked.h"
#include "kota/zest/zest.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

// hybrid_vector<T> is what code takes to work on a small_vector<T, N> of any N.

void append_three(hybrid_vector<int>& v) {
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
}

ZEST_SUITE(support_small_vector_hybrid) {

ZEST_CASE(works_on_any_inline_capacity) {
    small_vector<int, 1> small;
    small_vector<int, 8> large;
    append_three(small);
    append_three(large);
    EXPECT(small == std::vector{1, 2, 3});
    EXPECT(large == std::vector{1, 2, 3});
    EXPECT(!small.inlined());
    EXPECT(large.inlined());
}

ZEST_CASE(cannot_be_copied_out_of_a_small_vector) {
    STATIC_EXPECT(!std::is_copy_constructible_v<hybrid_vector<int>>);
    STATIC_EXPECT(!std::is_move_constructible_v<hybrid_vector<int>>);
}

ZEST_CASE(copy_assignment_copies_the_elements) {
    test::Census census;
    {
        small_vector<test::Tracked, 1> a = {1, 2};
        small_vector<test::Tracked, 1> b = {3, 4, 5};
        hybrid_vector<test::Tracked>& target = a;
        const hybrid_vector<test::Tracked>& source = b;
        target = source;
        EXPECT(test::values(a) == std::vector{3, 4, 5});
        EXPECT(test::values(b) == std::vector{3, 4, 5});
        EXPECT(a.data() != b.data());
        EXPECT(census.live == 6);
    }
    EXPECT(census.live == 0);
}

ZEST_CASE(move_assignment_takes_the_allocation) {
    small_vector<std::string, 1> a = {"a"};
    small_vector<std::string, 2> b = {"b", "c", "d"};
    const auto* allocation = b.data();
    hybrid_vector<std::string>& target = a;
    hybrid_vector<std::string>& source = b;
    target = std::move(source);
    EXPECT(a == std::vector<std::string>{"b", "c", "d"});
    EXPECT(a.data() == allocation);
    // Through the base, the source's inline capacity is unknown: it is left with none, and
    // grows again from nothing.
    EXPECT(b.empty());
    EXPECT(b.capacity() == 0U);
    b.push_back("e");
    EXPECT(b == std::vector<std::string>{"e"});
    b.shrink_to_fit();
    EXPECT(b.inlined());
    EXPECT(b.capacity() == 2U);
}

ZEST_CASE(move_assignment_of_inline_elements_moves_them) {
    small_vector<int, 4> target = {1, 2, 3};
    small_vector<int, 2> source = {7, 8};
    target.assign(static_cast<hybrid_vector<int>&&>(source));
    EXPECT(target == std::vector{7, 8});
    EXPECT(target.inlined());
    EXPECT(source.empty());
}

ZEST_CASE(move_assignment_of_an_empty_vector_keeps_the_inline_buffer) {
    small_vector<int, 4> target = {1, 2, 3};
    small_vector<int, 2> source;
    target.assign(static_cast<hybrid_vector<int>&&>(source));
    EXPECT(target.empty());
    EXPECT(target.capacity() == 4U);
    target.push_back(42);
    EXPECT(target.inlined());
}

ZEST_CASE(small_vector_constructs_from_one) {
    small_vector<int, 2> source = {1, 2, 3};
    const auto* allocation = source.data();
    hybrid_vector<int>& erased = source;
    small_vector<int, 8> copy(static_cast<const hybrid_vector<int>&>(erased));
    EXPECT(copy == std::vector{1, 2, 3});
    small_vector<int, 8> moved(std::move(erased));
    EXPECT(moved == std::vector{1, 2, 3});
    EXPECT(moved.data() == allocation);
}

ZEST_CASE(small_vector_assigns_from_one) {
    small_vector<int, 2> source = {1, 2, 3};
    const hybrid_vector<int>& erased = source;
    small_vector<int, 8> target = {9};
    target = erased;
    EXPECT(target == std::vector{1, 2, 3});
}

};  // ZEST_SUITE(support_small_vector_hybrid)

}  // namespace

}  // namespace kota
