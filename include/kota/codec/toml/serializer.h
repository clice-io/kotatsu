#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

}  // namespace detail

template <typename Config = config::default_config>
class Serializer {
public:
    using config_type = Config;
    using value_type = void;
    using error_type = error_kind;

    constexpr static auto backend_kind_v = backend_kind::streaming;
    constexpr static auto field_mode_v = field_mode::by_name;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    status_t serialize_null() {
        // TOML has no null. In array context this is an error.
        // In table context, skip (missing key = null).
        if(!ser_stack.empty() && ser_stack.back().is_array) {
            return std::unexpected(error_kind::unsupported_type);
        }
        if(!ser_stack.empty()) {
            ser_stack.back().pending_key.clear();
        }
        return {};
    }

    template <typename T>
    status_t serialize_some(const T& value) {
        return codec::serialize(*this, value);
    }

    status_t serialize_bool(bool value) {
        return insert_value(value);
    }

    status_t serialize_int(std::int64_t value) {
        return insert_value(value);
    }

    status_t serialize_uint(std::uint64_t value) {
        if(value > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
            return std::unexpected(error_kind::number_out_of_range);
        }
        return insert_value(static_cast<std::int64_t>(value));
    }

    status_t serialize_float(double value) {
        return insert_value(value);
    }

    status_t serialize_char(char value) {
        return insert_value(std::string(1, value));
    }

    status_t serialize_str(std::string_view value) {
        return insert_value(std::string(value));
    }

    status_t serialize_bytes(std::span<const std::byte> value) {
        KOTA_EXPECTED_TRY(begin_array(value.size()));
        for(const auto byte: value) {
            KOTA_EXPECTED_TRY(
                insert_value(static_cast<std::int64_t>(std::to_integer<std::uint8_t>(byte))));
        }
        return end_array();
    }

    template <typename... Ts>
    status_t serialize_variant(const std::variant<Ts...>& value) {
        return std::visit(
            [&](const auto& item) -> status_t { return codec::serialize(*this, item); },
            value);
    }

    status_t begin_object(std::size_t /*count*/) {
        ser_stack.push_back({});
        return {};
    }

    status_t field(std::string_view name) {
        ser_stack.back().pending_key = std::string(name);
        return {};
    }

    status_t end_object() {
        auto frame = std::move(ser_stack.back());
        ser_stack.pop_back();

        if(ser_stack.empty()) {
            root_table_ = std::move(frame.table);
            root_is_direct_table_ = true;
            return {};
        }

        auto& parent = ser_stack.back();
        if(parent.is_array) {
            parent.array.push_back(std::move(frame.table));
        } else {
            parent.table.insert_or_assign(parent.pending_key, std::move(frame.table));
            parent.pending_key.clear();
        }
        return {};
    }

    status_t begin_array(std::optional<std::size_t> /*count*/) {
        ser_frame frame;
        frame.is_array = true;
        ser_stack.push_back(std::move(frame));
        return {};
    }

    status_t end_array() {
        auto frame = std::move(ser_stack.back());
        ser_stack.pop_back();

        if(ser_stack.empty()) {
            root_table_.insert_or_assign(detail::boxed_root_key, std::move(frame.array));
            return {};
        }

        auto& parent = ser_stack.back();
        if(parent.is_array) {
            parent.array.push_back(std::move(frame.array));
        } else {
            parent.table.insert_or_assign(parent.pending_key, std::move(frame.array));
            parent.pending_key.clear();
        }
        return {};
    }

    auto serialize_dom(const ::toml::table& value) -> status_t {
        if(ser_stack.empty()) {
            root_table_ = value;
            root_is_direct_table_ = true;
            return {};
        }
        auto& parent = ser_stack.back();
        if(parent.is_array) {
            parent.array.push_back(value);
        } else {
            parent.table.insert_or_assign(parent.pending_key, value);
            parent.pending_key.clear();
        }
        return {};
    }

    auto serialize_dom(const ::toml::array& value) -> status_t {
        if(ser_stack.empty()) {
            root_table_.insert_or_assign(detail::boxed_root_key, value);
            return {};
        }
        auto& parent = ser_stack.back();
        if(parent.is_array) {
            parent.array.push_back(value);
        } else {
            parent.table.insert_or_assign(parent.pending_key, value);
            parent.pending_key.clear();
        }
        return {};
    }

    template <typename T>
    auto dom(const T& value) -> result_t<::toml::table> {
        KOTA_EXPECTED_TRY(codec::serialize(*this, value));
        return std::move(root_table_);
    }

private:
    template <typename V>
    status_t insert_value(V&& v) {
        if(ser_stack.empty()) {
            root_table_.insert_or_assign(detail::boxed_root_key, std::forward<V>(v));
            return {};
        }
        auto& frame = ser_stack.back();
        if(frame.is_array) {
            frame.array.push_back(std::forward<V>(v));
        } else {
            frame.table.insert_or_assign(frame.pending_key, std::forward<V>(v));
            frame.pending_key.clear();
        }
        return {};
    }

    struct ser_frame {
        ::toml::table table;
        ::toml::array array;
        std::string pending_key;
        bool is_array = false;
    };

    ::toml::table root_table_;
    bool root_is_direct_table_ = false;
    std::vector<ser_frame> ser_stack;
};

template <typename Config = config::default_config, typename T>
auto to_toml(const T& value) -> std::expected<::toml::table, error> {
    Serializer<Config> serializer;
    return serializer.dom(value);
}

static_assert(codec::serializer_like<Serializer<>>);

}  // namespace kota::codec::toml
