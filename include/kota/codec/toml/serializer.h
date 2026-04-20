#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/codec/backend.h"
#include "kota/codec/codec.h"
#include "kota/codec/config.h"
#include "kota/codec/toml/error.h"

#if __has_include(<toml++/toml.hpp>)
#include "toml++/toml.hpp"
#else
#error "toml++/toml.hpp not found. Enable KOTA_CODEC_ENABLE_TOML or add tomlplusplus include paths."
#endif

namespace kota::codec::toml {

namespace detail {

constexpr inline std::string_view boxed_root_key = "__value";

struct none_t {};

struct Value {
    struct array_t {
        std::vector<Value> values;
    };

    struct table_t {
        std::vector<std::pair<std::string, Value>> entries;
    };

    using storage_t =
        std::variant<none_t, bool, std::int64_t, double, std::string, array_t, table_t>;

    storage_t storage = none_t{};

    Value() = default;

    template <typename T>
    Value(T&& value) : storage(std::forward<T>(value)) {}
};

template <typename T>
using result_t = std::expected<T, error_kind>;

using status_t = result_t<void>;

inline auto append_to_array(::toml::array& array, const Value& value) -> status_t;

inline auto assign_to_table(::toml::table& table, std::string_view key, const Value& value)
    -> status_t;

inline auto append_to_array(::toml::array& array, const Value& value) -> status_t {
    return std::visit(
        [&array](const auto& node) -> status_t {
            using node_t = std::remove_cvref_t<decltype(node)>;

            if constexpr(std::same_as<node_t, none_t>) {
                return std::unexpected(error_kind::unsupported_type);
            } else if constexpr(std::same_as<node_t, bool> || std::same_as<node_t, std::int64_t> ||
                                std::same_as<node_t, double>) {
                array.push_back(node);
                return {};
            } else if constexpr(std::same_as<node_t, std::string>) {
                array.push_back(std::string_view(node));
                return {};
            } else if constexpr(std::same_as<node_t, Value::array_t>) {
                ::toml::array nested;
                for(const auto& element: node.values) {
                    KOTA_EXPECTED_TRY(append_to_array(nested, element));
                }
                array.push_back(std::move(nested));
                return {};
            } else if constexpr(std::same_as<node_t, Value::table_t>) {
                ::toml::table nested;
                for(const auto& [nested_key, nested_value]: node.entries) {
                    KOTA_EXPECTED_TRY(assign_to_table(nested, nested_key, nested_value));
                }
                array.push_back(std::move(nested));
                return {};
            } else {
                return std::unexpected(error_kind::unsupported_type);
            }
        },
        value.storage);
}

inline auto assign_to_table(::toml::table& table, std::string_view key, const Value& value)
    -> status_t {
    return std::visit(
        [&table, key](const auto& node) -> status_t {
            using node_t = std::remove_cvref_t<decltype(node)>;

            if constexpr(std::same_as<node_t, none_t>) {
                // TOML has no null, so we encode none as a missing key.
                return {};
            } else if constexpr(std::same_as<node_t, bool> || std::same_as<node_t, std::int64_t> ||
                                std::same_as<node_t, double>) {
                table.insert_or_assign(key, node);
                return {};
            } else if constexpr(std::same_as<node_t, std::string>) {
                table.insert_or_assign(key, std::string_view(node));
                return {};
            } else if constexpr(std::same_as<node_t, Value::array_t>) {
                ::toml::array nested;
                for(const auto& element: node.values) {
                    KOTA_EXPECTED_TRY(append_to_array(nested, element));
                }
                table.insert_or_assign(key, std::move(nested));
                return {};
            } else if constexpr(std::same_as<node_t, Value::table_t>) {
                ::toml::table nested;
                for(const auto& [nested_key, nested_value]: node.entries) {
                    KOTA_EXPECTED_TRY(assign_to_table(nested, nested_key, nested_value));
                }
                table.insert_or_assign(key, std::move(nested));
                return {};
            } else {
                return std::unexpected(error_kind::unsupported_type);
            }
        },
        value.storage);
}

inline auto value_to_table(const Value& value) -> result_t<::toml::table> {
    if(std::holds_alternative<Value::table_t>(value.storage)) {
        ::toml::table table;
        for(const auto& [key, field]: std::get<Value::table_t>(value.storage).entries) {
            KOTA_EXPECTED_TRY(assign_to_table(table, key, field));
        }
        return table;
    }

    if(std::holds_alternative<none_t>(value.storage)) {
        return ::toml::table{};
    }

    ::toml::table wrapped;
    KOTA_EXPECTED_TRY(assign_to_table(wrapped, boxed_root_key, value));
    return wrapped;
}

inline auto node_to_value(const ::toml::node& node) -> result_t<Value> {
    if(node.is_boolean()) {
        auto parsed = node.value<bool>();
        if(!parsed.has_value()) {
            return std::unexpected(error_kind::type_mismatch);
        }
        return Value(*parsed);
    }

    if(node.is_integer()) {
        auto parsed = node.value<std::int64_t>();
        if(!parsed.has_value()) {
            return std::unexpected(error_kind::type_mismatch);
        }
        return Value(*parsed);
    }

    if(node.is_floating_point()) {
        auto parsed = node.value<double>();
        if(!parsed.has_value()) {
            return std::unexpected(error_kind::type_mismatch);
        }
        return Value(*parsed);
    }

    if(node.is_string()) {
        auto parsed = node.value<std::string>();
        if(!parsed.has_value()) {
            return std::unexpected(error_kind::type_mismatch);
        }
        return Value(std::move(*parsed));
    }

    if(node.is_array()) {
        const auto* array = node.as_array();
        if(array == nullptr) {
            return std::unexpected(error_kind::invalid_state);
        }

        Value::array_t values;
        values.values.reserve(array->size());
        for(const auto& element: *array) {
            KOTA_EXPECTED_TRY_V(auto converted, node_to_value(element));
            values.values.push_back(std::move(converted));
        }
        return Value(std::move(values));
    }

    if(node.is_table()) {
        const auto* table = node.as_table();
        if(table == nullptr) {
            return std::unexpected(error_kind::invalid_state);
        }

        Value::table_t values;
        values.entries.reserve(table->size());
        for(const auto& [key, element]: *table) {
            KOTA_EXPECTED_TRY_V(auto converted, node_to_value(element));
            values.entries.emplace_back(std::string(key.str()), std::move(converted));
        }
        return Value(std::move(values));
    }

    return std::unexpected(error_kind::unsupported_type);
}

inline auto table_to_value(const ::toml::table& table) -> result_t<Value> {
    return node_to_value(table);
}

inline auto array_to_value(const ::toml::array& array) -> result_t<Value> {
    return node_to_value(array);
}

}  // namespace detail

template <typename Config = config::default_config>
class Serializer {
public:
    using config_type = Config;
    using value_type = detail::Value;
    using error_type = error_kind;

