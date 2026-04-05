#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "eventide/common/expected_try.h"
#include "eventide/serde/json/error.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/serde.h"
#include "eventide/serde/serde/utils/backend_helpers.h"

namespace eventide::serde::json {

template <typename Config = config::default_config>
class Serializer {
public:
    using config_type = Config;
    using value_type = void;
    using error_type = json::error_kind;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    using SerializeArray = serde::detail::SerializeArray<Serializer<Config>>;
    using SerializeObject = serde::detail::SerializeObject<Serializer<Config>>;

    using SerializeSeq = SerializeArray;
    using SerializeTuple = SerializeArray;
    using SerializeMap = SerializeObject;
    using SerializeStruct = SerializeObject;

    Serializer() = default;

    explicit Serializer(std::size_t initial_capacity) : builder(initial_capacity) {}

    result_t<std::string_view> view() const {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        if(!stack.empty() || !root_written) {
            return std::unexpected(error_kind::invalid_state);
        }

        std::string_view out{};
        auto err = builder.view().get(out);
        if(err != simdjson::SUCCESS) {
            return std::unexpected(json::make_error(err));
        }
        return out;
    }

    result_t<std::string> str() const {
        ETD_EXPECTED_TRY_V(auto out, view());
        return std::string(out);
    }

    void clear() {
        builder.clear();
        stack.clear();
        root_written = false;
        is_valid = true;
        last_error = error_kind::ok;
    }

    bool valid() const {
        return is_valid;
    }

    error_type error() const {
        return last_error;
    }

    result_t<value_type> serialize_null() {
        return null();
    }

    template <typename T>
    result_t<value_type> serialize_some(const T& value) {
        return serde::serialize(*this, value);
    }

    template <typename... Ts>
    result_t<value_type> serialize_variant(const std::variant<Ts...>& value) {
        return std::visit(
            [&](const auto& item) -> result_t<value_type> { return serde::serialize(*this, item); },
            value);
    }

    result_t<value_type> serialize_bool(bool value) {
        if(!before_value()) {
            return status();
        }

        builder.append(value);
        return status();
    }

    result_t<value_type> serialize_int(std::int64_t value) {
        if(!before_value()) {
            return status();
        }

        builder.append(value);
        return status();
    }

    result_t<value_type> serialize_uint(std::uint64_t value) {
        if(!before_value()) {
            return status();
        }

        builder.append(value);
        return status();
    }

    result_t<value_type> serialize_float(double value) {
        if(!before_value()) {
            return status();
        }

        if(std::isfinite(value)) {
            builder.append(value);
        } else {
            builder.append_null();
        }
        return status();
    }

    result_t<value_type> serialize_char(char value) {
        if(!before_value()) {
            return status();
        }

        const char text[1] = {value};
        builder.escape_and_append_with_quotes(std::string_view(text, 1));
        return status();
    }

    result_t<value_type> serialize_str(std::string_view value) {
        if(!before_value()) {
            return status();
        }

        builder.escape_and_append_with_quotes(value);
        return status();
    }

    result_t<value_type> serialize_raw_json(std::string_view raw_json) {
        if(!before_value()) {
            return status();
        }

        builder.append_raw(raw_json);
        return status();
    }

    result_t<value_type> serialize_bytes(std::string_view value) {
        ETD_EXPECTED_TRY_V(auto seq, serialize_seq(value.size()));

        for(unsigned char byte: value) {
            ETD_EXPECTED_TRY(seq.serialize_element(static_cast<std::uint64_t>(byte)));
        }

        return seq.end();
    }

    result_t<value_type> serialize_bytes(std::span<const std::byte> value) {
        ETD_EXPECTED_TRY_V(auto seq, serialize_seq(value.size()));

        for(std::byte byte: value) {
            ETD_EXPECTED_TRY(seq.serialize_element(
                static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(byte))));
        }

        return seq.end();
    }

    result_t<SerializeSeq> serialize_seq(std::optional<std::size_t> /*len*/) {
        ETD_EXPECTED_TRY(begin_array());
        return SerializeSeq(*this);
    }

