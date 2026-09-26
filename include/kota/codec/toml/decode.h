#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <type_traits>

#include "kota/support/numeric.h"
#include "kota/meta/type_info.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/toml/type.h"
#include "kota/codec/visit/common.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/context.h"
#include "kota/codec/visit/decode.h"
#include "kota/codec/visit/map_key.h"

namespace kota::codec::toml {

namespace detail {

inline std::string_view node_type_name(const Node* node) {
    if(!node)
        return "null";
    if(node->is_boolean())
        return "boolean";
    if(node->is_integer())
        return "integer";
    if(node->is_floating_point())
        return "float";
    if(node->is_string())
        return "string";
    if(node->is_array())
        return "array";
    if(node->is_table())
        return "table";
    return "unknown";
}

template <typename T>
constexpr bool root_table_v = [] {
    // Judge the shape of the representation the codec dispatch resolves
    // (annotations and toml-scoped meta::repr included) with the same kind
    // test to_toml's root routing applies — in kind_of, str-like or
    // tuple-like wins over reflection, so a bare reflectable check would
    // claim the root table for values the encoder boxes.
    using R = meta::resolved_repr_t<T, format>;
    constexpr auto kind = meta::kind_of<R>();
    return kind == meta::type_kind::structure || kind == meta::type_kind::map ||
           std::same_as<R, Table>;
}();

template <typename T>
auto select_root_node(const Table& tbl) -> const Node* {
    using U = std::remove_cvref_t<T>;
    constexpr auto kind = meta::kind_of<U>();

    // Nullable roots mirror to_toml's unwrapping: an absent value is the
    // empty document, a present one routes by the shape of what it wraps.
    // Empty always means null here — to_toml rejects an engaged value whose
    // serialization would be the empty document.
    if constexpr((kind == meta::type_kind::optional || kind == meta::type_kind::pointer) &&
                 std::is_same_v<meta::resolved_repr_t<U, format>, U>) {
        if(tbl.empty()) {
            return nullptr;
        }
        using value_t = std::remove_cvref_t<decltype(*std::declval<U&>())>;
        if constexpr(root_table_v<value_t>) {
            return std::addressof(static_cast<const Node&>(tbl));
        } else {
            return tbl.get(boxed_root_key);
        }
    } else if constexpr(root_table_v<U>) {
        return std::addressof(static_cast<const Node&>(tbl));
    } else {
        return tbl.get(boxed_root_key);
    }
}

}  // namespace detail

struct ValueReader {
    const Node* node;
    constexpr static bool data_driven = true;
    constexpr static bool human_readable = true;
    using error_type = rich_error;
    using format = toml::format;

    template <typename F>
    bool try_read(F&& fn) {
        error_type discard_err;
        scoped_context<error_type> guard(discard_err);
        ValueReader fork{node};
        return fn(fork);
    }

    bool visit_bool(bool& out) {
        if(!node || !node->is_boolean()) {
            return fail_type("boolean");
        }
        out = *node->value<bool>();
        return true;
    }

    template <typename T>
    bool visit_int(T& out) {
        if(!node || !node->is_integer()) {
            return fail_type("integer");
        }
        auto val = *node->value<std::int64_t>();
        if(!kota::narrow_int(val, out)) {
            return fail_with_location("integer value out of range");
        }
        return true;
    }

    template <typename T>
    bool visit_uint(T& out) {
        if(!node || !node->is_integer()) {
            return fail_type("integer");
        }
        auto val = *node->value<std::int64_t>();
        if(!kota::narrow_int(val, out)) {
            return fail_with_location("integer value out of range");
        }
        return true;
    }

    template <typename T>
    bool visit_float(T& out) {
        if(!node) {
            return fail_type("float");
        }
        if(node->is_floating_point()) {
            out = static_cast<T>(*node->value<double>());
            return true;
        }
        if(node->is_integer()) {
            out = static_cast<T>(*node->value<std::int64_t>());
            return true;
        }
        return fail_type("float");
    }

    template <typename T>
    bool visit_str(T& out) {
        if(!node) {
            return fail_type("string");
        }
        auto val = node->value<std::string>();
        if(!val) {
            return fail_type("string");
        }
        out = T(std::move(*val));
        return true;
    }

    template <typename T>
    bool visit_char(T& out) {
        if(!node) {
            return fail_type("string");
        }
        auto val = node->value<std::string_view>();
        if(!val) {
            return fail_type("string");
        }
        auto c = char_from_utf8(*val);
        if(!c) {
            return fail_with_location(std::string(invalid_char_message));
        }
        out = *c;
        return true;
    }

    template <typename T>
    bool visit_bytes(T& out) {
        if(!node) {
            return fail_type("array");
        }
        const auto* arr = node->as_array();
        if(!arr) {
            return fail_type("array");
        }
        auto size = arr->size();
        out.clear();
        out.reserve(size);
        for(std::size_t i = 0; i < size; ++i) {
            const auto& elem = (*arr)[i];
            auto val = elem.value<std::int64_t>();
            if(!val || *val < 0 || *val > 255) {
                return fail_with_location("byte array element out of range [0, 255]");
            }
            out.push_back(static_cast<typename T::value_type>(static_cast<std::uint8_t>(*val)));
        }
        return true;
    }

    bool peek_null() {
        return node == nullptr;
    }

    bool visit_null() {
        if(peek_null()) {
            return true;
        }
        return fail_type("null");
    }