    constexpr static auto backend_kind_v = backend_kind::streaming;
    constexpr static auto field_mode_v = field_mode::by_name;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    result_t<value_type> serialize_null() {
        return value_type(detail::none_t{});
    }

    template <typename T>
    result_t<value_type> serialize_some(const T& value) {
        return codec::serialize(*this, value);
    }

    result_t<value_type> serialize_bool(bool value) {
        return value_type(value);
    }

    result_t<value_type> serialize_int(std::int64_t value) {
        return value_type(value);
    }

    result_t<value_type> serialize_uint(std::uint64_t value) {
        if(value > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
            return std::unexpected(error_kind::number_out_of_range);
        }
        return value_type(static_cast<std::int64_t>(value));
    }

    result_t<value_type> serialize_float(double value) {
        return value_type(value);
    }

    result_t<value_type> serialize_char(char value) {
        return value_type(std::string(1, value));
    }

    result_t<value_type> serialize_str(std::string_view value) {
        return value_type(std::string(value));
    }

    result_t<value_type> serialize_bytes(std::span<const std::byte> value) {
        value_type::array_t bytes;
        bytes.values.reserve(value.size());
        for(const auto byte: value) {
            bytes.values.emplace_back(
                static_cast<std::int64_t>(std::to_integer<std::uint8_t>(byte)));
        }
        return value_type(std::move(bytes));
    }

    template <typename... Ts>
    result_t<value_type> serialize_variant(const std::variant<Ts...>& value) {
        return std::visit(
            [&](const auto& item) -> result_t<value_type> { return codec::serialize(*this, item); },
            value);
    }

    // --- Streaming interface ---

    status_t begin_object(std::size_t count) {
        ser_stack.push_back({});
        ser_stack.back().table.entries.reserve(count);
        return {};
    }

    status_t field(std::string_view name) {
        ser_stack.back().pending_key = std::string(name);
        return {};
    }

    status_t accept_field_value(value_type v) {
        auto& frame = ser_stack.back();
        frame.table.entries.emplace_back(std::move(frame.pending_key), std::move(v));
        frame.pending_key.clear();
        return {};
    }

    result_t<value_type> end_object() {
        auto frame = std::move(ser_stack.back());
        ser_stack.pop_back();
        return value_type(std::move(frame.table));
    }

    status_t begin_array(std::optional<std::size_t> count) {
        ser_stack.push_back({});
        if(count.has_value()) {
            ser_stack.back().array.values.reserve(*count);
        }
        ser_stack.back().is_array = true;
        return {};
    }

    status_t accept_element_value(value_type v) {
        auto& frame = ser_stack.back();
        frame.array.values.push_back(std::move(v));
        return {};
    }

    result_t<value_type> end_array() {
        auto frame = std::move(ser_stack.back());
        ser_stack.pop_back();
        return value_type(std::move(frame.array));
    }

    // --- DOM helpers ---

    auto serialize_dom(const ::toml::table& value) -> result_t<value_type> {
        KOTA_EXPECTED_TRY_V(auto converted, detail::table_to_value(value));
        return converted;
    }

    auto serialize_dom(const ::toml::array& value) -> result_t<value_type> {
        KOTA_EXPECTED_TRY_V(auto converted, detail::array_to_value(value));
        return converted;
    }

    template <typename T>
    auto dom(const T& value) -> result_t<::toml::table> {
        auto result = codec::serialize(*this, value);
        if(!result.has_value()) {
            return std::unexpected(result.error());
        }
        return detail::value_to_table(*result);
    }

private:
    struct ser_frame {
        value_type::table_t table;
        value_type::array_t array;
        std::string pending_key;
        bool is_array = false;
    };

    std::vector<ser_frame> ser_stack;
};

template <typename Config = config::default_config, typename T>
auto to_toml(const T& value) -> std::expected<::toml::table, error> {
    Serializer<Config> serializer;
    return serializer.dom(value);
}

static_assert(codec::serializer_like<Serializer<>>);

}  // namespace kota::codec::toml
