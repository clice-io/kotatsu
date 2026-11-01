#pragma once

#include <concepts>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "kotatsu/support/type_traits.h"
#include "kotatsu/meta/type_kind.h"

namespace kotatsu::codec {

/// Marker representation for repr: the shape is only known at runtime.
/// Maps to type_kind::any, so schema output degrades to "any".
struct dynamic {};

/// Declares the serialized representation of a type: what the codec reads and
/// writes for T on every backend, and how T appears to every schema consumer
/// (JSON Schema export, flatbuffers layout, zero-copy views). Specialize it
/// for your own types; the primary template is intentionally undefined.
///
/// Declarative form (preferred — works on all backends, full schema support):
///
///     template <>
///     struct kotatsu::codec::repr<RelationKind> {
///         using type = std::uint32_t;
///         static type to(RelationKind k);    // encode: T -> type
///         static RelationKind from(type v);  // decode: type -> T
///     };
///
/// Either direction may be omitted; using the missing direction is a compile
/// error. Signatures are checked against `type` at the call site.
///
/// Imperative form (works on all backends; the body drives the visitor in the
/// shape `type` declares, and can fail, which from() cannot: it returns false
/// after scoped_context<rich_error>::fail):
///
///     template <>
///     struct kotatsu::codec::repr<Weird> {
///         using type = std::string;  // still required: schema stays honest
///         template <typename Config>
///         static bool serialize(auto& vis, const Weird& w);
///         template <typename Config>
///         static bool deserialize(auto& vis, Weird& w);
///     };
///
/// Use `using type = dynamic;` when the shape is only known at runtime:
/// schema output degrades to "any", and layout-computed backends
/// (flatbuffers) reject the type at compile time.
///
/// The second parameter scopes a specialization to one serialization format:
/// each backend declares a `format` tag type and exposes it on its visitors,
/// and a `repr<T, Format>` specialization wins over the format-agnostic
/// `repr<T>` on that backend only. Backends without a tag — and every schema
/// consumer entered without one — see the format-agnostic repr.
///
///     template <>
///     struct kotatsu::codec::repr<Roaring, codec::fbs::format> {
///         using type = std::vector<std::byte>;  // flatbuffers only
///         ...
///     };
///
/// Per-field customization (behavior::with / behavior::as annotations) takes
/// priority over the field type's repr.
template <typename T, typename Format = void>
struct repr;

/// True when a repr applies to T under Format: a format-scoped
/// specialization, or the format-agnostic one as fallback.
template <typename T, typename Format = void>
concept has_repr = requires { sizeof(repr<T, Format>); } || requires { sizeof(repr<T>); };

namespace detail {

template <typename T, typename Format>
constexpr auto repr_for_impl() {
    if constexpr(requires { sizeof(repr<T, Format>); }) {
        return std::type_identity<repr<T, Format>>{};
    } else {
        return std::type_identity<repr<T>>{};
    }
}

}  // namespace detail

/// The repr specialization selected for T under Format: the format-scoped one
/// when present, the format-agnostic one otherwise.
template <typename T, typename Format>
    requires has_repr<T, Format>
using repr_for = typename decltype(detail::repr_for_impl<T, Format>())::type;

/// The repr of an enum that travels as the names of its enumerators, spelled by Policy (a
/// naming::rename_policy); without one, by the rename_all of the enum's own annotation, else
/// the config's. The names are computed at compile time and must be distinct; a document is
/// read by matching them exactly, and a value no enumerator has fails to write.
///
///     template <>
///     struct kotatsu::codec::repr<Level> : enum_names<> {};
///
///     KANNOTATE(&level, with = type<enum_names<naming::rename_policy::lower_camel>>)
///     Level level;
template <typename Policy = void>
struct enum_names {
    using type = std::string;
    using policy = Policy;
};

/// Whether Repr is enum_names, or a class derived from it, such as a repr specialization.
template <typename Repr>
concept names_enum = requires { typename Repr::policy; } &&
                     std::derived_from<Repr, enum_names<typename Repr::policy>>;

namespace detail {

template <typename Repr>
constexpr auto declared_repr_impl() {
    if constexpr(requires { typename Repr::type; }) {
        return std::type_identity<typename Repr::type>{};
    } else {
        static_assert(dependent_false<Repr>,
                      "a repr specialization or behavior::with adapter must declare its "
                      "representation via `using type = ...` (dynamic when only known at "
                      "runtime)");
        return std::type_identity<void>{};
    }
}

}  // namespace detail

/// The representation a repr specialization or with-adapter declares; a
/// missing `type` member is a hard error with the protocol diagnostic.
/// Declared representations are not final: resolved_repr_t follows
/// chained reprs and annotations nested inside them to the resolved type.
template <typename Repr>
using declared_repr_t = typename decltype(detail::declared_repr_impl<Repr>())::type;

/// How the text backends (json, toml, dyn) carry a char: its value, 0 to
/// 255, is one codepoint, written in UTF-8 — one byte below 0x80, two above.
inline std::string char_to_utf8(char c) {
    auto codepoint = static_cast<unsigned char>(c);
    if(codepoint < 0x80) {
        return std::string(1, c);
    }
    return {static_cast<char>(0xC0 | (codepoint >> 6)),
            static_cast<char>(0x80 | (codepoint & 0x3F))};
}

/// The char `text` spells as char_to_utf8 writes it, if it spells one: a
/// single codepoint up to U+00FF.
inline std::optional<char> char_from_utf8(std::string_view text) {
    auto byte = [&](std::size_t at) {
        return static_cast<unsigned char>(text[at]);
    };
    if(text.size() == 1 && byte(0) < 0x80) {
        return text[0];
    }
    if(text.size() == 2 && (byte(0) == 0xC2 || byte(0) == 0xC3) && (byte(1) & 0xC0) == 0x80) {
        return static_cast<char>(((byte(0) & 0x1F) << 6) | (byte(1) & 0x3F));
    }
    return std::nullopt;
}

/// What a text backend reports for a string char_from_utf8 rejects.
constexpr inline std::string_view invalid_char_message = "expected a single character up to U+00FF";

/// A document's value kept as its text, which a backend that writes text writes back as it is:
/// json reads it as the text of the value, whatever its shape, and writes it verbatim, an
/// empty one as null.
struct raw_value {
    std::string data;

    bool empty() const noexcept {
        return data.empty();
    }
};

}  // namespace kotatsu::codec

template <>
constexpr inline bool kotatsu::meta::runtime_shaped<kotatsu::codec::dynamic> = true;
