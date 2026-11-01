#pragma once

#include <concepts>
#include <optional>
#include <type_traits>

#include "kotatsu/support/naming.h"
#include "kotatsu/codec/attribute.h"
#include "kotatsu/codec/context.h"

namespace kotatsu::codec {

/// How the codec maps values to a backend's documents, decided at compile time: the root of
/// every format. A backend's format (json::format, bincode::format, ...) adds what the backend
/// can do; a format of your own extends one and says what it changes:
///
///     struct lsp_format : codec::extend<json::format> {
///         using rename_all = naming::rename_policy::lower_camel;
///     };
///
/// and travels as the first template argument of the backend's entry points
/// (`json::to_string<lsp_format>(value)`). A repr<T, F> specialization applies under F and
/// every format that extends it.
struct format {
    /// How the names a type declares are spelled in a document: struct fields, enumerators
    /// that travel as names (enum_names) and the default tags of variant alternatives. A rename
    /// policy (naming::rename_policy); a type's own rename_all overrides it.
    using rename_all = naming::rename_policy::identity;
    /// The presence of every struct field nothing else gives one; unset, its kind decides.
    constexpr static std::optional<presence_kind> presence_all = std::nullopt;
    /// Whether documents carry names, a struct's fields and a variant's tags: true but for a
    /// binary backend's format. A format may turn it off, never on.
    constexpr static bool human_readable = true;
};

/// A format extending Base: what it declares hides Base's, the rest it inherits. The repr
/// specializations of each format along the chain apply under it.
template <typename Base>
struct extend : Base {
    using base = Base;
};

/// What a backend whose documents are UTF-8 text (json, toml) does with a
/// string that is not UTF-8. Decoding never meets one: those backends' parsers
/// reject a document that is not UTF-8.
enum class invalid_utf8 {
    /// Fail encoding with an error.
    Error,
    /// Write it as a UTF-8 decoder reads it, each ill-formed sequence as
    /// U+FFFD.
    Replace,
};

/// How one encode goes, decided at run time.
struct encode_options {
    /// What a backend whose documents are UTF-8 text does with a string that is not UTF-8.
    codec::invalid_utf8 invalid_utf8 = codec::invalid_utf8::Error;
};

/// How one decode goes, decided at run time.
struct decode_options {
    /// Fail on a key no field of the struct answers to, in every struct, as a struct's own
    /// deny_unknown_fields does in that struct.
    bool deny_unknown_fields = false;
};

namespace detail {

/// The options of the encode under way: those its entry point was given, else the defaults.
const inline encode_options& current_encode_options() {
    constexpr static encode_options defaults{};
    auto* options = scoped_context<const encode_options>::try_current();
    return options ? *options : defaults;
}

/// The options of the decode under way: those its entry point was given, else the defaults.
const inline decode_options& current_decode_options() {
    constexpr static decode_options defaults{};
    auto* options = scoped_context<const decode_options>::try_current();
    return options ? *options : defaults;
}

template <typename Vis>
constexpr auto format_of_impl() {
    if constexpr(std::derived_from<Vis, format>) {
        // A format carries none: `Vis::format` would name its root, which MSVC
        // reads as the root's constructor.
        return std::type_identity<void>{};
    } else if constexpr(requires { typename Vis::format; }) {
        return std::type_identity<typename Vis::format>{};
    } else {
        return std::type_identity<void>{};
    }
}

}  // namespace detail

/// The format a visitor carries, which the entry point that made it was given;
/// void when it carries none.
template <typename Vis>
using format_of_t = typename decltype(detail::format_of_impl<Vis>())::type;

/// True when the visitor's format computes the output layout statically
/// (flatbuffers). Such backends cannot carry a codec::dynamic repr: there is
/// no layout to compute.
template <typename Vis>
concept layout_computed = requires { requires format_of_t<Vis>::layout_computed; };

/// True when the visitor's format writes every field of a struct, whatever
/// its presence: bincode writes the fields back to back with nothing marking
/// which are present, and fbs reads an absent slot as null, empty or zero.
template <typename Vis>
concept writes_every_field = requires { requires format_of_t<Vis>::writes_every_field; };

/// True for the format json::schema writes the documents it takes defaults
/// from under: it leaves out the fields annotated `schema_default = false`,
/// wherever they sit, where a field can be absent.
template <typename F>
concept omits_unstated_defaults = requires { requires F::omit_unstated_defaults; };

/// True when whoever drives the visitor has checked which fields the document
/// carries (cli, which reports a missing argument itself, and whose help
/// option ends a parse before anything is checked): decode leaves required
/// fields unchecked.
template <typename Vis>
concept presence_checked = requires { requires Vis::presence_checked; };

}  // namespace kotatsu::codec
