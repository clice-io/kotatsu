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

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // padded to its alignment, which is the point
#endif
struct alignas(64) Wide {
    int value = 0;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

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
    ZEXPECT(v.empty());
    ZEXPECT(v.size() == 0U);
    ZEXPECT(v.capacity() == 4U);
    ZEXPECT(v.inline_capacity() == 4U);
    ZEXPECT(v.inlined());
    ZEXPECT(v.inlinable());
}

ZEST_CASE(count_value_initializes) {
    small_vector<int, 4> v(3);
    ZEXPECT(v == std::vector{0, 0, 0});
    ZEXPECT(v.inlined());
}

ZEST_CASE(count_and_value_copies_the_value) {
    small_vector<std::string, 2> v(5, std::string("x"));
    ZEXPECT(v == std::vector<std::string>(5, "x"));
    ZEXPECT(!v.inlined());
}

ZEST_CASE(generator_is_called_once_per_element) {
    int calls = 0;
    small_vector<int, 4> v(5, [&calls] { return calls++; });
    ZEXPECT(v == std::vector{0, 1, 2, 3, 4});
    ZEXPECT(calls == 5);
}

ZEST_CASE(range_copies_its_elements) {
    std::array<int, 5> source = {10, 20, 30, 40, 50};
    small_vector<int, 4> v(source);
    ZEXPECT(v == std::vector{10, 20, 30, 40, 50});
    ZEXPECT(!v.inlined());

    small_vector<int, 4> view(std::views::all(source) | std::views::take(3));
    ZEXPECT(view == std::vector{10, 20, 30});
    ZEXPECT(view.inlined());
}

ZEST_CASE(range_that_is_not_contiguous) {
    std::list<std::string> source = {"a", "b", "c"};
    small_vector<std::string, 2> v(source);
    ZEXPECT(v == std::vector<std::string>{"a", "b", "c"});
}

ZEST_CASE(input_range_is_read_once) {
    std::istringstream text("1 2 3 4 5");
    small_vector<int, 2> v(std::views::istream<int>(text));
    ZEXPECT(v == std::vector{1, 2, 3, 4, 5});
}

ZEST_CASE(initializer_list_copies_its_elements) {
    small_vector<int, 4> v = {1, 2, 3, 4, 5};
    ZEXPECT(v == std::vector{1, 2, 3, 4, 5});
}

ZEST_CASE(deduction_takes_the_range_value_type) {
    std::array<std::uint8_t, 2> bytes = {1, 2};
    small_vector from_range(bytes);
    ZEXPECT(zest::type_eq<decltype(from_range)::value_type, std::uint8_t>());
    small_vector from_list{1.5, 2.5};
    ZEXPECT(zest::type_eq<decltype(from_list)::value_type, double>());
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
    ZASSERT(bases.size() == 2U);
    ZEXPECT(bases[0] == static_cast<Second*>(&object));
    ZEXPECT(bases[1]->b == 2);
}

ZEST_CASE(copy_keeps_inline_elements_inline) {
    small_vector<int, 4> source = {1, 2, 3};
    small_vector<int, 4> copy(source);
    ZEXPECT(copy == std::vector{1, 2, 3});
    ZEXPECT(copy.inlined());
    ZEXPECT(copy.data() != source.data());
}

ZEST_CASE(copy_of_a_heap_vector_allocates_its_own) {
    small_vector<int, 2> source = {1, 2, 3, 4, 5};
    small_vector<int, 2> copy(source);
    ZEXPECT(copy == std::vector{1, 2, 3, 4, 5});
    ZEXPECT(!copy.inlined());
    ZEXPECT(copy.data() != source.data());
}

ZEST_CASE(copy_across_capacities_fits_the_new_one) {
    small_vector<int, 2> source = {1, 2, 3};
    small_vector<int, 8> copy(source);
    ZEXPECT(copy == std::vector{1, 2, 3});
    ZEXPECT(copy.inlined());
}

ZEST_CASE(move_takes_the_allocation) {
    small_vector<int, 2> source = {1, 2, 3, 4};
    const auto* allocation = source.data();
    small_vector<int, 2> moved(std::move(source));
    ZEXPECT(moved == std::vector{1, 2, 3, 4});
    ZEXPECT(moved.data() == allocation);
    ZEXPECT(source.empty());
    ZEXPECT(source.inlined());
    ZEXPECT(source.capacity() == 2U);
}

