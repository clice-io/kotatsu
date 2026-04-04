#pragma once

#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

#include "eventide/serde/serde/attrs/behavior.h"
#include "eventide/serde/serde/attrs/schema.h"

namespace eventide::serde {

// Wrap or inherit depending on whether T is an aggregate class.
template <typename T>
concept wrap_type = !std::is_class_v<T> || std::is_final_v<T>;

// Aggregate class that can be inherited without losing aggregate-ness.
template <typename T>
concept inherit_type = std::is_aggregate_v<T> && !wrap_type<T>;

// Non-aggregate class where inheriting and reusing constructors is desired.
template <typename T>
concept inherit_use_type = !std::is_aggregate_v<T> && !wrap_type<T>;

template <typename T, typename... Attrs>
struct annotation;

template <wrap_type T, typename... Attrs>
struct annotation<T, Attrs...> {
    static_assert(detail::validate_attrs<std::tuple<Attrs...>>(), "Invalid attribute combination");

    T value;

    constexpr annotation() = default;

    template <typename U>
        requires (!std::same_as<std::remove_cvref_t<U>, annotation> &&
                  std::constructible_from<T, U>)
    constexpr annotation(U&& raw) : value(std::forward<U>(raw)) {}

    operator T&() {
        return value;
    }

    operator const T&() const {
        return value;
    }

    template <typename U>
        requires (!std::same_as<std::remove_cvref_t<U>, annotation> && std::assignable_from<T&, U>)
    constexpr annotation& operator=(U&& raw) {
        value = std::forward<U>(raw);
        return *this;
    }

    using annotated_type = T;
    using attrs = std::tuple<Attrs...>;
};

template <inherit_type T, typename... Attrs>
struct annotation<T, Attrs...> : T {
    static_assert(detail::validate_attrs<std::tuple<Attrs...>>(), "Invalid attribute combination");

    using annotated_type = T;
    using attrs = std::tuple<Attrs...>;
};

template <inherit_use_type T, typename... Attrs>
struct annotation<T, Attrs...> : T {
    static_assert(detail::validate_attrs<std::tuple<Attrs...>>(), "Invalid attribute combination");

    using T::T;

    template <typename U>
        requires (!std::same_as<std::remove_cvref_t<U>, annotation> && std::assignable_from<T&, U>)
    constexpr annotation& operator=(U&& raw) {
        T::operator=(std::forward<U>(raw));
        return *this;
    }

    using annotated_type = T;
    using attrs = std::tuple<Attrs...>;
};

template <typename T>
concept annotated_type = requires {
    typename std::remove_cvref_t<T>::annotated_type;
    typename std::remove_cvref_t<T>::attrs;
};

template <annotated_type Value>
constexpr decltype(auto) annotated_value(Value&& value) {
    using annotation_t = std::remove_cvref_t<Value>;
    using underlying_t = typename annotation_t::annotated_type;
    if constexpr(std::is_const_v<std::remove_reference_t<Value>>) {
        return static_cast<const underlying_t&>(value);
    } else {
        return static_cast<underlying_t&>(value);
    }
}

template <typename T>
using annotated_underlying_t = typename std::remove_cvref_t<T>::annotated_type;

template <annotated_type L, annotated_type R>
    requires requires(const annotated_underlying_t<L>& lhs, const annotated_underlying_t<R>& rhs) {
        { lhs == rhs } -> std::convertible_to<bool>;
    }
constexpr auto operator==(const L& lhs, const R& rhs) -> bool {
    return annotated_value(lhs) == annotated_value(rhs);
}

template <annotated_type L, typename R>
    requires (!annotated_type<R> &&
              requires(const annotated_underlying_t<L>& lhs, const R& rhs) {
                  { lhs == rhs } -> std::convertible_to<bool>;
              })
constexpr auto operator==(const L& lhs, const R& rhs) -> bool {
    return annotated_value(lhs) == rhs;
}

template <typename L, annotated_type R>
    requires (!annotated_type<L> &&
              requires(const L& lhs, const annotated_underlying_t<R>& rhs) {
                  { lhs == rhs } -> std::convertible_to<bool>;
              })
constexpr auto operator==(const L& lhs, const R& rhs) -> bool {
    return lhs == annotated_value(rhs);
}

/// Convenience alias: annotation<T, schema::default_value>.
/// Marks a field as allowed to be absent during deserialization,
/// keeping its default-constructed value. Like Rust's #[serde(default)].
template <typename T>
using defaulted = annotation<T, schema::default_value>;

}  // namespace eventide::serde
