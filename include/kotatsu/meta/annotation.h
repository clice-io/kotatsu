#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <format>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "attrs.h"
#include "struct.h"
#include "kotatsu/support/naming.h"

namespace kotatsu::meta {

template <typename T>
concept wrap_type = !std::is_class_v<T> || std::is_final_v<T>;

template <typename T>
concept inherit_type = std::is_aggregate_v<T> && !wrap_type<T>;

template <typename T>
concept inherit_use_type = !std::is_aggregate_v<T> && !wrap_type<T>;

template <typename T, typename... Attrs>
struct annotation;

namespace detail {

/// The attrs of an annotation of `T`: `Attrs`, written around those of `T` when `T` is an
/// annotation itself, so that an annotation of an annotation is one annotation of the type
/// inside both, whose outer entries override the inner ones.
template <typename T, typename... Attrs>
using layered_attrs_t = compose_attrs_t<decltype(own_attrs<T>()), std::tuple<Attrs...>>;

}  // namespace detail

template <wrap_type T, typename... Attrs>
struct annotation<T, Attrs...> {
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

    // A wrapped type is no class to inherit from, so never an annotation itself.
    using annotated_type = T;
    using attrs = std::tuple<Attrs...>;
    static_assert(detail::validate_attrs<attrs>(), "Invalid attribute combination");
};

template <inherit_type T, typename... Attrs>
struct annotation<T, Attrs...> : T {
    using annotated_type = raw_type_t<T>;
    using attrs = detail::layered_attrs_t<T, Attrs...>;
    static_assert(detail::validate_attrs<std::tuple<Attrs...>>() && detail::validate_attrs<attrs>(),
                  "Invalid attribute combination");
};

template <inherit_use_type T, typename... Attrs>
struct annotation<T, Attrs...> : T {
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

    using annotated_type = raw_type_t<T>;
    using attrs = detail::layered_attrs_t<T, Attrs...>;
    static_assert(detail::validate_attrs<std::tuple<Attrs...>>() && detail::validate_attrs<attrs>(),
                  "Invalid attribute combination");
};

template <annotated_type Value>
constexpr decltype(auto) annotated_value(Value&& value) {
    using underlying_t = raw_type_t<std::remove_cvref_t<Value>>;
    if constexpr(std::is_const_v<std::remove_reference_t<Value>>) {
        return static_cast<const underlying_t&>(value);
    } else {
        return static_cast<underlying_t&>(value);
    }
}

template <annotated_type L, annotated_type R>
    requires requires(const raw_type_t<L>& lhs, const raw_type_t<R>& rhs) {
        { lhs == rhs } -> std::convertible_to<bool>;
    }
constexpr auto operator==(const L& lhs, const R& rhs) -> bool {
    return annotated_value(lhs) == annotated_value(rhs);
}

template <annotated_type L, typename R>
    requires (!annotated_type<R> &&
              requires(const raw_type_t<L>& lhs, const R& rhs) {
                  { lhs == rhs } -> std::convertible_to<bool>;
              })
constexpr auto operator==(const L& lhs, const R& rhs) -> bool {
    return annotated_value(lhs) == rhs;
}

template <typename L, annotated_type R>
    requires (!annotated_type<L> &&
              requires(const L& lhs, const raw_type_t<R>& rhs) {
                  { lhs == rhs } -> std::convertible_to<bool>;
              })
constexpr auto operator==(const L& lhs, const R& rhs) -> bool {
    return lhs == annotated_value(rhs);
}

namespace detail {

/// The behavior attr of an entry that carries a type; none for an entry that carries a value.
template <typename C>
constexpr auto type_component_attr(const C&) {
    return std::tuple<>{};
}

template <dsl::aspect A, typename T>
constexpr auto type_component_attr(const dsl::type_component<A, T>&) {
    if constexpr(A == dsl::aspect::as) {
        return std::tuple<behavior::as<T>>{};
    } else if constexpr(A == dsl::aspect::with) {
        return std::tuple<behavior::with<T>>{};
    } else if constexpr(A == dsl::aspect::skip_if) {
        return std::tuple<behavior::skip_if<T>>{};
    } else {
        return std::tuple<behavior::enum_string<T>>{};
    }
}

/// The attrs the entries `Cs` of the annotation `Tag` make: attrs::spec for its field and
/// extension entries, attrs::struct_spec for its struct entries, and the behavior attr of
/// each entry that carries a type.
template <typename Tag, typename... Cs>
constexpr auto tag_attrs(std::type_identity<std::tuple<Cs...>>) {
    constexpr bool field = (!dsl::spec_component<Cs, struct_spec> || ...);
    constexpr bool structure = (dsl::spec_component<Cs, struct_spec> || ...);
    return std::tuple_cat(
        std::conditional_t<field, std::tuple<attrs::spec<Tag>>, std::tuple<>>{},
        std::conditional_t<structure, std::tuple<attrs::struct_spec<Tag>>, std::tuple<>>{},
        decltype(type_component_attr(std::declval<const Cs&>())){}...);
}

template <typename T, typename Attrs, typename... Extra>
struct annotation_of;

template <typename T, typename... Attrs, typename... Extra>
struct annotation_of<T, std::tuple<Attrs...>, Extra...> {
    using type = annotation<T, Attrs..., Extra...>;
};

}  // namespace detail

/// Binds the annotation `Tag` (KANNOTATION, or a struct with static `entries` from
/// make_annotation) to the type it annotates, a field's or a struct's or variant's. Extra
/// type attrs (behavior, ...) may follow the type explicitly.
template <typename Tag>
struct annotate {
    template <typename T, typename... Extra>
    using type = typename detail::annotation_of<
        T,
        decltype(detail::tag_attrs<Tag>(
            std::type_identity<std::remove_const_t<decltype(Tag::entries)>>{})),
        Extra...>::type;
};

namespace detail {

/// A field's name as a template argument.
template <std::size_t N>
struct field_key_text {
    char text[N + 1] = {};

