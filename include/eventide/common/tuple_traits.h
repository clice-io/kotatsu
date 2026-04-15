#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>

#include "meta.h"

namespace eventide {

namespace detail {

template <template <typename> class Pred, typename... Ts>
constexpr bool pack_any_v = (Pred<Ts>::value || ...);

template <template <typename> class Pred>
constexpr bool pack_any_v<Pred> = false;

template <template <typename> class Pred, typename... Ts>
constexpr std::size_t pack_count_v = (static_cast<std::size_t>(Pred<Ts>::value) + ...);

template <template <typename> class Pred>
constexpr std::size_t pack_count_v<Pred> = 0;

template <template <typename> class Pred, typename... Ts>
struct pack_find {
    using type = void;
};

template <template <typename> class Pred, typename First, typename... Rest>
struct pack_find<Pred, First, Rest...> {
    using type =
        std::conditional_t<Pred<First>::value, First, typename pack_find<Pred, Rest...>::type>;
};

template <template <typename...> typename HKT, typename... Ts>
struct pack_find_spec {
    using type = void;
};

template <template <typename...> typename HKT, typename First, typename... Rest>
struct pack_find_spec<HKT, First, Rest...> {
    using type = std::conditional_t<is_specialization_of<HKT, First>,
                                    First,
                                    typename pack_find_spec<HKT, Rest...>::type>;
};

}  // namespace detail

template <typename Tuple, template <typename> class Pred>
constexpr bool tuple_any_of_v = false;

template <template <typename> class Pred, typename... Ts>
constexpr bool tuple_any_of_v<std::tuple<Ts...>, Pred> = detail::pack_any_v<Pred, Ts...>;

template <typename Tuple, typename Tag>
constexpr bool tuple_has_v = false;

template <typename Tag, typename... Ts>
constexpr bool tuple_has_v<std::tuple<Ts...>, Tag> = (std::is_same_v<Ts, Tag> || ...);

template <typename Tuple, template <typename...> typename HKT>
constexpr bool tuple_has_spec_v = false;

template <template <typename...> typename HKT, typename... Ts>
constexpr bool tuple_has_spec_v<std::tuple<Ts...>, HKT> = (is_specialization_of<HKT, Ts> || ...);

template <typename Tuple, template <typename> class Pred>
constexpr std::size_t tuple_count_of_v = 0;

template <template <typename> class Pred, typename... Ts>
constexpr std::size_t tuple_count_of_v<std::tuple<Ts...>, Pred> = detail::pack_count_v<Pred, Ts...>;

template <typename Tuple, template <typename> class Pred>
struct tuple_find;

template <template <typename> class Pred, typename... Ts>
struct tuple_find<std::tuple<Ts...>, Pred> : detail::pack_find<Pred, Ts...> {};

template <typename Tuple, template <typename> class Pred>
using tuple_find_t = typename tuple_find<Tuple, Pred>::type;

template <typename Tuple, template <typename...> typename HKT>
struct tuple_find_spec;

template <template <typename...> typename HKT, typename... Ts>
struct tuple_find_spec<std::tuple<Ts...>, HKT> : detail::pack_find_spec<HKT, Ts...> {};

template <typename Tuple, template <typename...> typename HKT>
using tuple_find_spec_t = typename tuple_find_spec<Tuple, HKT>::type;

}  // namespace eventide
