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
    ZEXPECT((a == b));
    ZEXPECT(!(a != b));
}

ZEST_CASE(different_elements_or_sizes_compare_unequal) {
    small_vector<int, 4> a = {1, 2, 3};
    ZEXPECT((a != small_vector<int, 4>{1, 2, 4}));
    ZEXPECT((a != small_vector<int, 4>{1, 2}));
    ZEXPECT((a != small_vector<int, 4>{}));
}

ZEST_CASE(ordering_is_lexicographic) {
    small_vector<int, 4> a = {1, 2, 3};
    small_vector<int, 4> b = {1, 2, 4};
    small_vector<int, 4> prefix = {1, 2};
    ZEXPECT((a < b));
    ZEXPECT((b > a));
    ZEXPECT((prefix < a));
    ZEXPECT((a <= a));
    ZEXPECT((a >= prefix));
    ZEXPECT(((a <=> a) == std::strong_ordering::equal));
}

ZEST_CASE(capacities_do_not_matter) {
    small_vector<int, 2> heap = {1, 2, 3};
    small_vector<int, 8> fits = {1, 2, 3};
    ZEXPECT((heap == fits));
    ZEXPECT((heap < small_vector<int, 8>{1, 2, 4}));
}

ZEST_CASE(elements_with_only_less_order_weakly) {
    small_vector<LessOnly, 2> a = {{1}, {2}};
    small_vector<LessOnly, 2> b = {{1}, {3}};
    ZEXPECT(((a <=> b) == std::weak_ordering::less));
    ZEXPECT(((b <=> a) == std::weak_ordering::greater));
    ZEXPECT(((a <=> a) == std::weak_ordering::equivalent));
}

};  // ZEST_SUITE(support_small_vector_compare)

}  // namespace

}  // namespace kota
