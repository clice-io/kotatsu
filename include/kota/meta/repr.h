#pragma once

#include "type_kind.h"
#include "kota/support/type_traits.h"

namespace kota::meta {

/// Declares the serialized representation of a type: what the codec reads and
/// writes for T on every backend, and how T appears to every schema consumer
/// (JSON Schema export, flatbuffers layout, zero-copy views). Specialize it
/// for your own types; the primary template is intentionally undefined.
///
/// Declarative form (preferred — works on all backends, full schema support):
///
///     template <>
///     struct kota::meta::repr<RelationKind> {
///         using type = std::uint32_t;
///         static type to(RelationKind k);    // encode: T -> type
///         static RelationKind from(type v);  // decode: type -> T
///     };
///
/// Either direction may be omitted; using the missing direction is a compile
/// error. Signatures are checked against `type` at the call site.
///
/// Imperative form (works on all backends; the body drives the visitor, and
/// must write and read the shape `type` declares, which flatbuffers lays the
/// value out by):
///
///     template <>
///     struct kota::meta::repr<Weird> {
///         using type = std::string;  // still required: schema stays honest
///         template <typename Config>
///         static bool serialize(auto& vis, const Weird& w);
///         template <typename Config>
///         static bool deserialize(auto& vis, Weird& w);
///     };
///
/// Use `using type = meta::dynamic;` when the shape is only known at runtime:
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
///     struct kota::meta::repr<Roaring, codec::fbs::format> {
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

namespace detail {

template <typename Repr>
constexpr auto declared_repr_impl() {
    if constexpr(requires { typename Repr::type; }) {
        return std::type_identity<typename Repr::type>{};
    } else {
        static_assert(dependent_false<Repr>,
                      "a meta::repr specialization or behavior::with adapter must declare its "
                      "representation via `using type = ...` (meta::dynamic when only known at "
                      "runtime)");
        return std::type_identity<void>{};
    }
}

}  // namespace detail

/// The representation a repr specialization or with-adapter declares; a
/// missing `type` member is a hard error with the protocol diagnostic.
/// Declared representations are not final: meta::resolved_repr_t follows
/// chained reprs and annotations nested inside them to the resolved type.
template <typename Repr>
using declared_repr_t = typename decltype(detail::declared_repr_impl<Repr>())::type;

}  // namespace kota::meta
