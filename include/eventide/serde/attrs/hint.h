#pragma once

#include <tuple>
#include <type_traits>

#include "eventide/common/meta.h"

namespace eventide::serde {

/// A hint attribute is transparent to the core serde framework.
/// Backends query hints by their own tag type and interpret them freely.
template <typename BackendTag, typename... KV>
struct hint {
    using backend_tag = BackendTag;
    using params = std::tuple<KV...>;
};

/// True for any hint<...> specialization.
template <typename T>
constexpr bool is_hint_attr_v = is_specialization_of<hint, T>;

namespace detail {

template <typename BackendTag, typename T>
constexpr bool hint_matches_v = false;

template <typename BackendTag, typename... KV>
constexpr bool hint_matches_v<BackendTag, hint<BackendTag, KV...>> = true;

template <typename BackendTag, typename... Attrs>
struct find_hint_impl {
    using type = void;
};

template <typename BackendTag, typename First, typename... Rest>
struct find_hint_impl<BackendTag, First, Rest...> {
    using type = std::conditional_t<hint_matches_v<BackendTag, First>,
                                    First,
                                    typename find_hint_impl<BackendTag, Rest...>::type>;
};

}  // namespace detail

/// Get the hint for a specific backend from an attrs tuple.
/// Returns void if no matching hint exists.
template <typename BackendTag, typename AttrsTuple>
struct get_hint;

template <typename BackendTag, typename... Attrs>
struct get_hint<BackendTag, std::tuple<Attrs...>> : detail::find_hint_impl<BackendTag, Attrs...> {};

template <typename BackendTag, typename AttrsTuple>
using get_hint_t = typename get_hint<BackendTag, AttrsTuple>::type;

}  // namespace eventide::serde
