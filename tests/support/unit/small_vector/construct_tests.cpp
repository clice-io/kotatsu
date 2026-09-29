#include <array>
#include <cstdint>
#include <list>
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

struct alignas(64) Wide {
    int value = 0;
};

struct Big {
    char bytes[100] = {};
};

/// Whether `v`'s elements lie inside the object itself.
template <typename Vector>
bool elements_inside(const Vector& v) {
    const auto* first = reinterpret_cast<const std::byte*>(&v);
    const auto* data = reinterpret_cast<const std::byte*>(v.data());
    return data >= first && data + v.capacity_in_bytes() <= first + sizeof(v);
}

ZEST_SUITE(support_small_vector_construct) {

ZEST_CASE(default_is_empty_and_inline) {
    small_vector<int, 4> v;
    EXPECT(v.empty());
    EXPECT(v.size() == 0U);
    EXPECT(v.capacity() == 4U);
    EXPECT(v.inline_capacity() == 4U);
    EXPECT(v.inlined());
    EXPECT(v.inlinable());
}

ZEST_CASE(count_value_initializes) {
    small_vector<int, 4> v(3);
    EXPECT(v == std::vector{0, 0, 0});
    EXPECT(v.inlined());
}

ZEST_CASE(count_and_value_copies_the_value) {
    small_vector<std::string, 2> v(5, std::string("x"));
    EXPECT(v == std::vector<std::string>(5, "x"));
    EXPECT(!v.inlined());
}

ZEST_CASE(generator_is_called_once_per_element) {
    int calls = 0;
    small_vector<int, 4> v(5, [&calls] { return calls++; });
    EXPECT(v == std::vector{0, 1, 2, 3, 4});
    EXPECT(calls == 5);
}

ZEST_CASE(range_copies_its_elements) {
    std::array<int, 5> source = {10, 20, 30, 40, 50};
    small_vector<int, 4> v(source);
    EXPECT(v == std::vector{10, 20, 30, 40, 50});
    EXPECT(!v.inlined());

    small_vector<int, 4> view(std::views::all(source) | std::views::take(3));
    EXPECT(view == std::vector{10, 20, 30});
    EXPECT(view.inlined());
}

ZEST_CASE(range_that_is_not_contiguous) {
    std::list<std::string> source = {"a", "b", "c"};
    small_vector<std::string, 2> v(source);
    EXPECT(v == std::vector<std::string>{"a", "b", "c"});
}

ZEST_CASE(input_range_is_read_once) {
    std::istringstream text("1 2 3 4 5");
    small_vector<int, 2> v(std::views::istream<int>(text));
    EXPECT(v == std::vector{1, 2, 3, 4, 5});
}

ZEST_CASE(initializer_list_copies_its_elements) {
    small_vector<int, 4> v = {1, 2, 3, 4, 5};
    EXPECT(v == std::vector{1, 2, 3, 4, 5});
}

ZEST_CASE(deduction_takes_the_range_value_type) {
    std::array<std::uint8_t, 2> bytes = {1, 2};
    small_vector from_range(bytes);
    EXPECT(zest::type_eq<decltype(from_range)::value_type, std::uint8_t>());
    small_vector from_list{1.5, 2.5};
    EXPECT(zest::type_eq<decltype(from_list)::value_type, double>());
}

ZEST_CASE(pointers_to_a_second_base_are_converted) {
    struct First {
        int a = 1;
    };

    struct Second {
        int b = 2;
    };

    struct Both : First, Second {};

    Both object;
    std::array<Both*, 2> derived = {&object, &object};
    small_vector<Second*, 4> bases(derived);
    ASSERT(bases.size() == 2U);
    EXPECT(bases[0] == static_cast<Second*>(&object));
    EXPECT(bases[1]->b == 2);
}

ZEST_CASE(copy_keeps_inline_elements_inline) {
    small_vector<int, 4> source = {1, 2, 3};
    small_vector<int, 4> copy(source);
    EXPECT(copy == std::vector{1, 2, 3});
    EXPECT(copy.inlined());
    EXPECT(copy.data() != source.data());
}

ZEST_CASE(copy_of_a_heap_vector_allocates_its_own) {
    small_vector<int, 2> source = {1, 2, 3, 4, 5};
    small_vector<int, 2> copy(source);
    EXPECT(copy == std::vector{1, 2, 3, 4, 5});
    EXPECT(!copy.inlined());
    EXPECT(copy.data() != source.data());
}

ZEST_CASE(copy_across_capacities_fits_the_new_one) {
    small_vector<int, 2> source = {1, 2, 3};
    small_vector<int, 8> copy(source);
    EXPECT(copy == std::vector{1, 2, 3});
    EXPECT(copy.inlined());
}

ZEST_CASE(move_takes_the_allocation) {
    small_vector<int, 2> source = {1, 2, 3, 4};
    const auto* allocation = source.data();
    small_vector<int, 2> moved(std::move(source));
    EXPECT(moved == std::vector{1, 2, 3, 4});
    EXPECT(moved.data() == allocation);
    EXPECT(source.empty());
    EXPECT(source.inlined());
    EXPECT(source.capacity() == 2U);
}

ZEST_CASE(move_of_inline_elements_moves_each) {
    test::Census census;
    small_vector<test::Tracked, 4> source = {1, 2, 3};
    const auto moves = census.moves;
    small_vector<test::Tracked, 4> moved(std::move(source));
    EXPECT(test::values(moved) == std::vector{1, 2, 3});
    EXPECT(census.moves - moves == 3);
    EXPECT(source.empty());
}

ZEST_CASE(move_of_move_only_elements_moves_them) {
    small_vector<std::unique_ptr<int>, 2> source;
    source.push_back(std::make_unique<int>(1));
    small_vector<std::unique_ptr<int>, 2> inline_moved(std::move(source));
    ASSERT(inline_moved.size() == 1U);
    EXPECT(*inline_moved[0] == 1);
    source.push_back(std::make_unique<int>(2));
    source.push_back(std::make_unique<int>(3));
    source.push_back(std::make_unique<int>(4));
    small_vector<std::unique_ptr<int>, 2> heap_moved(std::move(source));
    ASSERT(heap_moved.size() == 3U);
    EXPECT(*heap_moved[2] == 4);
    EXPECT(source.empty());
}

ZEST_CASE(move_of_an_empty_allocation_takes_it) {
    small_vector<int, 2> source;
    source.reserve(16);
    const auto* allocation = source.data();
    small_vector<int, 2> moved(std::move(source));
    EXPECT(moved.data() == allocation);
    EXPECT(moved.capacity() >= 16U);
    EXPECT(source.inlined());
}

ZEST_CASE(move_across_capacities_leaves_the_source_its_buffer) {
    small_vector<int, 2> source = {1, 2, 3};
    const auto* allocation = source.data();
    small_vector<int, 8> moved(std::move(source));
    EXPECT(moved == std::vector{1, 2, 3});
    EXPECT(moved.data() == allocation);
    EXPECT(source.capacity() == 2U);
    source.push_back(9);
    EXPECT(source.inlined());
    EXPECT(source == std::vector{9});
}

ZEST_CASE(from_raw_parts_adopts_the_buffer) {
    auto* buffer = mem::allocate<int>(8);
    for(int i = 0; i < 3; ++i) {
        std::construct_at(buffer + i, i + 1);
    }
    auto v = small_vector<int, 2>::from_raw_parts(buffer, 3, 8);
    EXPECT(v == std::vector{1, 2, 3});
    EXPECT(v.data() == buffer);
    EXPECT(v.capacity() == 8U);
    EXPECT(!v.inlined());
}

ZEST_CASE(from_raw_parts_of_nothing_is_empty) {
    auto v = small_vector<int, 2>::from_raw_parts(nullptr, 0, 0);
    EXPECT(v.empty());
    EXPECT(v.inlined());
}

ZEST_CASE(vector_has_no_inline_buffer) {
    vector<int> v;
    EXPECT(v.inline_capacity() == 0U);
    EXPECT(v.capacity() == 0U);
    EXPECT(v.inlined());
    v.push_back(1);
    EXPECT(!v.inlined());
    EXPECT(v == std::vector{1});
}

ZEST_CASE(default_inline_capacity_fills_64_bytes) {
    STATIC_EXPECT(sizeof(small_vector<int>) <= 64U);
    STATIC_EXPECT(small_vector<int>::inline_capacity_v >= 1U);
    // An element larger than the budget still gets one inline slot.
    STATIC_EXPECT(small_vector<Big>::inline_capacity_v == 1U);
}

ZEST_CASE(inline_elements_live_inside_the_object) {
    small_vector<int, 4> v = {1, 2};
    EXPECT(elements_inside(v));

    small_vector<std::uint8_t, 3> bytes = {1, 2, 3};
    EXPECT(elements_inside(bytes));
}

ZEST_CASE(over_aligned_elements_are_aligned_inline_and_on_the_heap) {
    small_vector<Wide, 2> v(2);
    EXPECT(elements_inside(v));
    EXPECT(reinterpret_cast<std::uintptr_t>(v.data()) % 64 == 0U);
    v.resize(5);
    ASSERT(!v.inlined());
    EXPECT(reinterpret_cast<std::uintptr_t>(v.data()) % 64 == 0U);
}

};  // ZEST_SUITE(support_small_vector_construct)

}  // namespace

}  // namespace kota
