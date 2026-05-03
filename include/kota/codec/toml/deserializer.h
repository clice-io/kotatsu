#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/deserialize.h"
#include "kota/codec/toml/error.h"
#include "kota/codec/toml/serializer.h"

#if __has_include(<toml++/toml.hpp>)
#include "toml++/toml.hpp"
#else
#error "toml++/toml.hpp not found. Enable KOTA_CODEC_ENABLE_TOML or add tomlplusplus include paths."
#endif

namespace kota::codec::content {

class Value;

}  // namespace kota::codec::content

namespace kota::codec::toml {

namespace detail {

struct error_context {
    std::optional<error> pending;

    void set(error err) {
        pending = std::move(err);
    }

    void clear() {
        pending.reset();
    }

    std::optional<error> take() {
        auto result = std::move(pending);
        pending.reset();
        return result;
    }

    void prepend_field(std::string_view name) {
        ensure_pending();
        pending->prepend_field(name);
    }

    void prepend_index(std::size_t index) {
        ensure_pending();
        pending->prepend_index(index);
    }

private:
    void ensure_pending() {
        if(!pending) {
            pending = error(error_kind::type_mismatch);
        }
    }
};

inline error_context& thread_error_context() {
    thread_local error_context ctx;
    return ctx;
}

/// Try to extract source location from a toml node.
inline std::optional<codec::source_location> source_from_node(const ::toml::node* node) {
    if(!node)
        return std::nullopt;
    auto region = node->source();
    if(!static_cast<bool>(region.begin))
        return std::nullopt;
    return codec::source_location{
        static_cast<std::size_t>(region.begin.line),
        static_cast<std::size_t>(region.begin.column),
        0,
    };
}

}  // namespace detail

struct toml_backend {
    using value_type = const ::toml::node*;
    using error_type = error_kind;
    constexpr static error_type success = error_kind::ok;
    constexpr static error_type type_mismatch = error_kind::type_mismatch;
    constexpr static error_type number_out_of_range = error_kind::number_out_of_range;

    static error_type read_bool(value_type& v, bool& out) {
        if(!v)
            return type_mismatch;
        auto parsed = v->value<bool>();
        if(!parsed.has_value()) {
            set_location_from_node(v);
            return type_mismatch;
        }
        out = *parsed;
        return success;
    }

    static error_type read_int64(value_type& v, std::int64_t& out) {
        if(!v)
            return type_mismatch;
        auto parsed = v->value<std::int64_t>();
        if(!parsed.has_value()) {
            set_location_from_node(v);
            return type_mismatch;
        }
        out = *parsed;
        return success;
    }

    static error_type read_uint64(value_type& v, std::uint64_t& out) {
        if(!v)
            return type_mismatch;
        // toml++ stores integers as int64_t; read as int64 and convert
        auto parsed = v->value<std::int64_t>();
        if(!parsed.has_value()) {
            set_location_from_node(v);
            return type_mismatch;
        }
        if(*parsed < 0) {
            set_location_from_node(v);
            return number_out_of_range;
        }
        out = static_cast<std::uint64_t>(*parsed);
        return success;
    }

    static error_type read_double(value_type& v, double& out) {
        if(!v)
            return type_mismatch;
        auto parsed = v->value<double>();
        if(!parsed.has_value()) {
            set_location_from_node(v);
            return type_mismatch;
        }
        out = *parsed;
        return success;
    }

    static error_type read_string(value_type& v, std::string_view& out) {
        if(!v)
            return type_mismatch;
        auto parsed = v->value<std::string_view>();
        if(!parsed.has_value()) {
            set_location_from_node(v);
            return type_mismatch;
        }
        out = *parsed;
        return success;
    }

private:
    static void set_location_from_node(value_type& v) {
        if(auto loc = detail::source_from_node(v)) {
            auto& ctx = detail::thread_error_context();
            if(!ctx.pending) {
                ctx.pending = error(error_kind::type_mismatch);
            }
            ctx.pending->set_location(*loc);
        }
    }

public:
    static error_type read_is_null(value_type& v, bool& is_null) {
        is_null = (v == nullptr);
        return success;
    }

    template <typename Visitor>
    static error_type visit_object(value_type& src, Visitor&& vis) {
        if(!src)
            return type_mismatch;
        const auto* tbl = src->as_table();
        if(!tbl)
            return type_mismatch;
        for(const auto& [k, node]: *tbl) {
            value_type val = &node;
            auto err = vis.visit_field(std::string_view(k), val);
            if(err != success) [[unlikely]]
                return err;
        }
        return success;
    }

    template <typename Visitor>
    static error_type visit_array(value_type& src, Visitor&& vis) {
        if(!src)
            return type_mismatch;
        const auto* arr = src->as_array();
        if(!arr)
            return type_mismatch;
        for(std::size_t i = 0; i < arr->size(); ++i) {
            value_type elem = arr->get(i);
            auto err = vis.visit_element(elem);
            if(err != success) [[unlikely]]
                return err;
        }
        return success;
    }

