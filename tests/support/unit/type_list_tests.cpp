#include <string>
#include <type_traits>
#include <variant>

#include "kota/zest/zest.h"
#include "kota/support/type_list.h"

namespace kota {

namespace {

using Numbers = type_list<int, double, int, char>;

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
}

ZEST_CASE(element_is_found_by_index) {
    ZEXPECT(zest::type_eq<type_list_element_t<0, Numbers>, int>());
    ZEXPECT(zest::type_eq<type_list_element_t<3, Numbers>, char>());
}

ZEST_CASE(prepend_and_concatenate_keep_the_order) {
    ZEXPECT(zest::type_eq<type_list_prepend_t<type_list<int>, char>, type_list<char, int>>());
    ZEXPECT(
        zest::type_eq<type_list_cat_t<type_list<int>, type_list<char>>, type_list<int, char>>());
    ZEXPECT(zest::type_eq<type_list_concat_t<>, type_list<>>());
    ZEXPECT(zest::type_eq<type_list_concat_t<Numbers>, Numbers>());
    ZEXPECT(zest::type_eq<type_list_concat_t<type_list<int>, type_list<>, type_list<char, bool>>,
                          type_list<int, char, bool>>());
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

}  // namespace kota
