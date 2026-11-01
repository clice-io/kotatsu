#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "attrs.h"
#include "struct.h"

namespace kotatsu::meta {

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

}  // namespace detail

/// Whether KANNOTATE or KANNOTATE_FIELD annotates field I of T.
template <typename T, std::size_t I>
concept field_annotated = detail::annotated_in_class<T, I> || detail::annotated_out_of_class<T, I>;

namespace detail {

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
    if constexpr(field_annotated<T, I>) {
        return tag_attrs_t<field_annotation<T, I>>{};
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
