#include <algorithm>
#include <array>
#include <compare>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "kota/zest/zest.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

// In constant evaluation there is no inline buffer: every vector allocates.

constexpr bool int_operations() {
    small_vector<int, 4> v{1, 2, 3};
    v.append(std::array{4, 5});
    v.insert(v.begin() + 1, 9);
    v.erase(v.begin() + 2);
    v.resize_for_overwrite(7);
    v[5] = 11;
    v[6] = 12;
    const auto popped = v.pop_back_val();
    v.shrink_to_fit();
    return v.size() == 6 && v.front() == 1 && v[1] == 9 && v.back() == 11 && popped == 12 &&
           !v.inlined() && (v <=> small_vector<int, 0>{1, 9, 3, 4, 5, 11}) == 0;
}

constexpr bool string_operations() {
    small_vector<std::string, 2> v;
    v.emplace_back("alpha");
    v.emplace_back("beta");
    v.resize(4, std::string("tail"));
    v.insert(v.begin(), v[1]);
    v.pop_back();
    v.shrink_to_fit();
    return v.size() == 4 && v[0] == "beta" && v[1] == "alpha" && v.back() == "tail";
}

constexpr bool optional_operations() {
    small_vector<std::optional<int>, 1> v;
    std::array<std::optional<int>, 3> source = {1, std::nullopt, 3};
    v.assign(source);
    v.erase(v.begin() + 1);
    v.push_back(4);
    return v.size() == 3 && *v[0] == 1 && *v[1] == 3 && *v[2] == 4;
}

constexpr bool variant_operations() {
    small_vector<std::variant<int, std::string>, 1> v;
    v.emplace_back(1);
    v.emplace_back(std::string("two"));
    v.emplace_back(3);
    v.erase(v.begin());
    v.shrink_to_fit();
    return v.size() == 2 && std::get<std::string>(v[0]) == "two" && std::get<int>(v[1]) == 3;
}

constexpr bool moved_from_vector_is_reusable() {
    small_vector<int, 4> source = {1, 2};
    small_vector<int, 4> moved(std::move(source));
    source.push_back(3);
    small_vector<int, 4> assigned;
    assigned = std::move(moved);
    moved.push_back(4);
    return source.size() == 1 && source[0] == 3 && moved.size() == 1 && moved[0] == 4 &&
           assigned.size() == 2 && source.capacity() >= 1;
}

constexpr bool aliasing_arguments() {
    small_vector<int, 2> v = {1, 2};
    v.push_back(v[0]);
    v.append(v);
    v.insert(v.begin(), v[5]);
    return std::ranges::equal(v, std::array{1, 1, 2, 1, 1, 2, 1});
}

ZEST_SUITE(support_small_vector_constexpr) {

ZEST_CASE(deduced_vectors) {
    STATIC_EXPECT(small_vector{1, 2, 3}.size() == 3U);
    STATIC_EXPECT(vector<int>{1, 2, 3, 4}.size() == 4U);
}

ZEST_CASE(int_elements) {
    STATIC_EXPECT(int_operations());
}

ZEST_CASE(string_elements) {
    STATIC_EXPECT(string_operations());
}

ZEST_CASE(optional_elements) {
    STATIC_EXPECT(optional_operations());
}

ZEST_CASE(variant_elements) {
    STATIC_EXPECT(variant_operations());
}

ZEST_CASE(moved_from_vectors) {
    STATIC_EXPECT(moved_from_vector_is_reusable());
}

ZEST_CASE(aliasing) {
    STATIC_EXPECT(aliasing_arguments());
}

};  // ZEST_SUITE(support_small_vector_constexpr)

}  // namespace

}  // namespace kota
