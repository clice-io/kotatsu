#pragma once

#include <optional>
#include <type_traits>

#include "kotatsu/meta/spec.h"

namespace kotatsu::codec {

/// How non-finite floats (NaN, ±Infinity) are serialized. The check runs
/// after narrowing to double, upstream of the backend visitor.
enum class nan_repr {
    /// Hand the raw value to the backend. Binary backends store the bit
    /// pattern and TOML has non-finite literals, but JSON does not — its
    /// writer emits `null` for non-finite values even in this mode.
    Passthrough,
    /// Encode as null.
    Null,
    /// Encode as the strings "NaN" / "Infinity" / "-Infinity". Encode-side
    /// only: decoding those strings back into a float is not implemented.
    String,
    /// Fail encoding with an error.
    Error,
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

namespace detail {

struct empty_config_base {};

}  // namespace detail

// clang-format off
#define KOTATSU_CFG_FIELD_(name, default_val)                                                      \
    constexpr static auto name = [] {                                                              \
        if constexpr(std::is_void_v<UserConfig>)                                                   \
            return (default_val);                                                                   \
        else if constexpr(requires { UserConfig::name; })                                          \
            return UserConfig::name;                                                               \
        else                                                                                       \
            return (default_val);                                                                   \
    }()
// clang-format on

/// Config template that resolves missing fields from defaults.
/// `default_config<>` gives all defaults. `default_config<MyConfig>` fills in
/// any fields that MyConfig does not provide.  Inherits from UserConfig so that
/// type aliases (field_rename) are forwarded transparently.
template <typename UserConfig = void>
struct default_config :
    std::conditional_t<std::is_void_v<UserConfig>, detail::empty_config_base, UserConfig> {
    /// NaN/Infinity handling.
    KOTATSU_CFG_FIELD_(nan_repr, kotatsu::codec::nan_repr::Passthrough);

    /// Handling of a string that is not UTF-8, in a UTF-8 text document.
    KOTATSU_CFG_FIELD_(invalid_utf8, kotatsu::codec::invalid_utf8::Error);

    /// Encode: leave out the fields annotated `schema_default = false`, as
    /// json::schema does for the documents it takes defaults from. A format
    /// that writes every field (bincode) writes them still.
    KOTATSU_CFG_FIELD_(omit_unstated_defaults, false);

    /// Deserialize: reject unknown fields in data-driven mode.
    KOTATSU_CFG_FIELD_(deny_unknown_fields, false);

    /// The presence of every struct field its annotation leaves unset;
    /// unset, a field's kind decides.
    KOTATSU_CFG_FIELD_(presence_all, std::optional<meta::presence_kind>{});

    /// Generate error path tracking code (prepend_field/prepend_index).
    KOTATSU_CFG_FIELD_(detailed_error, true);
};

#undef KOTATSU_CFG_FIELD_

/// True when the visitor computes the output layout statically (flatbuffers).
/// Such backends cannot carry a meta::dynamic repr: there is no layout to
/// compute.
template <typename Vis>
concept layout_computed = requires { requires Vis::layout_computed; };

/// True when the visitor writes every field of a struct, whatever its
/// presence: bincode writes the fields back to back with nothing marking
/// which are present, and fbs reads an absent slot as null, empty or zero.
template <typename Vis>
concept writes_every_field = requires { requires Vis::writes_every_field; };

/// True when whoever drives the visitor has checked which fields the document
/// carries (cli, which reports a missing argument itself, and whose help
/// option ends a parse before anything is checked): decode leaves required
/// fields unchecked.
template <typename Vis>
concept presence_checked = requires { requires Vis::presence_checked; };

/// Config > Vis > true. Determines text vs binary serialization strategy for user-defined types.
template <typename Config, typename Vis>
constexpr bool is_human_readable() {
    if constexpr(requires { Config::human_readable; }) {
        return Config::human_readable;
    } else if constexpr(requires { Vis::human_readable; }) {
        return Vis::human_readable;
    } else {
        return true;
    }
}

namespace detail {

/// Whether Config's human_readable fits the visitor: a config may turn a
/// human-readable backend's name tags off, never a binary backend's on, since
/// a binary visitor writes no field names and a tagged variant would encode
/// as a struct its decoder does not read.
template <typename Config, typename Vis>
concept human_readable_allowed =
    !requires { Vis::human_readable; } || Vis::human_readable || !is_human_readable<Config, Vis>();

template <typename Config, typename Vis>
consteval void assert_human_readable_allowed() {
    static_assert(human_readable_allowed<Config, Vis>,
                  "Config::human_readable = true on a binary backend: only a human-readable "
                  "backend's tagging can be configured, and only off");
}

}  // namespace detail

}  // namespace kotatsu::codec
