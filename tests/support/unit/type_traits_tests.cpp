#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/support/type_traits.h"

namespace kota {

namespace {

struct Unordered {};

struct EqualOnly {
    bool operator==(const EqualOnly&) const = default;
};

ZEST_SUITE(support_type_traits) {

ZEST_CASE(specialization_of_matches_the_template_exactly) {
    STATIC_EXPECT(is_specialization_of<std::vector, std::vector<int>>);
    STATIC_EXPECT(!is_specialization_of<std::vector, std::string>);
    STATIC_EXPECT(!is_specialization_of<std::optional, const std::optional<int>>);
}

ZEST_CASE(optional_and_expected_see_through_references) {
    STATIC_EXPECT(is_optional_v<std::optional<int>>);
    STATIC_EXPECT(is_optional_v<const std::optional<int>&>);
    STATIC_EXPECT(!is_optional_v<int>);
    STATIC_EXPECT(is_expected_v<std::expected<int, std::string>&&>);
    STATIC_EXPECT(!is_expected_v<std::optional<int>>);
}

ZEST_CASE(comparison_concepts_follow_the_operators) {
    STATIC_EXPECT(eq_comparable_with<int, long>);
    STATIC_EXPECT(eq_comparable_with<EqualOnly, EqualOnly>);
    STATIC_EXPECT(!eq_comparable_with<Unordered, Unordered>);
    STATIC_EXPECT(lt_comparable_with<std::string, const char*>);
    STATIC_EXPECT(!lt_comparable_with<EqualOnly, EqualOnly>);
    STATIC_EXPECT(le_comparable_with<double, int>);
    STATIC_EXPECT(gt_comparable_with<int, int>);
    STATIC_EXPECT(ge_comparable_with<int, int>);
    STATIC_EXPECT(!ge_comparable_with<Unordered, int>);
}

ZEST_CASE(dependent_false_is_false) {
    STATIC_EXPECT(!dependent_false<int>);
}

};  // ZEST_SUITE(support_type_traits)

}  // namespace

}  // namespace kota
