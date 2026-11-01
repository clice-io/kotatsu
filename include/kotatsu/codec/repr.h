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
///         static bool encode(auto& vis, const Weird& w);
///         static bool decode(auto& vis, Weird& w);
///     };
///
/// The visitor carries the format the value is read or written under, which
/// codec::encode_value(vis, part) and codec::decode_value(vis, part) apply
/// to the values the body is made of.
///
/// Use `using type = dynamic;` when the shape is only known at runtime:
/// schema output degrades to "any", and layout-computed backends
/// (flatbuffers) reject the type at compile time.
///
/// The second parameter scopes a specialization to a format: `repr<T, F>`
/// applies under F and under every format that extends it (codec::extend), the
/// nearest along the chain first, and wins over `repr<T>`, which applies under
/// every format.
///
///     template <>
///     struct kotatsu::codec::repr<Roaring, codec::fbs::format> {
///         using type = std::vector<std::byte>;  // flatbuffers only
///         ...
///     };
///
/// A field's `with` or `as` attribute takes priority over its type's repr.
template <typename T, typename Format = void>
struct repr;

namespace detail {

template <typename T, typename F>
constexpr auto repr_lookup() {
    if constexpr(requires { sizeof(repr<T, F>); }) {
        return std::type_identity<repr<T, F>>{};
    } else if constexpr(requires { typename F::base; }) {
        return repr_lookup<T, typename F::base>();
    } else if constexpr(requires { sizeof(repr<T>); }) {
        return std::type_identity<repr<T>>{};
    } else {
        return std::type_identity<void>{};
    }
}

}  // namespace detail

/// True when a repr applies to T under the format F: one for F or a format it extends, the
/// nearest first, else the one for every format.
template <typename T, typename F>
concept has_repr = !std::is_void_v<typename decltype(detail::repr_lookup<T, F>())::type>;

/// The repr that applies to T under the format F.
template <typename T, typename F>
    requires has_repr<T, F>
using repr_for = typename decltype(detail::repr_lookup<T, F>())::type;

/// The repr of an enum that travels as the names of its enumerators, spelled by Policy (a
/// naming::rename_policy); without one, by the rename_all of the enum's own annotation, else
/// the format's. The names are computed at compile time and must be distinct; a document is
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
/// Declared representations are not final: resolved_repr_t follows chained
/// reprs, and the annotations of the types they declare, to the type that
/// travels.
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