    static meta::type_kind kind_of(value_type& src) {
        if(!src)
            return meta::type_kind::null;
        if(src->is_boolean())
            return meta::type_kind::boolean;
        if(src->is_integer())
            return meta::type_kind::int64;
        if(src->is_floating_point())
            return meta::type_kind::float64;
        if(src->is_string())
            return meta::type_kind::string;
        if(src->is_array())
            return meta::type_kind::array;
        if(src->is_table())
            return meta::type_kind::structure;
        return meta::type_kind::any;
    }

    template <typename Visitor>
    static error_type visit_object_keys(value_type& src, Visitor&& vis) {
        if(!src)
            return type_mismatch;
        const auto* tbl = src->as_table();
        if(!tbl)
            return type_mismatch;
        for(const auto& [k, node]: *tbl) {
            value_type val = &node;
            meta::type_kind k_kind = kind_of(val);
            auto err = vis.on_field(std::string_view(k), k_kind, val);
            if(err != success) [[unlikely]]
                return err;
        }
        return success;
    }

    template <typename Visitor>
    static error_type visit_array_keys(value_type& src, Visitor&& vis) {
        if(!src)
            return type_mismatch;
        const auto* arr = src->as_array();
        if(!arr)
            return type_mismatch;
        std::size_t total = arr->size();
        for(std::size_t i = 0; i < total; ++i) {
            value_type elem = arr->get(i);
            meta::type_kind elem_kind = kind_of(elem);
            auto err = vis.on_element(i, total, elem_kind, elem);
            if(err != success) [[unlikely]]
                return err;
        }
        return success;
    }

    /// Error context helpers: store rich error info in the thread-local context.
    /// The from_toml entry point checks this after a failed deserialization.

    static void report_missing_field(std::string_view field_name) {
        detail::thread_error_context().set(error::missing_field(field_name));
    }

    static void report_unknown_field(std::string_view field_name) {
        detail::thread_error_context().set(error::unknown_field(field_name));
    }

    static void report_unknown_enum(std::string_view value) {
        detail::thread_error_context().set(
            error(error_kind::type_mismatch, std::format("unknown enum string value '{}'", value)));
    }

    static void report_prepend_field(std::string_view name) {
        detail::thread_error_context().prepend_field(name);
    }

    static void report_prepend_index(std::size_t index) {
        detail::thread_error_context().prepend_index(index);
    }
};

template <typename Config>
struct toml_backend_with_config : toml_backend {
    using config_type = Config;
    using base_backend_type = toml_backend;
};

namespace detail {

using codec::detail::clean_t;
using codec::detail::remove_annotation_t;
using codec::detail::remove_optional_t;

template <typename T>
consteval bool is_map_like() {
    if constexpr(std::ranges::input_range<T>) {
        return format_kind<T> == range_format::map;
    } else {
        return false;
    }
}

template <typename T>
constexpr bool is_map_like_v = is_map_like<T>();

template <typename T>
constexpr bool root_table_v =
    (meta::reflectable_class<T> && !is_pair_v<T> && !is_tuple_v<T> &&
     !std::ranges::input_range<T>) ||
    is_map_like_v<T> || std::same_as<T, ::toml::table> || std::same_as<T, content::Value>;

template <typename T>
auto select_root_node(const ::toml::table& table) -> const ::toml::node* {
    using U = std::remove_cvref_t<T>;

    if constexpr(is_specialization_of<std::optional, U>) {
        if(table.empty()) {
            return nullptr;
        }
        using value_t = clean_t<U>;
        if constexpr(root_table_v<value_t>) {
            return std::addressof(static_cast<const ::toml::node&>(table));
        } else {
            return table.get(boxed_root_key);
        }
    } else if constexpr(root_table_v<clean_t<U>>) {
        return std::addressof(static_cast<const ::toml::node&>(table));
    } else {
        return table.get(boxed_root_key);
    }
}

}  // namespace detail

template <typename Config = config::default_config, typename T>
auto from_toml(const ::toml::table& table, T& value) -> std::expected<void, error> {
    using Backend = toml_backend_with_config<Config>;
    detail::thread_error_context().clear();

    typename Backend::value_type root = detail::select_root_node<T>(table);
    auto err = codec::deserialize<Backend>(root, value);
    if(err != error_kind::ok) {
        auto& ctx = detail::thread_error_context();
        auto pending = ctx.take();
        if(pending) {
            return std::unexpected(std::move(*pending));
        }
        return std::unexpected(error(err));
    }
    return {};
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_toml(const ::toml::table& table) -> std::expected<T, error> {
    T value{};
    KOTA_EXPECTED_TRY(from_toml<Config>(table, value));
    return value;
}

}  // namespace kota::codec::toml
