#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>

#include "kota/zest/zest.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

// In constant evaluation there is no inline buffer: every vector allocates. Each function
// below runs a vector through a series of operations and returns what they left, which a
// constant can hold, for the checks to read one fact at a time.

/// Up to 8 elements of an int vector, and its size.
struct Ints {
    std::size_t size = 0;
    std::array<int, 8> elements = {};

    template <typename Range>
    constexpr static Ints of(const Range& range) {
        Ints ints{.size = std::ranges::size(range)};
        std::ranges::copy(range | std::views::take(ints.elements.size()), ints.elements.begin());
        return ints;
    }
};

struct IntFacts {
    Ints ints;
    int popped = 0;
    bool inlined = true;
};

constexpr IntFacts int_operations() {
    small_vector<int, 4> v{1, 2, 3};
    v.append(std::array{4, 5});
    v.insert(v.begin() + 1, 9);
    v.erase(v.begin() + 2);
    v.resize_for_overwrite(7);
    v[5] = 11;
    v[6] = 12;
    const auto popped = v.pop_back_val();
    v.shrink_to_fit();
    return {.ints = Ints::of(v), .popped = popped, .inlined = v.inlined()};
}

struct StringFacts {
    std::size_t size = 0;
    bool first_is_beta = false;
    bool second_is_alpha = false;
    bool last_is_tail = false;
};

constexpr StringFacts string_operations() {
    small_vector<std::string, 2> v;
    v.emplace_back("alpha");
    v.emplace_back("beta");
    v.resize(4, std::string("tail"));
    v.insert(v.begin(), v[1]);
    v.pop_back();
    v.shrink_to_fit();
    return {
        .size = v.size(),
        .first_is_beta = v[0] == "beta",
        .second_is_alpha = v[1] == "alpha",
        .last_is_tail = v.back() == "tail",
    };
}

/// The values of an optional<int> vector, -1 standing for nullopt.
constexpr Ints optional_operations() {
    small_vector<std::optional<int>, 1> v;
    std::array<std::optional<int>, 4> source = {1, std::nullopt, 3, std::nullopt};
    v.assign(source);
    v.erase(v.begin() + 1);
    v.push_back(4);
    return Ints::of(v |
                    std::views::transform([](const auto& value) { return value.value_or(-1); }));
}

struct VariantFacts {
    std::size_t size = 0;
    bool first_is_two = false;
    int second = 0;
};

constexpr VariantFacts variant_operations() {
    small_vector<std::variant<int, std::string>, 1> v;
    v.emplace_back(1);
    v.emplace_back(std::string("two"));
    v.emplace_back(3);
    v.erase(v.begin());
    v.shrink_to_fit();
    return {
        .size = v.size(),
        .first_is_two = std::get<std::string>(v[0]) == "two",
        .second = std::get<int>(v[1]),
    };
}

struct MovedFacts {
    Ints source;
    Ints moved;
    Ints assigned;
};

constexpr MovedFacts moved_from_vectors() {
    small_vector<int, 4> source = {1, 2};
    small_vector<int, 4> moved(std::move(source));
    source.push_back(3);
    small_vector<int, 4> assigned;
    assigned = std::move(moved);
    moved.push_back(4);
    return {.source = Ints::of(source), .moved = Ints::of(moved), .assigned = Ints::of(assigned)};
}

constexpr Ints aliasing_arguments() {
    small_vector<int, 2> v = {1, 2};
    v.push_back(v[0]);
    v.append(v);
    v.insert(v.begin(), v[5]);
    v.assign(std::ranges::subrange(v.begin() + 1, v.end()));
    return Ints::of(v);
}

ZEST_SUITE(support_small_vector_constexpr) {

ZEST_CASE(deduction_takes_the_element_type) {
    STATIC_EXPECT(small_vector{1, 2, 3}.size() == 3U);
    STATIC_EXPECT(vector<int>{1, 2, 3, 4}.size() == 4U);
}

ZEST_CASE(int_elements_go_through_every_operation) {
    constexpr auto facts = int_operations();
    STATIC_EXPECT(facts.ints.size == 6U);
    STATIC_EXPECT(facts.ints.elements == std::array{1, 9, 3, 4, 5, 11, 0, 0});
    STATIC_EXPECT(facts.popped == 12);
    STATIC_EXPECT(!facts.inlined);
}

ZEST_CASE(string_elements_go_through_every_operation) {
    constexpr auto facts = string_operations();
    STATIC_EXPECT(facts.size == 4U);
    STATIC_EXPECT(facts.first_is_beta);
    STATIC_EXPECT(facts.second_is_alpha);
    STATIC_EXPECT(facts.last_is_tail);
}

ZEST_CASE(optional_elements_go_through_every_operation) {
    constexpr auto ints = optional_operations();
    STATIC_EXPECT(ints.size == 4U);
    STATIC_EXPECT(ints.elements == std::array{1, 3, -1, 4, 0, 0, 0, 0});
}

ZEST_CASE(variant_elements_go_through_every_operation) {
    constexpr auto facts = variant_operations();
    STATIC_EXPECT(facts.size == 2U);
    STATIC_EXPECT(facts.first_is_two);
    STATIC_EXPECT(facts.second == 3);
}

ZEST_CASE(moved_from_vectors_are_reusable) {
    constexpr auto facts = moved_from_vectors();
    STATIC_EXPECT(facts.source.size == 1U);
    STATIC_EXPECT(facts.source.elements[0] == 3);
    STATIC_EXPECT(facts.moved.size == 1U);
    STATIC_EXPECT(facts.moved.elements[0] == 4);
    STATIC_EXPECT(facts.assigned.size == 2U);
    STATIC_EXPECT(facts.assigned.elements[1] == 2);
}

ZEST_CASE(arguments_may_view_the_elements) {
    constexpr auto ints = aliasing_arguments();
    STATIC_EXPECT(ints.size == 6U);
    STATIC_EXPECT(ints.elements == std::array{1, 2, 1, 1, 2, 1, 0, 0});
}

};  // ZEST_SUITE(support_small_vector_constexpr)

}  // namespace

}  // namespace kota
