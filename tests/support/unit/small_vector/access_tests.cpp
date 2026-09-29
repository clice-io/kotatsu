#include <numeric>
#include <stdexcept>
#include <vector>

#include "support/harness/throws.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

ZEST_SUITE(support_small_vector_access) {

ZEST_CASE(index_reaches_each_element) {
    small_vector<int, 4> v = {10, 20, 30};
    v[1] = 99;
    EXPECT(v[0] == 10);
    EXPECT(v[1] == 99);
    EXPECT(v.at(2) == 30);
    const auto& constant = v;
    EXPECT(constant[1] == 99);
    EXPECT(constant.at(0) == 10);
}

ZEST_CASE(front_back_and_data) {
    small_vector<int, 2> v = {1, 2, 3};
    v.front() = 11;
    v.back() = 33;
    EXPECT(v.data()[0] == 11);
    EXPECT(v.data()[2] == 33);
    const auto& constant = v;
    EXPECT(constant.front() == 11);
    EXPECT(constant.back() == 33);
    EXPECT(constant.data() == v.data());
}

#if KOTA_ENABLE_EXCEPTIONS

ZEST_CASE(at_past_the_end_fails) {
    small_vector<int, 2> v = {1};
    EXPECT(test::throws<std::out_of_range>([&] { v.at(1); }));
    const auto& constant = v;
    EXPECT(test::throws<std::out_of_range>([&] { constant.at(100); }));
}

#endif

ZEST_CASE(iterators_walk_the_elements) {
    small_vector<int, 4> v = {1, 2, 3, 4};
    EXPECT(std::accumulate(v.begin(), v.end(), 0) == 10);
    EXPECT(std::accumulate(v.cbegin(), v.cend(), 0) == 10);
    EXPECT(v.end() - v.begin() == 4);
}

ZEST_CASE(reverse_iterators_walk_backwards) {
    const small_vector<int, 4> v = {10, 20, 30};
    std::vector<int> backwards(v.rbegin(), v.rend());
    EXPECT(backwards == std::vector{30, 20, 10});
    EXPECT(std::vector<int>(v.crbegin(), v.crend()) == std::vector{30, 20, 10});

    small_vector<int, 4> mutable_v = {1, 2};
    *mutable_v.rbegin() = 5;
    EXPECT(mutable_v == std::vector{1, 5});
}

ZEST_CASE(range_for_visits_each_element) {
    small_vector<int, 2> v = {1, 2, 3};
    int product = 1;
    for(int x: v) {
        product *= x;
    }
    EXPECT(product == 6);
}

};  // ZEST_SUITE(support_small_vector_access)

}  // namespace

}  // namespace kota