    consteval field_key_text(const char (&name)[N + 1]) {
        std::ranges::copy(name, text);
    }

    consteval explicit field_key_text(std::string_view name) {
        std::ranges::copy(name, text);
    }
};

template <std::size_t N>
field_key_text(const char (&)[N]) -> field_key_text<N - 1>;

}  // namespace detail

/// The key KANNOTATE and KANNOTATE_FIELD file a field's annotation under: the field's name.
/// It appears only in consteval functions, so it is neither emitted nor mangled.
template <detail::field_key_text Name>
struct name_key {};

namespace detail {

/// Declared so that the unqualified call below finds KANNOTATE_FIELD's by argument-dependent
/// lookup, in the namespace of the type it annotates.
void __kotatsu__field_annotation() = delete;

template <typename T, std::size_t I>
consteval auto key_of() {
    constexpr std::string_view name = field_name<I, T>();
    return name_key<field_key_text<name.size()>(name)>{};
}

template <typename T, std::size_t I>
concept annotated_in_class = requires { T::__kotatsu__annotation(key_of<T, I>()); };

template <typename T, std::size_t I>
concept annotated_out_of_class =
    requires { __kotatsu__field_annotation(std::type_identity<T>{}, key_of<T, I>()); };

/// The annotation KANNOTATE or KANNOTATE_FIELD gives field I of T, as a tag.
template <typename T, std::size_t I>
struct field_annotation {
    static_assert(!(annotated_in_class<T, I> && annotated_out_of_class<T, I>),
                  "a field is annotated both in its class and out of it");

    constexpr static auto entries = [] {
        if constexpr(annotated_in_class<T, I>) {
            return T::__kotatsu__annotation(key_of<T, I>());
        } else {
            return __kotatsu__field_annotation(std::type_identity<T>{}, key_of<T, I>());
        }
    }();
};

template <typename T, std::size_t I>
constexpr auto named_attrs() {
    if constexpr(annotated_in_class<T, I> || annotated_out_of_class<T, I>) {
        using tag = field_annotation<T, I>;
        return tag_attrs<tag>(std::type_identity<std::remove_const_t<decltype(tag::entries)>>{});
    } else {
        return std::tuple<>{};
    }
}

}  // namespace detail

/// The attrs field I of T carries: those of its type (attrs_of_t), under the field's own
/// annotation, whose entries override them.
template <typename T, std::size_t I>
using field_attrs_t =
    compose_attrs_t<attrs_of_t<field_type<T, I>>, decltype(detail::named_attrs<T, I>())>;

/// The value spec field I of T carries (field_attrs_t).
template <typename T, std::size_t I>
constexpr const inline field_spec& field_spec_of = spec_of<field_attrs_t<T, I>>;

/// The spec of type `Spec` an extension attached to field I of T, its type's own annotation
/// included (field_attrs_t); null when it has none.
template <typename Spec, typename T, std::size_t I>
constexpr const Spec* extension_of = extension_in<Spec, field_attrs_t<T, I>>;

}  // namespace kotatsu::meta

/// An annotated value formats as the value it annotates, through every
/// annotation around it, with that value's format spec. std::format looks for
/// an exact specialization, which neither a wrapped value nor a class it
/// inherits from has; an annotated string would otherwise take the range
/// formatter, or none.
template <typename T, typename... Attrs, typename Char>
    requires std::formattable<kotatsu::meta::raw_type_t<T>, Char>
struct std::formatter<kotatsu::meta::annotation<T, Attrs...>, Char> :
    std::formatter<kotatsu::meta::raw_type_t<T>, Char> {
    template <typename Context>
    auto format(const kotatsu::meta::annotation<T, Attrs...>& value, Context& ctx) const {
        return std::formatter<kotatsu::meta::raw_type_t<T>, Char>::format(
            kotatsu::meta::annotated_value(value),
            ctx);
    }
};
