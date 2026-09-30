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
    STATIC_EXPECT(is_one_of<int, char, int>);
    STATIC_EXPECT(!is_one_of<long, char, int>);
    STATIC_EXPECT(!is_one_of<int>);
}

ZEST_CASE(contains_and_size_read_the_list) {
    STATIC_EXPECT(type_list_contains_v<Numbers, double>);
    STATIC_EXPECT(!type_list_contains_v<Numbers, float>);
    STATIC_EXPECT(type_list_size_v<Numbers> == 4U);
    STATIC_EXPECT(type_list_size_v<type_list<>> == 0U);
}

ZEST_CASE(element_is_found_by_index) {
    EXPECT(zest::type_eq<type_list_element_t<0, Numbers>, int>());
    EXPECT(zest::type_eq<type_list_element_t<3, Numbers>, char>());
}

ZEST_CASE(prepend_and_concatenate_keep_the_order) {
    EXPECT(zest::type_eq<type_list_prepend_t<type_list<int>, char>, type_list<char, int>>());
    EXPECT(zest::type_eq<type_list_cat_t<type_list<int>, type_list<char>>, type_list<int, char>>());
    EXPECT(zest::type_eq<type_list_concat_t<>, type_list<>>());
    EXPECT(zest::type_eq<type_list_concat_t<Numbers>, Numbers>());
    EXPECT(zest::type_eq<type_list_concat_t<type_list<int>, type_list<>, type_list<char, bool>>,
                         type_list<int, char, bool>>());
}

ZEST_CASE(filter_keeps_order) {
    EXPECT(
        zest::type_eq<type_list_filter_t<Numbers, std::is_integral>, type_list<int, int, char>>());
    EXPECT(zest::type_eq<type_list_filter_t<Numbers, std::is_pointer>, type_list<>>());
}

ZEST_CASE(unique_keeps_first_occurrences) {
    EXPECT(zest::type_eq<type_list_unique_t<Numbers>, type_list<int, double, char>>());
    EXPECT(zest::type_eq<type_list_unique_t<type_list<>>, type_list<>>());
}

ZEST_CASE(to_union_depends_on_the_count) {
    EXPECT(zest::type_eq<type_list_to_union_t<type_list<>>, void>());
    EXPECT(zest::type_eq<type_list_to_union_t<type_list<int>>, int>());
    EXPECT(zest::type_eq<type_list_to_union_t<type_list<int, std::string>>,
                         std::variant<int, std::string>>());
}

};  // ZEST_SUITE(support_type_list)

}  // namespace

}  // namespace kota
