#include <optional>
#include <tuple>
#include <type_traits>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/support/tuple_traits.h"

namespace kota {

namespace {

using Mixed = std::tuple<int, std::optional<char>, float, std::vector<int>, double>;

ZEST_SUITE(support_tuple_traits) {

ZEST_CASE(any_of_and_count_of) {
    ZSTATIC_EXPECT(tuple_any_of_v<Mixed, std::is_floating_point>);
    ZSTATIC_EXPECT(!tuple_any_of_v<Mixed, std::is_pointer>);
    ZSTATIC_EXPECT(!tuple_any_of_v<std::tuple<>, std::is_integral>);
    ZSTATIC_EXPECT(tuple_count_of_v<Mixed, std::is_floating_point> == 2U);
    ZSTATIC_EXPECT(tuple_count_of_v<std::tuple<>, std::is_floating_point> == 0U);
}

ZEST_CASE(has_a_type) {
    ZSTATIC_EXPECT(tuple_has_v<Mixed, float>);
    ZSTATIC_EXPECT(!tuple_has_v<Mixed, long>);
    ZSTATIC_EXPECT(tuple_has_spec_v<Mixed, std::optional>);
    ZSTATIC_EXPECT(!tuple_has_spec_v<std::tuple<int>, std::optional>);
}

ZEST_CASE(finds_the_first_match) {
    ZEXPECT(zest::type_eq<tuple_find_t<Mixed, std::is_floating_point>, float>());
    ZEXPECT(zest::type_eq<tuple_find_t<Mixed, std::is_pointer>, void>());
    ZEXPECT(zest::type_eq<tuple_find_spec_t<Mixed, std::vector>, std::vector<int>>());
    ZEXPECT(zest::type_eq<tuple_find_spec_t<Mixed, std::tuple>, void>());
}

ZEST_CASE(only_tuples_are_searched) {
    ZSTATIC_EXPECT(!tuple_has_v<int, int>);
    ZSTATIC_EXPECT(!tuple_any_of_v<int, std::is_integral>);
    ZSTATIC_EXPECT(tuple_count_of_v<int, std::is_integral> == 0U);
}

};  // ZEST_SUITE(support_tuple_traits)

}  // namespace

}  // namespace kota
