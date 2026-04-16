#pragma once

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace eventide {

template <typename... Ts>
struct type_list {};

template <typename List, typename T>
struct type_list_prepend;

template <typename... Ts, typename T>
struct type_list_prepend<type_list<Ts...>, T> {
    using type = type_list<T, Ts...>;
};

template <typename List, typename T>
using type_list_prepend_t = typename type_list_prepend<List, T>::type;

template <typename List, typename T>
struct type_list_contains;

template <typename... Ts, typename T>
struct type_list_contains<type_list<Ts...>, T> :
    std::bool_constant<(std::same_as<T, Ts> || ...)> {};

template <typename List, typename T>
constexpr inline bool type_list_contains_v = type_list_contains<List, T>::value;

template <typename List, template <typename> typename Predicate>
struct type_list_filter;

template <template <typename> typename Predicate>
struct type_list_filter<type_list<>, Predicate> {
    using type = type_list<>;
};

template <typename T, typename... Ts, template <typename> typename Predicate>
struct type_list_filter<type_list<T, Ts...>, Predicate> {
private:
    using tail = typename type_list_filter<type_list<Ts...>, Predicate>::type;

public:
    using type = std::conditional_t<Predicate<T>::value, type_list_prepend_t<tail, T>, tail>;
};

template <typename List, template <typename> typename Predicate>
using type_list_filter_t = typename type_list_filter<List, Predicate>::type;

template <typename List>
struct type_list_unique;

template <typename Accum, typename List>
struct type_list_unique_impl;

template <>
struct type_list_unique<type_list<>> {
    using type = type_list<>;
};

template <typename... Ts>
struct type_list_unique_impl<type_list<Ts...>, type_list<>> {
    using type = type_list<Ts...>;
};

template <typename... Ts, typename T, typename... Rest>
struct type_list_unique_impl<type_list<Ts...>, type_list<T, Rest...>> {
private:
    using next = std::conditional_t<type_list_contains_v<type_list<Ts...>, T>,
                                    type_list<Ts...>,
                                    type_list<Ts..., T>>;

public:
    using type = typename type_list_unique_impl<next, type_list<Rest...>>::type;
};

template <typename... Ts>
struct type_list_unique<type_list<Ts...>> {
    using type = typename type_list_unique_impl<type_list<>, type_list<Ts...>>::type;
};

template <typename List>
using type_list_unique_t = typename type_list_unique<List>::type;

template <std::size_t I, typename List>
struct type_list_element;

template <std::size_t I, typename First, typename... Rest>
struct type_list_element<I, type_list<First, Rest...>> :
    type_list_element<I - 1, type_list<Rest...>> {};

template <typename First, typename... Rest>
struct type_list_element<0, type_list<First, Rest...>> {
    using type = First;
};

template <std::size_t I, typename List>
using type_list_element_t = typename type_list_element<I, List>::type;

template <typename A, typename B>
struct type_list_cat;

template <typename... As, typename... Bs>
struct type_list_cat<type_list<As...>, type_list<Bs...>> {
    using type = type_list<As..., Bs...>;
};

template <typename A, typename B>
using type_list_cat_t = typename type_list_cat<A, B>::type;

template <typename... Lists>
struct type_list_concat;

template <>
struct type_list_concat<> {
    using type = type_list<>;
};

template <typename List>
struct type_list_concat<List> {
    using type = List;
};

template <typename First, typename Second, typename... Rest>
struct type_list_concat<First, Second, Rest...> :
    type_list_concat<type_list_cat_t<First, Second>, Rest...> {};

template <typename... Lists>
using type_list_concat_t = typename type_list_concat<Lists...>::type;

template <typename List>
struct type_list_size;

template <typename... Ts>
struct type_list_size<type_list<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)> {};

template <typename List>
constexpr inline std::size_t type_list_size_v = type_list_size<List>::value;

}  // namespace eventide
