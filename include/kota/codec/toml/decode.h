#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "kota/support/expected_try.h"
#include "kota/support/numeric.h"
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

/// The node from_toml reads a T from, routed as to_toml wrote it. Empty
/// always means null for a nullable root: to_toml rejects an engaged value
/// whose serialization would be the empty document.
template <typename T>
auto select_root_node(const Table& tbl) -> const Node* {
    if constexpr(nullable_root_v<T>) {
        if(tbl.empty()) {
            return nullptr;
        }
        if constexpr(root_table_v<std::remove_cvref_t<decltype(*std::declval<T&>())>>) {
            return std::addressof(static_cast<const Node&>(tbl));
        } else {
            return tbl.get(boxed_root_key);
        }
    } else if constexpr(root_table_v<T>) {
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
    using format = toml::format;

    template <typename F>
    bool try_read(F&& fn) {
        rich_error discard_err;
        scoped_context<rich_error> guard(discard_err);
        ValueReader fork{node};
        return fn(fork);
    }

    bool visit_bool(bool& out) {
        const auto* flag = as<bool>();
        if(!flag) {
            return fail_type("boolean");
        }
        out = flag->get();
        return true;
    }

    template <typename T>
    bool visit_int(T& out) {
        const auto* number = as<std::int64_t>();
        if(!number) {
            return fail_type("integer");
        }
        if(!kota::narrow_int(number->get(), out)) {
            return fail_with_location("integer value out of range");
        }
        return true;
    }

    template <typename T>
    bool visit_uint(T& out) {
        return visit_int(out);
    }

    template <typename T>
    bool visit_float(T& out) {
        if(const auto* real = as<double>()) {
            out = static_cast<T>(real->get());
            return true;
        }
        if(const auto* number = as<std::int64_t>()) {
            out = static_cast<T>(number->get());
            return true;
        }
        return fail_type("float");
    }

    template <typename T>
    bool visit_str(T& out) {
        const auto* text = as<std::string>();
        if(!text) {
            return fail_type("string");
        }
        out = T(text->get());
        return true;
    }

    template <typename T>
    bool visit_char(T& out) {
        const auto* text = as<std::string>();
        if(!text) {
            return fail_type("string");
        }
        auto c = char_from_utf8(text->get());
        if(!c) {
            return fail_with_location(std::string(invalid_char_message));
        }
        out = *c;
        return true;
    }

    template <typename T>
    bool visit_bytes(T& out) {
        const auto* arr = as<Array>();
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
        const auto* tbl = as<Table>();
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
        const auto* arr = as<Array>();
        if(!arr) {
            return fail_type("array");
        }
        for(std::size_t i = 0; i < arr->size(); ++i) {
            ValueReader sub{arr->get(i)};
            KOTA_CODEC_TRY(cb(sub));
        }
        return true;
    }

    /// A table read with MapKeyReader keys.
    template <typename Callback>
    bool visit_map(Callback&& cb) {
        return visit_struct([&](std::string_view key, ValueReader& value) {
            MapKeyReader<format> kr{key};
            return cb(kr, value);
        });
    }

    /// A tuple is an array, read as a sequence.
    template <typename Callback>
    bool visit_tuple(Callback&& cb) {
        return visit_seq(std::forward<Callback>(cb));
    }

    /// Backend hook used by data-driven struct decoding: fail an unknown field
    /// with the offending value node's source location attached.
    bool fail_unknown_field(std::string_view key) {
        auto err = rich_error::unknown_field(key);
        attach_location(err);
        return scoped_context<rich_error>::fail(std::move(err));
    }

private:
    /// The node as a T (a Table, an Array, or the value node holding a T),
    /// or null when it is absent or holds something else; fail_type reports
    /// either.
    template <typename T>
    auto as() const -> decltype(std::declval<const Node&>().template as<T>()) {
        return node ? node->template as<T>() : nullptr;
    }

    void attach_location(rich_error& err) {
        if(node) {
            err.location = detail::location_of(node->source());
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
        rich_error err(std::format("TOML parse error: {}", e.description()));
        err.location = detail::location_of(e.source());
        return std::unexpected(std::move(err));
    }
    return std::move(parsed).table();
}

/// Decodes a toml::Table DOM into `out`, routing the root the same way
/// to_toml produced it (root table vs boxed `__value` key).
template <typename Config = void, typename T>
auto from_toml(const Table& tbl, T& out) -> std::expected<void, rich_error> {
    ValueReader reader{detail::select_root_node<T>(tbl)};
    return codec::detail::run_decode<Config>(reader, out);
}

/// Decodes TOML text into `out` (or, in the value-returning overload, into a
/// default-constructed T): parse_table followed by from_toml.
template <typename Config = void, typename T>
auto from_string(std::string_view text, T& out) -> std::expected<void, rich_error> {
    KOTA_EXPECTED_TRY_V(auto table, parse_table(text));
    return from_toml<Config>(table, out);
}

template <typename T, typename Config = void>
    requires std::default_initializable<T>
auto from_string(std::string_view text) -> std::expected<T, rich_error> {
    T value{};
    KOTA_EXPECTED_TRY(from_string<Config>(text, value));
    return value;
}

}  // namespace kota::codec::toml
