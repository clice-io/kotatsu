#pragma once

#include <string>
#include <string_view>
#include <type_traits>

namespace kota::codec {

/// How enums map to serialized form.
enum class enum_repr {
    /// The underlying integer value, unchecked against declared enumerators.
    Integer,
    /// The declared enumerator name (through Config::enum_rename when
    /// present); encoding a value with no declared name fails.
    String,
};

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
#define KOTA_CFG_FIELD_(name, default_val)                                                         \
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
/// type aliases (field_rename, enum_rename, etc.) are forwarded transparently.
template <typename UserConfig = void>
struct default_config :
    std::conditional_t<std::is_void_v<UserConfig>, detail::empty_config_base, UserConfig> {
    /// Enum serialized representation.
    KOTA_CFG_FIELD_(enum_repr, kota::codec::enum_repr::Integer);

    /// NaN/Infinity handling.
    KOTA_CFG_FIELD_(nan_repr, kota::codec::nan_repr::Passthrough);

    /// Handling of a string that is not UTF-8, in a UTF-8 text document.
    KOTA_CFG_FIELD_(invalid_utf8, kota::codec::invalid_utf8::Error);

    /// Encode: leave out the fields annotated `schema_default = false`, as
    /// json::schema does for the documents it takes defaults from. A format
    /// that writes every field (bincode) writes them still.
    KOTA_CFG_FIELD_(omit_unstated_defaults, false);

    /// Deserialize: reject unknown fields in data-driven mode.
    KOTA_CFG_FIELD_(deny_unknown_fields, false);

    /// Deserialize: every struct field may be absent, as if each were
    /// `defaulted`.
    KOTA_CFG_FIELD_(defaulted_fields, false);

    /// Generate error path tracking code (prepend_field/prepend_index).
    KOTA_CFG_FIELD_(detailed_error, true);
};

#undef KOTA_CFG_FIELD_

/// True when the visitor computes the output layout statically (flatbuffers).
/// Such backends cannot carry a meta::dynamic repr: there is no layout to
/// compute.
template <typename Vis>
concept layout_computed = requires { requires Vis::layout_computed; };

/// True when the visitor writes a struct's fields back to back with nothing
/// marking which are present (bincode). It cannot leave a field out, so
/// skip_if does not apply: every field is written, and decode reads it back.
template <typename Vis>
concept writes_every_field = requires { requires Vis::writes_every_field; };

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

template <typename Config>
std::string apply_enum_rename(bool is_serialize, std::string_view name) {
    if constexpr(requires { typename Config::enum_rename; }) {
        return typename Config::enum_rename{}(is_serialize, name);
    } else {
        return std::string(name);
    }
}

}  // namespace kota::codec