    meta::type_kind peek_kind() {
        if(!node)
            return meta::type_kind::null;
        if(node->is_boolean())
            return meta::type_kind::boolean;
        if(node->is_integer())
            return meta::type_kind::int64;
        if(node->is_floating_point())
            return meta::type_kind::float64;
        if(node->is_string())
            return meta::type_kind::string;
        if(node->is_array())
            return meta::type_kind::array;
        if(node->is_table())
            return meta::type_kind::structure;
        return meta::type_kind::unknown;
    }

    template <typename Callback>
    bool visit_struct(Callback&& cb) {
        if(!node) {
            return fail_type("table");
        }
        const auto* tbl = node->as_table();
        if(!tbl) {
            return fail_type("table");
        }
        for(const auto& [k, v]: *tbl) {
            ValueReader sub{&v};
            KOTA_CODEC_TRY(cb(std::string_view(k), sub));
        }
        return true;
    }

    template <typename Callback>
    bool visit_seq(Callback&& cb) {
        if(!node) {
            return fail_type("array");
        }
        const auto* arr = node->as_array();
        if(!arr) {
            return fail_type("array");
        }
        for(std::size_t i = 0; i < arr->size(); ++i) {
            ValueReader sub{arr->get(i)};
            KOTA_CODEC_TRY(cb(sub));
        }
        return true;
    }

    template <typename Callback>
    bool visit_map(Callback&& cb) {
        if(!node) {
            return fail_type("table");
        }
        const auto* tbl = node->as_table();
        if(!tbl) {
            return fail_type("table");
        }
        for(const auto& [k, v]: *tbl) {
            MapKeyReader<format> kr{std::string_view(k)};
            ValueReader vr{&v};
            KOTA_CODEC_TRY(cb(kr, vr));
        }
        return true;
    }

    template <typename Callback>
    bool visit_tuple(Callback&& cb) {
        if(!node) {
            return fail_type("array");
        }
        const auto* arr = node->as_array();
        if(!arr) {
            return fail_type("array");
        }
        for(std::size_t i = 0; i < arr->size(); ++i) {
            ValueReader sub{arr->get(i)};
            KOTA_CODEC_TRY(cb(sub));
        }
        return true;
    }

    bool visit_skip() {
        return true;  // nothing to consume in a DOM backend
    }

    /// Backend hook used by data-driven struct decoding: fail an unknown field
    /// with the offending value node's source location attached.
    bool fail_unknown_field(std::string_view key) {
        auto err = rich_error::unknown_field(key);
        attach_location(err);
        return scoped_context<rich_error>::fail(std::move(err));
    }

private:
    void attach_location(rich_error& err) {
        if(node) {
            auto src = node->source();
            if(src.begin.line != 0) {
                err.set_location({
                    static_cast<std::size_t>(src.begin.line),
                    static_cast<std::size_t>(src.begin.column),
                    0,
                });
            }
        }
    }

    bool fail_type(std::string_view expected) {
        auto got = detail::node_type_name(node);
        auto err = rich_error::invalid_type(expected, got);
        attach_location(err);
        return scoped_context<rich_error>::fail(std::move(err));
    }

    bool fail_with_location(std::string msg) {
        rich_error err(std::move(msg));
        attach_location(err);
        return scoped_context<rich_error>::fail(std::move(err));
    }
};

/// Parses TOML text into a raw toml::Table DOM without decoding into any
/// type; from_toml consumes the result.
inline auto parse_table(std::string_view text) -> std::expected<Table, rich_error> {
    // toml++ is pinned to TOML_EXCEPTIONS=0 (see toml/type.h), so parsing
    // always reports failures through toml::parse_result — a single code path
    // with no exception handling involved.
    auto parsed = ::toml::parse(text);
    if(!parsed) {
        const auto& e = parsed.error();
        rich_error err(std::string("TOML parse error: ") + std::string(e.description()));
        auto src = e.source();
        if(src.begin.line != 0) {
            err.set_location({
                static_cast<std::size_t>(src.begin.line),
                static_cast<std::size_t>(src.begin.column),
                0,
            });
        }
        return std::unexpected(std::move(err));
    }
    return std::move(parsed).table();
}

/// Decodes a toml::Table DOM into `out`, routing the root the same way
/// to_toml produced it (root table vs boxed `__value` key).
template <typename Config = void, typename T>
auto from_toml(const Table& tbl, T& out) -> std::expected<void, rich_error> {
    using V = std::remove_const_t<T>;
    const auto* root = detail::select_root_node<V>(tbl);

    using Cfg = default_config<Config>;
    rich_error err;
    scoped_context<rich_error> guard(err);

    ValueReader reader{root};
    if(!decode_value<Cfg>(reader, out)) {
        return std::unexpected(std::move(err));
    }
    return {};
}

/// Decodes TOML text into `out` (or, in the value-returning overload, into a
/// default-constructed T): parse_table followed by from_toml.
template <typename Config = void, typename T>
auto from_string(std::string_view text, T& out) -> std::expected<void, rich_error> {
    auto table = parse_table(text);
    if(!table) {
        return std::unexpected(std::move(table).error());
    }
    return from_toml<Config>(*table, out);
}

template <typename T, typename Config = void>
    requires std::default_initializable<T>
auto from_string(std::string_view text) -> std::expected<T, rich_error> {
    T value{};
    auto result = from_string<Config>(text, value);
    if(!result) {
        return std::unexpected(std::move(result).error());
    }
    return value;
}

}  // namespace kota::codec::toml
