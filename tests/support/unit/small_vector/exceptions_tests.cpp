#include <array>
#include <stdexcept>
#include <vector>

#include "support/harness/throws.h"
#include "support/harness/tracked.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

#if KOTA_ENABLE_EXCEPTIONS

// An element's constructor throws partway through an operation. Whatever the vector then
// holds, every element it constructed is either among its elements or destroyed: the census
// counts as many live elements as the vector and the test hold.

ZEST_SUITE(support_small_vector_exceptions) {

ZEST_CASE(push_back_of_a_throwing_copy_while_growing_fails) {
    test::Census census;
    small_vector<test::Tracked, 2> v = {1, 2};
    const auto* data = v.data();
    census.throw_after = 0;
    EXPECT_THROWS(v.push_back(v[0]));
    EXPECT(test::values(v) == std::vector{1, 2});
    EXPECT(v.data() == data);
    EXPECT(census.live == 2);
}

ZEST_CASE(growing_with_a_throwing_move_copies_the_elements) {
    // Elements whose move can throw are copied into the new allocation rather than moved, so
    // a copy that throws leaves the old ones as they were.
    test::Census census;
    small_vector<test::ThrowingTracked, 2> v = {1, 2};
    const auto* data = v.data();
    test::ThrowingTracked element(3);
    census.throw_after = 2;
    EXPECT_THROWS(v.push_back(element));
    EXPECT(test::values(v) == std::vector{1, 2});
    EXPECT(v.data() == data);
    EXPECT(census.live == 3);
    v.push_back(element);
    EXPECT(test::values(v) == std::vector{1, 2, 3});
    EXPECT(census.moves == 0);
}

ZEST_CASE(insert_of_a_throwing_copy_fails) {
    test::Census census;
    small_vector<test::Tracked, 4> v = {1, 2, 3};
    census.throw_after = 0;
    EXPECT_THROWS(v.insert(v.begin() + 1, v[2]));
    EXPECT(test::values(v) == std::vector{1, 2, 3});
    census.throw_after = 0;
    EXPECT_THROWS(v.emplace(v.begin(), v[0]));
    EXPECT(test::values(v) == std::vector{1, 2, 3});
    EXPECT(census.live == 3);
}

ZEST_CASE(append_of_a_throwing_copy_fails) {
    test::Census census;
    small_vector<test::Tracked, 4> v = {1};
    std::array<test::Tracked, 3> source = {7, 8, 9};
    census.throw_after = 1;
    EXPECT_THROWS(v.append(source));
    EXPECT(test::values(v) == std::vector{1});
    EXPECT(census.live == 4);
}

ZEST_CASE(append_of_a_throwing_copy_while_growing_fails) {
    test::Census census;
    small_vector<test::Tracked, 2> v = {1, 2};
    census.throw_after = 2;
    EXPECT_THROWS(v.append(3, test::Tracked(5)));
    EXPECT(test::values(v) == std::vector{1, 2});
    EXPECT(v.inlined());
    EXPECT(census.live == 2);
}

ZEST_CASE(insert_copies_with_a_throwing_tail_move_fails) {
    test::Census census;
    {
        small_vector<test::ThrowingTracked, 8> v = {1, 2};
        // The copies land past the old end, then the tail moves behind them and throws.
        census.throw_after = 3;
        EXPECT_THROWS(v.insert(v.begin() + 1, 3, test::ThrowingTracked(5)));
        EXPECT(census.live == static_cast<int>(v.size()));
    }
    EXPECT(census.live == 0);
}

ZEST_CASE(insert_range_with_a_throwing_tail_move_fails) {
    test::Census census;
    {
        small_vector<test::ThrowingTracked, 8> v = {1, 2};
        std::array<test::ThrowingTracked, 3> source = {7, 8, 9};
        census.throw_after = 2;
        EXPECT_THROWS(v.insert(v.begin() + 1, source));
        EXPECT(census.live == static_cast<int>(v.size()) + 3);
    }
    EXPECT(census.live == 0);
}

ZEST_CASE(insert_of_a_throwing_copy_while_growing_fails) {
    test::Census census;
    {
        small_vector<test::Tracked, 2> v = {1, 2};
        std::array<test::Tracked, 3> source = {7, 8, 9};
        census.throw_after = 1;
        EXPECT_THROWS(v.insert(v.begin() + 1, source));
        EXPECT(census.live == static_cast<int>(v.size()) + 3);
    }
    EXPECT(census.live == 0);
}

ZEST_CASE(assign_of_a_throwing_copy_while_growing_fails) {
    test::Census census;
    small_vector<test::Tracked, 2> v = {1, 2};
    census.throw_after = 3;
    EXPECT_THROWS(v.assign(6, test::Tracked(4)));
    EXPECT(test::values(v) == std::vector{1, 2});
    EXPECT(census.live == 2);
}

ZEST_CASE(copy_assignment_of_a_throwing_copy_fails) {
    test::Census census;
    {
        small_vector<test::Tracked, 2> target = {1};
        small_vector<test::Tracked, 2> source = {4, 5, 6, 7};
        census.throw_after = 2;
        EXPECT_THROWS(target = source);
        EXPECT(census.live == static_cast<int>(target.size()) + 4);
    }
    EXPECT(census.live == 0);
}

ZEST_CASE(resize_with_a_throwing_copy_fails) {
    test::Census census;
    small_vector<test::Tracked, 2> v = {1};
    census.throw_after = 1;
    EXPECT_THROWS(v.resize(4, test::Tracked(3)));
    EXPECT(test::values(v) == std::vector{1});
    EXPECT(census.live == 1);
}

ZEST_CASE(growing_past_max_size_fails) {
    small_vector<int, 2> v = {1};
    EXPECT(test::throws<std::length_error>([&] { v.reserve(v.max_size() + 1); }));
    EXPECT(test::throws<std::length_error>([&] { v.append(v.max_size(), 0); }));
    EXPECT(v == std::vector{1});
}

};  // ZEST_SUITE(support_small_vector_exceptions)

#endif  // KOTA_ENABLE_EXCEPTIONS

}  // namespace

}  // namespace kota
