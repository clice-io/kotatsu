#pragma once

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

#include "kotatsu/support/config.h"
#include "kotatsu/support/type_traits.h"

/// Type lists, each operation one fold or one pack expansion rather than a recursion over the
/// list. A fold nests as deep as its operands are many, which clang limits to 256 by default
/// (-fbracket-depth): concatenate, filter and unique take lists shorter than that.
namespace kotatsu {

template <typename T, typename... Ts>
concept is_one_of = (std::same_as<T, Ts> || ...);

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

/// A list as the operand of a fold, whose `+` appends a list and whose `|` appends a type the
/// list does not hold yet.
template <typename... Ts>
struct list_fold {
    using type = type_list<Ts...>;
};

template <typename T>
struct list_item {};

template <typename... As, typename... Bs>
list_fold<As..., Bs...> operator+(list_fold<As...>, type_list<Bs...>);

template <typename... Ts, typename T>
std::conditional_t<is_one_of<T, Ts...>, list_fold<Ts...>, list_fold<Ts..., T>>
    operator|(list_fold<Ts...>, list_item<T>);

template <typename... Ts>
auto list_unique(type_list<Ts...>) -> decltype((list_fold<>{} | ... | list_item<Ts>{}));

template <template <typename> typename Predicate, typename... Ts>
auto list_filter(type_list<Ts...>)
    -> decltype((list_fold<>{} + ... +
                 std::conditional_t<Predicate<Ts>::value, type_list<Ts>, type_list<>>{}));

/// Where the first of flags is set; the last of them is, past those a list tests.
template <std::size_t N>
consteval std::size_t first_set(const bool (&flags)[N]) {
    std::size_t i = 0;
    while(!flags[i]) {
        ++i;
    }
    return i;
}

template <template <typename...> typename HKT, typename... Ts>
auto list_find_spec(type_list<Ts...>)
    -> decltype(list_element<first_set({is_specialization_of<HKT, Ts>..., true})>(
        type_list<Ts..., void>{}));

}  // namespace detail

template <std::size_t I, typename List>
using type_list_element_t = typename decltype(detail::list_element<I>(List{}))::type;

template <typename... Lists>
using type_list_concat_t = typename decltype((detail::list_fold<>{} + ... + Lists{}))::type;

/// The types of List for which Predicate holds, in order.
template <typename List, template <typename> typename Predicate>
using type_list_filter_t = typename decltype(detail::list_filter<Predicate>(List{}))::type;

/// The types of List, each where it first appears.
template <typename List>
using type_list_unique_t = typename decltype(detail::list_unique(List{}))::type;

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
