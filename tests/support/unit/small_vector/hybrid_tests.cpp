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
    ZEXPECT(small == std::vector{1, 2, 3});
    ZEXPECT(large == std::vector{1, 2, 3});
    ZEXPECT(!small.inlined());
    ZEXPECT(large.inlined());
}

ZEST_CASE(cannot_be_copied_out_of_a_small_vector) {
    ZSTATIC_EXPECT(!std::is_copy_constructible_v<hybrid_vector<int>>);
    ZSTATIC_EXPECT(!std::is_move_constructible_v<hybrid_vector<int>>);
}

ZEST_CASE(copy_assignment_copies_the_elements) {
    test::Census census;
    {
        small_vector<test::Tracked, 1> a = {1, 2};
        small_vector<test::Tracked, 1> b = {3, 4, 5};
        hybrid_vector<test::Tracked>& target = a;
        const hybrid_vector<test::Tracked>& source = b;
        target = source;
        ZEXPECT(test::values(a) == std::vector{3, 4, 5});
        ZEXPECT(test::values(b) == std::vector{3, 4, 5});
        ZEXPECT(a.data() != b.data());
        ZEXPECT(census.live == 6);
    }
    ZEXPECT(census.live == 0);
}

ZEST_CASE(move_assignment_takes_the_allocation) {
    small_vector<std::string, 1> a = {"a"};
    small_vector<std::string, 2> b = {"b", "c", "d"};
    const auto* allocation = b.data();
    hybrid_vector<std::string>& target = a;
    hybrid_vector<std::string>& source = b;
    target = std::move(source);
    ZEXPECT(a == std::vector<std::string>{"b", "c", "d"});
    ZEXPECT(a.data() == allocation);
    // Through the base, the source's inline capacity is unknown: it is left with none, and
    // grows again from nothing.
    ZEXPECT(b.empty());
    ZEXPECT(b.capacity() == 0U);
    b.push_back("e");
    ZEXPECT(b == std::vector<std::string>{"e"});
    b.shrink_to_fit();
    ZEXPECT(b.inlined());
    ZEXPECT(b.capacity() == 2U);
}

ZEST_CASE(move_assignment_of_inline_elements_moves_them) {
    small_vector<int, 4> target = {1, 2, 3};
    small_vector<int, 2> source = {7, 8};
    target.assign(static_cast<hybrid_vector<int>&&>(source));
    ZEXPECT(target == std::vector{7, 8});
    ZEXPECT(target.inlined());
    ZEXPECT(source.empty());
}

ZEST_CASE(move_assignment_of_an_empty_vector_keeps_the_inline_buffer) {
    small_vector<int, 4> target = {1, 2, 3};
    small_vector<int, 2> source;
    target.assign(static_cast<hybrid_vector<int>&&>(source));
    ZEXPECT(target.empty());
    ZEXPECT(target.capacity() == 4U);
    target.push_back(42);
    ZEXPECT(target.inlined());
}

ZEST_CASE(small_vector_constructs_from_one) {
    small_vector<int, 2> source = {1, 2, 3};
    const auto* allocation = source.data();
    hybrid_vector<int>& erased = source;
    small_vector<int, 8> copy(static_cast<const hybrid_vector<int>&>(erased));
    ZEXPECT(copy == std::vector{1, 2, 3});
    small_vector<int, 8> moved(std::move(erased));
    ZEXPECT(moved == std::vector{1, 2, 3});
    ZEXPECT(moved.data() == allocation);
    // Through the base, the source's inline capacity is unknown: it is left with none.
    ZEXPECT(source.empty());
    ZEXPECT(source.capacity() == 0U);
    source.push_back(4);
    ZEXPECT(source == std::vector{4});
}

ZEST_CASE(move_assignment_gives_back_the_inline_buffer_a_base_move_took) {
    small_vector<int, 2> source = {1, 2, 3};
    hybrid_vector<int>& erased = source;
    small_vector<int, 8> taken(std::move(erased));
    ZASSERT(source.capacity() == 0U);
    source = small_vector<int, 2>{7, 8};
    ZEXPECT(source == std::vector{7, 8});
    ZEXPECT(source.inlined());
    ZEXPECT(source.capacity() == 2U);
}

ZEST_CASE(assign_copies_one) {
    small_vector<int, 2> source = {1, 2, 3};
    small_vector<int, 8> target = {9};
    hybrid_vector<int>& erased = target;
    erased.assign(static_cast<const hybrid_vector<int>&>(source));
    ZEXPECT(target == std::vector{1, 2, 3});
    ZEXPECT(target.inlined());
    ZEXPECT(source == std::vector{1, 2, 3});
}

ZEST_CASE(small_vector_assigns_from_one) {
    small_vector<int, 2> source = {1, 2, 3};
    const auto* allocation = source.data();
    hybrid_vector<int>& erased = source;
    small_vector<int, 8> target = {9};
    target = static_cast<const hybrid_vector<int>&>(erased);
    ZEXPECT(target == std::vector{1, 2, 3});
    small_vector<int, 1> taken = {7};
    taken = std::move(erased);
    ZEXPECT(taken == std::vector{1, 2, 3});
    ZEXPECT(taken.data() == allocation);
    ZEXPECT(source.empty());
}

};  // ZEST_SUITE(support_small_vector_hybrid)

}  // namespace

}  // namespace kota
