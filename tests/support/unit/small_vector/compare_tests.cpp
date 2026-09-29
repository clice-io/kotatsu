#include <compare>

#include "kota/zest/zest.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

/// Ordered by `<` only, which the vector's `<=>` synthesizes a weak ordering from.
struct LessOnly {
    int value;

    friend bool operator<(const LessOnly& lhs, const LessOnly& rhs) {
        return lhs.value < rhs.value;
    }
};

// Each case checks an operator of the vector itself, so each check is a plain bool.
ZEST_SUITE(support_small_vector_compare) {

ZEST_CASE(equal_elements_compare_equal) {
    small_vector<int, 4> a = {1, 2, 3};
    small_vector<int, 4> b = {1, 2, 3};
    EXPECT((a == b));
    EXPECT(!(a != b));
}

ZEST_CASE(different_elements_or_sizes_compare_unequal) {
    small_vector<int, 4> a = {1, 2, 3};
    EXPECT((a != small_vector<int, 4>{1, 2, 4}));
    EXPECT((a != small_vector<int, 4>{1, 2}));
    EXPECT((a != small_vector<int, 4>{}));
}

ZEST_CASE(ordering_is_lexicographic) {
    small_vector<int, 4> a = {1, 2, 3};
    small_vector<int, 4> b = {1, 2, 4};
    small_vector<int, 4> prefix = {1, 2};
    EXPECT((a < b));
    EXPECT((b > a));
    EXPECT((prefix < a));
    EXPECT((a <= a));
    EXPECT((a >= prefix));
    EXPECT(((a <=> a) == std::strong_ordering::equal));
}

ZEST_CASE(capacities_do_not_matter) {
    small_vector<int, 2> heap = {1, 2, 3};
    small_vector<int, 8> fits = {1, 2, 3};
    EXPECT((heap == fits));
    EXPECT((heap < small_vector<int, 8>{1, 2, 4}));
}

ZEST_CASE(elements_with_only_less_order_weakly) {
    small_vector<LessOnly, 2> a = {{1}, {2}};
    small_vector<LessOnly, 2> b = {{1}, {3}};
    EXPECT(((a <=> b) == std::weak_ordering::less));
    EXPECT(((b <=> a) == std::weak_ordering::greater));
    EXPECT(((a <=> a) == std::weak_ordering::equivalent));
}

};  // ZEST_SUITE(support_small_vector_compare)

}  // namespace

}  // namespace kota