ZEST_CASE(move_of_inline_elements_moves_each) {
    test::Census census;
    small_vector<test::Tracked, 4> source = {1, 2, 3};
    const auto moves = census.moves;
    small_vector<test::Tracked, 4> moved(std::move(source));
    ZEXPECT(test::values(moved) == std::vector{1, 2, 3});
    ZEXPECT(census.moves - moves == 3);
    ZEXPECT(source.empty());
}

ZEST_CASE(move_of_move_only_elements_moves_them) {
    small_vector<std::unique_ptr<int>, 2> source;
    source.push_back(std::make_unique<int>(1));
    small_vector<std::unique_ptr<int>, 2> inline_moved(std::move(source));
    ZASSERT(inline_moved.size() == 1U);
    ZEXPECT(*inline_moved[0] == 1);
    source.push_back(std::make_unique<int>(2));
    source.push_back(std::make_unique<int>(3));
    source.push_back(std::make_unique<int>(4));
    small_vector<std::unique_ptr<int>, 2> heap_moved(std::move(source));
    ZASSERT(heap_moved.size() == 3U);
    ZEXPECT(*heap_moved[2] == 4);
    ZEXPECT(source.empty());
}

ZEST_CASE(move_of_an_empty_allocation_takes_it) {
    small_vector<int, 2> source;
    source.reserve(16);
    const auto* allocation = source.data();
    small_vector<int, 2> moved(std::move(source));
    ZEXPECT(moved.data() == allocation);
    ZEXPECT(moved.capacity() >= 16U);
    ZEXPECT(source.inlined());
}

ZEST_CASE(move_across_capacities_leaves_the_source_its_buffer) {
    small_vector<int, 2> source = {1, 2, 3};
    const auto* allocation = source.data();
    small_vector<int, 8> moved(std::move(source));
    ZEXPECT(moved == std::vector{1, 2, 3});
    ZEXPECT(moved.data() == allocation);
    ZEXPECT(source.capacity() == 2U);
    source.push_back(9);
    ZEXPECT(source.inlined());
    ZEXPECT(source == std::vector{9});
}

ZEST_CASE(from_raw_parts_adopts_the_buffer) {
    auto* buffer = mem::allocate<int>(8);
    for(int i = 0; i < 3; ++i) {
        std::construct_at(buffer + i, i + 1);
    }
    auto v = small_vector<int, 2>::from_raw_parts(buffer, 3, 8);
    ZEXPECT(v == std::vector{1, 2, 3});
    ZEXPECT(v.data() == buffer);
    ZEXPECT(v.capacity() == 8U);
    ZEXPECT(!v.inlined());
}

ZEST_CASE(from_raw_parts_of_nothing_is_empty) {
    auto v = small_vector<int, 2>::from_raw_parts(nullptr, 0, 0);
    ZEXPECT(v.empty());
    ZEXPECT(v.inlined());
}

ZEST_CASE(vector_has_no_inline_buffer) {
    vector<int> v;
    ZEXPECT(v.inline_capacity() == 0U);
    ZEXPECT(v.capacity() == 0U);
    ZEXPECT(v.inlined());
    v.push_back(1);
    ZEXPECT(!v.inlined());
    ZEXPECT(v == std::vector{1});
}

ZEST_CASE(default_inline_capacity_fills_64_bytes) {
    ZSTATIC_EXPECT(sizeof(small_vector<int>) <= 64U);
    ZSTATIC_EXPECT(small_vector<int>::inline_capacity_v >= 1U);
    // An element larger than the budget still gets one inline slot.
    ZSTATIC_EXPECT(small_vector<Big>::inline_capacity_v == 1U);
}

ZEST_CASE(inline_elements_live_inside_the_object) {
    small_vector<int, 4> v = {1, 2};
    ZEXPECT(elements_inside(v));

    small_vector<std::uint8_t, 3> bytes = {1, 2, 3};
    ZEXPECT(elements_inside(bytes));
}

ZEST_CASE(over_aligned_elements_are_aligned_inline_and_on_the_heap) {
    small_vector<Wide, 2> v(2);
    ZEXPECT(elements_inside(v));
    ZEXPECT(reinterpret_cast<std::uintptr_t>(v.data()) % 64 == 0U);
    v.resize(5);
    ZASSERT(!v.inlined());
    ZEXPECT(reinterpret_cast<std::uintptr_t>(v.data()) % 64 == 0U);
}

};  // ZEST_SUITE(support_small_vector_construct)

}  // namespace

}  // namespace kota