    result_t<SerializeTuple> serialize_tuple(std::size_t /*len*/) {
        ETD_EXPECTED_TRY(begin_array());
        return SerializeTuple(*this);
    }

    result_t<SerializeMap> serialize_map(std::optional<std::size_t> /*len*/) {
        ETD_EXPECTED_TRY(begin_object());
        return SerializeMap(*this);
    }

    result_t<SerializeStruct> serialize_struct(std::string_view /*name*/, std::size_t /*len*/) {
        ETD_EXPECTED_TRY(begin_object());
        return SerializeStruct(*this);
    }

private:
    friend class serde::detail::SerializeArray<Serializer<Config>>;
    friend class serde::detail::SerializeObject<Serializer<Config>>;

    enum class container_kind : std::uint8_t { array, object };

    struct container_frame {
        container_kind kind;
        bool first = true;
        bool expect_key = true;
    };

    status_t begin_object() {
        if(!before_value()) {
            return status();
        }

        builder.start_object();
        stack.push_back(container_frame{container_kind::object, true, true});
        return {};
    }

    result_t<value_type> end_object() {
        if(!is_valid || stack.empty()) {
            mark_invalid();
            return std::unexpected(last_error);
        }

        const auto frame = stack.back();
        if(frame.kind != container_kind::object || !frame.expect_key) {
            mark_invalid();
            return std::unexpected(last_error);
        }

        builder.end_object();
        stack.pop_back();
        return status();
    }

    status_t begin_array() {
        if(!before_value()) {
            return status();
        }

        builder.start_array();
        stack.push_back(container_frame{container_kind::array, true, false});
        return {};
    }

    result_t<value_type> end_array() {
        if(!is_valid || stack.empty()) {
            mark_invalid();
            return std::unexpected(last_error);
        }

        if(stack.back().kind != container_kind::array) {
            mark_invalid();
            return std::unexpected(last_error);
        }

        builder.end_array();
        stack.pop_back();
        return status();
    }

    status_t key(std::string_view key_name) {
        if(!is_valid || stack.empty()) {
            mark_invalid();
            return std::unexpected(last_error);
        }

        auto& frame = stack.back();
        if(frame.kind != container_kind::object || !frame.expect_key) {
            mark_invalid();
            return std::unexpected(last_error);
        }

        if(!frame.first) {
            builder.append_comma();
        }
        frame.first = false;

        builder.escape_and_append_with_quotes(key_name);
        builder.append_colon();
        frame.expect_key = false;
        return status();
    }

    status_t null() {
        if(!before_value()) {
            return status();
        }

        builder.append_null();
        return status();
    }

    bool before_value() {
        if(!is_valid) {
            return false;
        }

        if(stack.empty()) {
            if(root_written) {
                mark_invalid();
                return false;
            }

            root_written = true;
            return true;
        }

        auto& frame = stack.back();
        if(frame.kind == container_kind::array) {
            if(!frame.first) {
                builder.append_comma();
            }
            frame.first = false;
            return true;
        }

        if(frame.expect_key) {
            mark_invalid();
            return false;
        }

        frame.expect_key = true;
        return true;
    }

    void mark_invalid(error_kind error = error_kind::invalid_state) {
        is_valid = false;
        if(last_error == error_kind::ok) {
            last_error = error;
        }
    }

    status_t status() const {
        if(is_valid) {
            return {};
        }
        return std::unexpected(last_error);
    }

private:
    bool is_valid = true;
    bool root_written = false;
    error_type last_error = error_kind::ok;
    std::vector<container_frame> stack;
    simdjson::builder::string_builder builder;
};

template <typename Config = config::default_config, typename T>
auto to_json(const T& value, std::optional<std::size_t> initial_capacity = std::nullopt)
    -> std::expected<std::string, error> {
    Serializer<Config> serializer =
        initial_capacity.has_value() ? Serializer<Config>(*initial_capacity) : Serializer<Config>();
    ETD_EXPECTED_TRY(serde::serialize(serializer, value));
    return serializer.str();
}

static_assert(serde::serializer_like<Serializer<>>);

}  // namespace eventide::serde::json
