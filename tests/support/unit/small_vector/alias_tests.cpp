#include <ranges>
#include <string>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

// An argument that views the vector's own elements stays valid through the operation,
// including one that reallocates. The elements are strings that allocate, so that reading
// one after its buffer is gone is an ASan report as well as a wrong value.

using Strings = small_vector<std::string, 2>;

const std::string alpha = "alpha, which is long enough to allocate";
const std::string beta = "beta, which is long enough to allocate";

/// A vector of `alpha` and `beta`, full: whatever adds to it reallocates.
Strings full() {
    return Strings{alpha, beta};
}

/// A vector of `alpha` and `beta` on the heap, with room to spare.
Strings roomy() {
    Strings v{alpha, beta};
    v.reserve(8);
    return v;
}

ZEST_SUITE(support_small_vector_alias) {

ZEST_CASE(push_back_own_element_while_growing) {
    auto v = full();
    v.push_back(v[0]);
    ZEXPECT(v == std::vector{alpha, beta, alpha});
}

ZEST_CASE(push_back_own_moved_element_while_growing) {
    auto v = full();
    v.push_back(std::move(v[1]));
    ZASSERT(v.size() == 3U);
    ZEXPECT(v[2] == beta);
}

ZEST_CASE(emplace_back_own_element_while_growing) {
    auto v = full();
    v.emplace_back(v[1]);
    ZEXPECT(v == std::vector{alpha, beta, beta});
}

ZEST_CASE(append_copies_of_own_element_while_growing) {
    auto v = full();
    v.append(3, v[0]);
    ZEXPECT(v == std::vector{alpha, beta, alpha, alpha, alpha});
}

ZEST_CASE(append_itself_from_the_inline_buffer) {
    Strings v{alpha};
    v.append(std::ranges::subrange(v.begin(), v.end()));
    v.append(v);
    ZEXPECT(v == std::vector{alpha, alpha, alpha, alpha});
}

ZEST_CASE(append_itself_from_the_heap) {
    auto v = full();
    v.push_back(alpha);
    v.append(v);
    ZEXPECT(v == std::vector{alpha, beta, alpha, alpha, beta, alpha});
}

ZEST_CASE(append_itself_within_the_capacity) {
    auto v = roomy();
    v.append(v);
    ZEXPECT(v == std::vector{alpha, beta, alpha, beta});
}

ZEST_CASE(append_part_of_itself_while_growing) {
    auto v = full();
    v.append(std::ranges::subrange(v.begin() + 1, v.end()));
    ZEXPECT(v == std::vector{alpha, beta, beta});
}

ZEST_CASE(assign_copies_of_own_element_while_growing) {
    auto v = full();
    v.assign(5, v[1]);
    ZEXPECT(v == std::vector<std::string>(5, beta));
}

ZEST_CASE(assign_copies_of_own_element_while_shrinking) {
    auto v = roomy();
    v.push_back(alpha);
    v.assign(1, v[2]);
    ZEXPECT(v == std::vector{alpha});
}

ZEST_CASE(assign_part_of_itself) {
    auto v = roomy();
    v.push_back(alpha);
    v.assign(std::ranges::subrange(v.begin() + 1, v.end()));
    ZEXPECT(v == std::vector{beta, alpha});
}

ZEST_CASE(part_of_itself_of_trivial_elements_is_copied_without_overlap) {
    // Trivially copyable elements are copied as bytes, which must not overlap the elements.
    small_vector<int, 8> v = {1, 2, 3, 4};
    v.assign(std::ranges::subrange(v.begin() + 1, v.end()));
    ZEXPECT(v == std::vector{2, 3, 4});
    v.insert(v.begin(), std::ranges::subrange(v.begin() + 1, v.end()));
    ZEXPECT(v == std::vector{3, 4, 2, 3, 4});
    v.append(std::ranges::subrange(v.begin() + 3, v.end()));
    ZEXPECT(v == std::vector{3, 4, 2, 3, 4, 3, 4});
}

ZEST_CASE(assign_itself_reversed) {
    auto v = full();
    v.assign(v | std::views::reverse);
    ZEXPECT(v == std::vector{beta, alpha});
}

ZEST_CASE(resize_with_own_element_while_growing) {
    auto v = full();
    v.resize(4, v[0]);
    ZEXPECT(v == std::vector{alpha, beta, alpha, alpha});
}

ZEST_CASE(insert_own_element_while_growing) {
    auto v = full();
    v.insert(v.begin(), v[1]);
    ZEXPECT(v == std::vector{beta, alpha, beta});
}

ZEST_CASE(insert_own_element_in_place) {
    auto v = roomy();
    v.insert(v.begin(), v[1]);
    ZEXPECT(v == std::vector{beta, alpha, beta});
}

ZEST_CASE(insert_own_moved_element) {
    auto v = roomy();
    v.insert(v.begin(), std::move(v[1]));
    ZASSERT(v.size() == 3U);
    ZEXPECT(v[0] == beta);
    ZEXPECT(v[1] == alpha);
}

ZEST_CASE(insert_copies_of_own_element) {
    auto v = roomy();
    v.insert(v.begin(), 2, v[1]);
    ZEXPECT(v == std::vector{beta, beta, alpha, beta});

    auto w = full();
    w.insert(w.begin() + 1, 3, w[0]);
    ZEXPECT(w == std::vector{alpha, alpha, alpha, alpha, beta});
}

ZEST_CASE(insert_part_of_itself_in_place) {
    small_vector<std::string, 8> v = {"a", "b", "c", "d"};
    v.insert(v.begin() + 1, std::ranges::subrange(v.begin(), v.begin() + 2));
    ZEXPECT(v == std::vector<std::string>{"a", "a", "b", "b", "c", "d"});
}

ZEST_CASE(insert_itself_while_growing) {
    auto v = full();
    v.insert(v.begin(), std::ranges::subrange(v.begin(), v.end()));
    ZEXPECT(v == std::vector{alpha, beta, alpha, beta});
}

ZEST_CASE(insert_itself_reversed) {
    auto v = roomy();
    v.insert(v.begin() + 1, v | std::views::reverse);
    ZEXPECT(v == std::vector{alpha, beta, alpha, beta});
}

ZEST_CASE(emplace_from_own_element) {
    auto v = full();
    v.emplace(v.begin() + 1, v[1]);
    ZEXPECT(v == std::vector{alpha, beta, beta});
}

};  // ZEST_SUITE(support_small_vector_alias)

}  // namespace

}  // namespace kota
