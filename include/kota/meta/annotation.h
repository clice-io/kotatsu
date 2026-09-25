#pragma once

#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

#include "attrs.h"
#include "kota/support/naming.h"

namespace kota::meta {

template <typename T>
concept wrap_type = !std::is_class_v<T> || std::is_final_v<T>;

template <typename T>
concept inherit_type = std::is_aggregate_v<T> && !wrap_type<T>;

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

    constexpr annotation() = default;

    // An inherited constructor never copies or moves from T itself, so
    // `.field = std::move(value)` needs these.
    constexpr annotation(const T& raw)
        requires std::copy_constructible<T>
        : T(raw) {}

    constexpr annotation(T&& raw) : T(std::move(raw)) {}

    template <typename U>
        requires (!std::same_as<std::remove_cvref_t<U>, annotation> && std::assignable_from<T&, U>)
    constexpr annotation& operator=(U&& raw) {
        T::operator=(std::forward<U>(raw));
        return *this;
    }

    using annotated_type = T;
    using attrs = std::tuple<Attrs...>;
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

namespace rename_policy = naming::rename_policy;

namespace detail {

/// Maps a DSL type component to the equivalent behavior attr.
template <typename Component>
struct component_attr;

template <typename Target>
struct component_attr<dsl::type_component<dsl::aspect::as, Target>> {
    using type = behavior::as<Target>;
};

template <typename Adapter>
struct component_attr<dsl::type_component<dsl::aspect::with, Adapter>> {
    using type = behavior::with<Adapter>;
};

template <typename Pred>
struct component_attr<dsl::type_component<dsl::aspect::skip_if, Pred>> {
    using type = behavior::skip_if<Pred>;
};

template <typename Policy>
struct component_attr<dsl::type_component<dsl::aspect::enum_string, Policy>> {
    using type = behavior::enum_string<Policy>;
};

template <typename T, typename Tag, typename Extras, typename... Extra>
struct annotated_field;

template <typename T, typename Tag, typename... Cs, typename... Extra>
struct annotated_field<T, Tag, std::tuple<Cs...>, Extra...> {
    using type = annotation<T, attrs::spec<Tag>, typename component_attr<Cs>::type..., Extra...>;
};

}  // namespace detail

/// A tag whose spec is a struct-level annotation (made by make_struct_spec)
/// rather than a field spec.
template <typename Tag>
concept struct_spec_tag = std::same_as<std::remove_cv_t<decltype(Tag::spec)>, struct_spec>;

/// Binds a KOTATSU_ANNOTATE tag to the field type it annotates. Extra type
/// attrs (behavior, ...) may follow the field type explicitly.
template <typename Tag>
struct annotate {
    template <typename T, typename... Extra>
    using type = typename detail::
        annotated_field<T, Tag, typename decltype(Tag::spec)::extras, Extra...>::type;
};

/// Binds a make_struct_spec tag to the struct or variant type it annotates.
template <struct_spec_tag Tag>
struct annotate<Tag> {
    template <typename T, typename... Extra>
    using type = annotation<T, attrs::struct_spec<Tag>, Extra...>;
};

}  // namespace kota::meta
