#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

#include "kotatsu/support/config.h"
#include "kotatsu/support/type_traits.h"

/// Type lists, each operation one pack expansion rather than a recursion over the list, nor a
/// fold, which nests as deep as the list is long.
namespace kotatsu {

template <typename T, typename... Ts>
concept is_one_of = (std::is_same_v<T, Ts> || ...);

template <typename... Ts>
struct type_list {};

template <typename List, typename T>
constexpr inline bool type_list_contains_v = false;

template <typename... Ts, typename T>
constexpr inline bool type_list_contains_v<type_list<Ts...>, T> = is_one_of<T, Ts...>;

/// Whether the list holds a specialization of HKT.
template <typename List, template <typename...> typename HKT>
constexpr inline bool type_list_has_spec_v = false;

template <typename... Ts, template <typename...> typename HKT>
constexpr inline bool type_list_has_spec_v<type_list<Ts...>, HKT> =
    (is_specialization_of<HKT, Ts> || ...);

template <typename List>
constexpr inline std::size_t type_list_size_v = 0;

template <typename... Ts>
constexpr inline std::size_t type_list_size_v<type_list<Ts...>> = sizeof...(Ts);

namespace detail {

#if !KOTATSU_HAS_TYPE_PACK_ELEMENT
template <std::size_t I, typename T>
struct indexed_type {
    using type = T;
};

template <typename Seq, typename... Ts>
struct indexed_types;

template <std::size_t... Is, typename... Ts>
struct indexed_types<std::index_sequence<Is...>, Ts...> : indexed_type<Is, Ts>... {};

template <std::size_t I, typename T>
indexed_type<I, T> select_indexed(const indexed_type<I, T>&);
#endif

/// The I-th of Ts, in one instantiation however many they are: the compiler's own pack
/// indexing where it has one (clang, GCC), else the one base of an indexed class the index
/// selects (MSVC).
template <std::size_t I, typename... Ts>
auto list_element(type_list<Ts...>)
#if KOTATSU_HAS_TYPE_PACK_ELEMENT
    -> std::type_identity<__type_pack_element<I, Ts...>>;
#else
    -> decltype(select_indexed<I>(indexed_types<std::index_sequence_for<Ts...>, Ts...>{}));
#endif

/// Where the first of flags is set; the last of them is, past those a list tests.
template <std::size_t N>
consteval std::size_t first_set(const bool (&flags)[N]) {
    std::size_t i = 0;
    while(!flags[i]) {
        ++i;
    }
    return i;
}

}  // namespace detail

template <std::size_t I, typename List>
using type_list_element_t = typename decltype(detail::list_element<I>(List{}))::type;

namespace detail {

/// Where an element of a concatenation of lists comes from: its list, and its place there.
struct list_place {
    std::size_t list;
    std::size_t at;
};

template <std::size_t Total, std::size_t N>
consteval std::array<list_place, Total> list_places(const std::size_t (&sizes)[N]) {
    std::array<list_place, Total> places{};
    std::size_t out = 0;
    for(std::size_t list = 0; list < N; ++list) {
        for(std::size_t at = 0; at < sizes[list]; ++at) {
            places[out++] = {list, at};
        }
    }
    return places;
}

template <typename... Lists>
constexpr inline auto concat_places =
    list_places<(0 + ... + type_list_size_v<Lists>)>({type_list_size_v<Lists>..., 0});

/// Element J of the concatenation of Lists.
template <std::size_t J, typename... Lists>
using concat_element =
    type_list_element_t<concat_places<Lists...>[J].at,
                        type_list_element_t<concat_places<Lists...>[J].list, type_list<Lists...>>>;

template <typename... Lists, std::size_t... Js>
auto list_concat(std::index_sequence<Js...>) -> type_list<concat_element<Js, Lists...>...>;

/// Where T first appears among Us.
template <typename T, typename... Us>
constexpr inline std::size_t first_of = first_set({std::is_same_v<T, Us>..., true});

template <template <typename...> typename HKT, typename... Ts>
auto list_find_spec(type_list<Ts...>)
    -> decltype(list_element<first_set({is_specialization_of<HKT, Ts>..., true})>(
        type_list<Ts..., void>{}));

}  // namespace detail

/// The lists, one after another, picked element by element in one pack expansion.
template <typename... Lists>
using type_list_concat_t = decltype(detail::list_concat<Lists...>(
    std::make_index_sequence<(0 + ... + type_list_size_v<Lists>)>{}));

namespace detail {

template <template <typename> typename Predicate, typename... Ts>
auto list_filter(type_list<Ts...>)
    -> type_list_concat_t<std::conditional_t<Predicate<Ts>::value, type_list<Ts>, type_list<>>...>;

template <typename... Ts, typename... Us, std::size_t... Is>
auto list_unique(type_list<Ts...>, type_list<Us...>, std::index_sequence<Is...>)
    -> type_list_concat_t<
        std::conditional_t<first_of<Ts, Us...> == Is, type_list<Ts>, type_list<>>...>;

}  // namespace detail

/// The types of List for which Predicate holds, in order.
template <typename List, template <typename> typename Predicate>
using type_list_filter_t = decltype(detail::list_filter<Predicate>(List{}));

/// The types of List, each where it first appears.
template <typename List>
using type_list_unique_t =
    decltype(detail::list_unique(List{},
                                 List{},
                                 std::make_index_sequence<type_list_size_v<List>>{}));

/// The first specialization of HKT in List; void when there is none.
template <typename List, template <typename...> typename HKT>
using type_list_find_spec_t = typename decltype(detail::list_find_spec<HKT>(List{}))::type;

template <typename List>
struct type_list_to_union;

template <>
struct type_list_to_union<type_list<>> {
    using type = void;
};

template <typename T>
struct type_list_to_union<type_list<T>> {
    using type = T;
};

template <typename... Ts>
    requires (sizeof...(Ts) > 1)
struct type_list_to_union<type_list<Ts...>> {
    using type = std::variant<Ts...>;
};

/// Nothing (void) for no type, the type for one, a variant of them for more.
template <typename List>
using type_list_to_union_t = typename type_list_to_union<List>::type;

}  // namespace kotatsu
