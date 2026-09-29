#include <vector>

#include "support/harness/tracked.h"
#include "kota/zest/zest.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

ZEST_SUITE(support_small_vector_erase) {

ZEST_CASE(erase_one_returns_the_next) {
    small_vector<int, 4> v = {1, 2, 3, 4};
    auto it = v.erase(v.begin() + 1);
    EXPECT(v == std::vector{1, 3, 4});
    ASSERT(it == v.begin() + 1);
    EXPECT(*it == 3);
}

ZEST_CASE(erase_the_first_and_the_last) {
    small_vector<int, 4> v = {1, 2, 3};
    v.erase(v.begin());
    auto it = v.erase(v.end() - 1);
    EXPECT(v == std::vector{2});
    EXPECT(it == v.end());
}

ZEST_CASE(erase_range_moves_the_rest_down) {
    small_vector<int, 4> v = {1, 2, 3, 4, 5};
    auto it = v.erase(v.begin() + 1, v.begin() + 4);
    EXPECT(v == std::vector{1, 5});
    ASSERT(it == v.begin() + 1);
    EXPECT(*it == 5);
}

ZEST_CASE(erase_empty_range_changes_nothing) {
    small_vector<int, 4> v = {1, 2};
    auto it = v.erase(v.begin() + 1, v.begin() + 1);
    EXPECT(v == std::vector{1, 2});
    EXPECT(it == v.begin() + 1);

    small_vector<int, 4> empty;
    EXPECT(empty.erase(empty.begin(), empty.end()) == empty.end());
}

ZEST_CASE(erase_everything) {
    small_vector<int, 4> v = {1, 2, 3};
    v.erase(v.begin(), v.end());
    EXPECT(v.empty());
}

ZEST_CASE(erase_destroys_what_it_removes) {
    test::Census census;
    small_vector<test::Tracked, 2> v = {1, 2, 3, 4};
    v.erase(v.begin());
    EXPECT(census.live == 3);
    v.erase(v.begin(), v.begin() + 2);
    EXPECT(census.live == 1);
    EXPECT(test::values(v) == std::vector{4});
}

ZEST_CASE(clear_keeps_the_capacity) {
    test::Census census;
    small_vector<test::Tracked, 3> v = {1, 2, 3, 4};
    const auto capacity = v.capacity();
    v.clear();
    EXPECT(v.empty());
    EXPECT(v.capacity() == capacity);
    EXPECT(census.live == 0);
    v.push_back(9);
    EXPECT(test::values(v) == std::vector{9});
}

ZEST_CASE(pop_back_removes_the_last) {
    test::Census census;
    small_vector<test::Tracked, 4> v = {1, 2, 3};
    v.pop_back();
    EXPECT(test::values(v) == std::vector{1, 2});
    EXPECT(census.live == 2);
}

ZEST_CASE(pop_back_val_returns_the_last) {
    small_vector<std::string, 4> v = {"a", "b"};
    auto last = v.pop_back_val();
    EXPECT(last == "b");
    EXPECT(v == std::vector<std::string>{"a"});
}

ZEST_CASE(pop_back_n_removes_the_last_n) {
    test::Census census;
    small_vector<test::Tracked, 2> v = {1, 2, 3, 4};
    v.pop_back_n(2);
    EXPECT(test::values(v) == std::vector{1, 2});
    EXPECT(census.live == 2);
    v.pop_back_n(0);
    EXPECT(v.size() == 2U);
    v.pop_back_n(2);
    EXPECT(v.empty());
}

ZEST_CASE(truncate_keeps_the_first_count) {
    test::Census census;
    small_vector<test::Tracked, 2> v = {1, 2, 3};
    const auto capacity = v.capacity();
    v.truncate(3);
    EXPECT(v.size() == 3U);
    v.truncate(1);
    EXPECT(test::values(v) == std::vector{1});
    EXPECT(v.capacity() == capacity);
    EXPECT(census.live == 1);
    v.truncate(0);
    EXPECT(v.empty());
}

};  // ZEST_SUITE(support_small_vector_erase)

}  // namespace

}  // namespace kota
