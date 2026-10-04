#include <array>
#include <cstddef>
#include <list>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

#include "support/harness/tracked.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/support/memory.h"

namespace kota::mem {

namespace {

struct First {
    int a = 1;
};

struct Second {
    int b = 2;
};

struct Both : First, Second {};

/// Uninitialized room for `Count` elements of T, which the test constructs and destroys.
template <typename T, std::size_t Count>
struct Room {
    alignas(T) std::byte bytes[Count * sizeof(T)];

    T* data() {
        return reinterpret_cast<T*>(bytes);
    }
};

ZEST_SUITE(support_memory) {

ZEST_CASE(same_trivially_copyable_types_copy_as_bytes) {
    ZSTATIC_EXPECT(is_bitwise_constructible_v<int, const int&>);
    ZSTATIC_EXPECT(is_bitwise_constructible_v<First, First&&>);
    ZSTATIC_EXPECT(!is_bitwise_constructible_v<std::string, const std::string&>);
}

ZEST_CASE(integers_of_one_size_copy_as_bytes) {
    ZSTATIC_EXPECT(is_bitwise_constructible_v<unsigned, const int&>);
    ZSTATIC_EXPECT(!is_bitwise_constructible_v<long long, const int&>);
    ZSTATIC_EXPECT(!is_bitwise_constructible_v<bool, const char&>);
    ZSTATIC_EXPECT(!is_bitwise_constructible_v<char, const bool&>);
}

ZEST_CASE(pointer_conversions_do_not_copy_as_bytes) {
    // A pointer to Both converted to one to its Second base moves past the First base.
    ZSTATIC_EXPECT(!is_bitwise_constructible_v<Second*, Both* const&>);
    ZSTATIC_EXPECT(!is_bitwise_constructible_v<const int*, int* const&>);
    ZSTATIC_EXPECT(is_bitwise_constructible_v<int*, int* const&>);
}

ZEST_CASE(allocate_of_nothing_is_null) {
    ZEXPECT(allocate<int>(0) == nullptr);
    deallocate<int>(nullptr, 0);
    auto* three = allocate<int>(3);
    ZEXPECT(three != nullptr);
    deallocate(three, 3);
}

ZEST_CASE(pointer_in_range_is_half_open) {
    std::array<int, 3> values = {};
    const auto* first = values.data();
    const auto* last = first + values.size();
    ZEXPECT(pointer_in_range(first, first, last));
    ZEXPECT(pointer_in_range(first + 2, first, last));
    ZEXPECT(!pointer_in_range(last, first, last));
    ZEXPECT(!pointer_in_range(first, first, first));
}

ZEST_CASE(uninitialized_copy_of_bytes) {
    std::array<int, 4> source = {1, 2, 3, 4};
    Room<int, 4> room;
    auto* end = uninitialized_copy(source, room.data());
    ZEXPECT(end == room.data() + 4);
    ZEXPECT(std::vector<int>(room.data(), end) == std::vector{1, 2, 3, 4});
}

ZEST_CASE(uninitialized_copy_converts_pointers_one_by_one) {
    Both object;
    std::array<Both*, 2> source = {&object, &object};
    Room<Second*, 2> room;
    uninitialized_copy(source, room.data());
    ZEXPECT(room.data()[0] == static_cast<Second*>(&object));
    ZEXPECT(room.data()[1]->b == 2);
}

ZEST_CASE(uninitialized_copy_of_a_non_contiguous_range) {
    std::list<std::string> source = {"a", "b"};
    Room<std::string, 2> room;
    auto* end = uninitialized_copy(source, room.data());
    ZEXPECT(std::vector<std::string>(room.data(), end) == std::vector<std::string>{"a", "b"});
    std::ranges::destroy(room.data(), end);
}

ZEST_CASE(uninitialized_move_leaves_the_sources_moved_from) {
    test::Census census;
    std::array<test::Tracked, 2> source = {1, 2};
    Room<test::Tracked, 2> room;
    auto* end = uninitialized_move(source.data(), source.data() + 2, room.data());
    ZEXPECT(test::values(std::ranges::subrange(room.data(), end)) == std::vector{1, 2});
    ZEXPECT(test::values(source) == std::vector{-1, -1});
    ZEXPECT(census.moves == 2);
    std::ranges::destroy(room.data(), end);
}

ZEST_CASE(uninitialized_value_and_default_construct) {
    Room<int, 3> room;
    auto* end = uninitialized_value_construct(room.data(), room.data() + 3);
    ZEXPECT(std::vector<int>(room.data(), end) == std::vector{0, 0, 0});

    test::Census census;
    Room<test::Tracked, 2> tracked;
    auto* tracked_end = uninitialized_default_construct(tracked.data(), tracked.data() + 2);
    ZEXPECT(census.live == 2);
    std::ranges::destroy(tracked.data(), tracked_end);
    ZEXPECT(census.live == 0);
}

ZEST_CASE(uninitialized_fill_copies_the_value) {
    Room<std::string, 3> room;
    const std::string value = "x";
    auto* end = uninitialized_fill(room.data(), room.data() + 3, value);
    ZEXPECT(std::vector<std::string>(room.data(), end) == std::vector<std::string>(3, "x"));
    std::ranges::destroy(room.data(), end);
}

ZEST_CASE(allocation_guard_frees_what_it_was_not_asked_to_keep) {
    test::Census census;
    {
        AllocationGuard<test::Tracked> guard(allocate<test::Tracked>(3), 3);
        std::construct_at(guard.data(), 1);
        std::construct_at(guard.data() + 1, 2);
        guard.mark(guard.data() + 2);
        ZEXPECT(census.live == 2);
    }
    ZEXPECT(census.live == 0);
}

ZEST_CASE(allocation_guard_hands_over_on_release) {
    AllocationGuard<int> guard(allocate<int>(2), 2);
    std::construct_at(guard.data(), 7);
    guard.mark(guard.data() + 1);
    auto* kept = guard.release();
    ZEXPECT(guard.data() == nullptr);
    ZEXPECT(*kept == 7);
    deallocate(kept, 2);
}

ZEST_CASE(default_construct_in_constant_evaluation_initializes) {
    constexpr auto value = [] {
        std::allocator<int> allocator;
        auto* p = allocator.allocate(1);
        default_construct(p);
        const int read = *p;
        allocator.deallocate(p, 1);
        return read;
    }();
    ZSTATIC_EXPECT(value == 0);
}

#if KOTA_ENABLE_EXCEPTIONS

ZEST_CASE(uninitialized_copy_of_a_throwing_copy_fails) {
    test::Census census;
    std::array<test::Tracked, 3> source = {1, 2, 3};
    Room<test::Tracked, 3> room;
    census.throw_after = 2;
    ZEXPECT(zest::throws([&] { uninitialized_copy(source, room.data()); }));
    ZEXPECT(census.live == 3);
}

ZEST_CASE(uninitialized_fill_of_a_throwing_copy_fails) {
    test::Census census;
    const test::Tracked value(5);
    Room<test::Tracked, 3> room;
    census.throw_after = 1;
    ZEXPECT(zest::throws([&] { uninitialized_fill(room.data(), room.data() + 3, value); }));
    ZEXPECT(census.live == 1);
}

#endif  // KOTA_ENABLE_EXCEPTIONS

};  // ZEST_SUITE(support_memory)

}  // namespace

}  // namespace kota::mem
