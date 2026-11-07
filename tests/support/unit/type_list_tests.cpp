#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "kotatsu/zest/zest.h"
#include "kotatsu/support/type_list.h"

namespace kotatsu {

namespace {

using Numbers = type_list<int, double, int, char>;

template <std::size_t... Is>
type_list<std::integral_constant<std::size_t, Is>...> indices(std::index_sequence<Is...>);

using Indices = decltype(indices(std::make_index_sequence<1100>{}));

ZEST_SUITE(support_type_list) {

ZEST_CASE(is_one_of_finds_a_type_among_several) {
    ZSTATIC_EXPECT(is_one_of<int, char, int>);
    ZSTATIC_EXPECT(!is_one_of<long, char, int>);
    ZSTATIC_EXPECT(!is_one_of<int>);
}

ZEST_CASE(contains_and_size_read_the_list) {
    ZSTATIC_EXPECT(type_list_contains_v<Numbers, double>);
    ZSTATIC_EXPECT(!type_list_contains_v<Numbers, float>);
    ZSTATIC_EXPECT(type_list_size_v<Numbers> == 4U);
    ZSTATIC_EXPECT(type_list_size_v<type_list<>> == 0U);
    ZSTATIC_EXPECT(!type_list_contains_v<int, int>);
}

ZEST_CASE(element_is_found_by_index) {
    ZEXPECT(zest::type_eq<type_list_element_t<0, Numbers>, int>());
    ZEXPECT(zest::type_eq<type_list_element_t<3, Numbers>, char>());
    // Deeper than the compilers' default limit on recursive instantiation, 1024.
    ZEXPECT(zest::type_eq<type_list_element_t<1099, Indices>,
                          std::integral_constant<std::size_t, 1099>>());
}

ZEST_CASE(has_spec_and_find_spec_look_for_a_template) {
    using Mixed = type_list<int, std::optional<char>, std::vector<int>, std::optional<int>>;
    ZSTATIC_EXPECT(type_list_has_spec_v<Mixed, std::optional>);
    ZSTATIC_EXPECT(!type_list_has_spec_v<Mixed, std::variant>);
    ZSTATIC_EXPECT(!type_list_has_spec_v<type_list<>, std::optional>);
    ZEXPECT(zest::type_eq<type_list_find_spec_t<Mixed, std::optional>, std::optional<char>>());
    ZEXPECT(zest::type_eq<type_list_find_spec_t<Mixed, std::vector>, std::vector<int>>());
    ZEXPECT(zest::type_eq<type_list_find_spec_t<Mixed, std::variant>, void>());
    ZEXPECT(zest::type_eq<type_list_find_spec_t<type_list<>, std::optional>, void>());
}

ZEST_CASE(concatenate_keeps_the_order) {
    ZEXPECT(zest::type_eq<type_list_concat_t<>, type_list<>>());
    ZEXPECT(zest::type_eq<type_list_concat_t<Numbers>, Numbers>());
    ZEXPECT(zest::type_eq<type_list_concat_t<type_list<int>, type_list<>, type_list<char, bool>>,
                          type_list<int, char, bool>>());
}

ZEST_CASE(long_lists_are_concatenated_and_filtered) {
    // Longer than the compilers' default limit on recursive instantiation, 1024.
    using Twice = type_list_concat_t<Indices, Indices>;
    ZSTATIC_EXPECT(type_list_size_v<Twice> == 2200U);
    ZEXPECT(
        zest::type_eq<type_list_element_t<1100, Twice>, std::integral_constant<std::size_t, 0>>());
    ZEXPECT(zest::type_eq<type_list_filter_t<Indices, std::is_integral>, type_list<>>());
    ZSTATIC_EXPECT(type_list_contains_v<Indices, std::integral_constant<std::size_t, 1099>>);
    ZSTATIC_EXPECT(!type_list_has_spec_v<Indices, std::optional>);
}

ZEST_CASE(find_takes_the_first_match) {
    ZEXPECT(zest::type_eq<type_list_find_t<Numbers, std::is_floating_point>, double>());
    ZEXPECT(zest::type_eq<type_list_find_t<Numbers, std::is_pointer>, void>());
}

ZEST_CASE(filter_keeps_order) {
    ZEXPECT(
        zest::type_eq<type_list_filter_t<Numbers, std::is_integral>, type_list<int, int, char>>());
    ZEXPECT(zest::type_eq<type_list_filter_t<Numbers, std::is_pointer>, type_list<>>());
}

ZEST_CASE(unique_keeps_first_occurrences) {
    ZEXPECT(zest::type_eq<type_list_unique_t<Numbers>, type_list<int, double, char>>());
    ZEXPECT(zest::type_eq<type_list_unique_t<type_list<>>, type_list<>>());
}

ZEST_CASE(to_union_depends_on_the_count) {
    ZEXPECT(zest::type_eq<type_list_to_union_t<type_list<>>, void>());
    ZEXPECT(zest::type_eq<type_list_to_union_t<type_list<int>>, int>());
    ZEXPECT(zest::type_eq<type_list_to_union_t<type_list<int, std::string>>,
                          std::variant<int, std::string>>());
}

};  // ZEST_SUITE(support_type_list)

}  // namespace

}  // namespace kotatsu
