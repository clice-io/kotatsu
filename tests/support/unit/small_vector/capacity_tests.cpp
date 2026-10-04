#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "support/harness/tracked.h"
#include "kota/zest/zest.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

ZEST_SUITE(support_small_vector_capacity) {

ZEST_CASE(reserve_past_the_inline_buffer_allocates) {
    small_vector<int, 4> v = {1, 2};
    v.reserve(100);
    ZEXPECT(v.capacity() >= 100U);
    ZEXPECT(!v.inlined());
    ZEXPECT(v == std::vector{1, 2});
}

ZEST_CASE(reserve_within_the_capacity_changes_nothing) {
    small_vector<int, 4> v = {1, 2};
    const auto* data = v.data();
    v.reserve(4);
    ZEXPECT(v.capacity() == 4U);
    ZEXPECT(v.data() == data);
}

ZEST_CASE(growth_doubles_the_capacity) {
    small_vector<int, 2> v = {1, 2};
    v.push_back(3);
    ZEXPECT(v.capacity() == 4U);
    v.append(2, 0);
    ZEXPECT(v.capacity() == 8U);
    v.append(10, 0);
    ZEXPECT(v.capacity() == 16U);
}

ZEST_CASE(resize_value_initializes_new_elements) {
    small_vector<int, 4> v = {1, 2};
    v.resize(5);
    ZEXPECT(v == std::vector{1, 2, 0, 0, 0});
}

ZEST_CASE(resize_to_fewer_destroys_the_rest) {
    test::Census census;
    small_vector<test::Tracked, 4> v = {1, 2, 3, 4, 5};
    v.resize(2);
    ZEXPECT(test::values(v) == std::vector{1, 2});
    ZEXPECT(census.live == 2);
}

ZEST_CASE(resize_with_a_value_copies_it) {
    small_vector<std::string, 1> v = {"a"};
    v.resize(3, "b");
    ZEXPECT(v == std::vector<std::string>{"a", "b", "b"});
    v.resize(1, "c");
    ZEXPECT(v == std::vector<std::string>{"a"});
}

ZEST_CASE(resize_for_overwrite_leaves_room_to_fill) {
    small_vector<int, 4> v = {1, 2};
    v.resize_for_overwrite(5);
    ZASSERT(v.size() == 5U);
    v[2] = 20;
    v[3] = 30;
    v[4] = 40;
    v.resize_for_overwrite(3);
    ZEXPECT(v == std::vector{1, 2, 20});
}

ZEST_CASE(shrink_to_fit_moves_back_inline) {
    small_vector<int, 4> v = {1, 2, 3};
    v.reserve(16);
    ZASSERT(!v.inlined());
    v.shrink_to_fit();
    ZEXPECT(v.inlined());
    ZEXPECT(v.capacity() == 4U);
    ZEXPECT(v == std::vector{1, 2, 3});
}

ZEST_CASE(shrink_to_fit_of_an_empty_allocation_frees_it) {
    small_vector<int, 4> v;
    v.reserve(16);
    v.shrink_to_fit();
    ZEXPECT(v.inlined());
    ZEXPECT(v.capacity() == 4U);
    v.push_back(42);
    ZEXPECT(v.inlined());
}

ZEST_CASE(shrink_to_fit_reallocates_to_the_size) {
    small_vector<int, 2> v;
    for(int i = 0; i < 100; ++i) {
        v.push_back(i);
    }
    v.resize(5);
    v.shrink_to_fit();
    ZEXPECT(v.capacity() == 5U);
    ZEXPECT(v == std::vector{0, 1, 2, 3, 4});
    const auto* data = v.data();
    v.shrink_to_fit();
    ZEXPECT(v.data() == data);
}

ZEST_CASE(shrink_to_fit_keeps_every_element_once) {
    test::Census census;
    {
        small_vector<test::Tracked, 2> v = {1, 2, 3, 4};
        v.pop_back_n(2);
        v.shrink_to_fit();
        ZEXPECT(v.inlined());
        ZEXPECT(test::values(v) == std::vector{1, 2});
        ZEXPECT(census.live == 2);
    }
    ZEXPECT(census.live == 0);
}

ZEST_CASE(shrink_to_fit_of_an_inline_vector_changes_nothing) {
    small_vector<int, 4> v = {1};
    const auto* data = v.data();
    v.shrink_to_fit();
    ZEXPECT(v.data() == data);
    ZEXPECT(v.capacity() == 4U);
}

ZEST_CASE(vector_shrinks_to_an_exact_allocation) {
    vector<int> v = {5, 4, 3, 2, 1};
    v.reserve(20);
    v.shrink_to_fit();
    ZEXPECT(v.capacity() == 5U);
    v.clear();
    v.shrink_to_fit();
    ZEXPECT(v.capacity() == 0U);
    ZEXPECT(v.inlined());
}

ZEST_CASE(inlinable_says_whether_the_elements_fit_inline) {
    small_vector<int, 3> v = {1, 2, 3};
    ZEXPECT(v.inlinable());
    v.push_back(4);
    ZEXPECT(!v.inlinable());
    v.resize(2);
    ZEXPECT(v.inlinable());
    ZEXPECT(!v.inlined());
}

ZEST_CASE(sizes_in_bytes) {
    small_vector<double, 2> v = {1.0, 2.0, 3.0};
    ZEXPECT(v.size_in_bytes() == 3 * sizeof(double));
    ZEXPECT(v.capacity_in_bytes() == v.capacity() * sizeof(double));
}

ZEST_CASE(max_size_is_bounded_by_the_size_field) {
    // Elements of 4 bytes or more keep their counts in 32 bits.
    small_vector<int, 1> ints;
    ZEXPECT(ints.max_size() <= std::numeric_limits<std::uint32_t>::max());
    // Smaller ones can outnumber that in a 64-bit address space.
    small_vector<char, 1> chars;
    if constexpr(sizeof(void*) >= 8) {
        ZEXPECT(chars.max_size() > std::numeric_limits<std::uint32_t>::max());
    }
}

};  // ZEST_SUITE(support_small_vector_capacity)

}  // namespace

}  // namespace kota
