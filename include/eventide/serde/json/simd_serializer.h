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

#include "eventide/serde/json/error.h"
#include "eventide/serde/serde.h"

namespace eventide::serde::json::simd {

class Serializer {
public:
    using value_type = void;
    using error_type = json::error_kind;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    class SerializeArray {
    public:
        explicit SerializeArray(Serializer& serializer) noexcept : serializer(serializer) {}

        template <typename T>
        status_t serialize_element(const T& value) {
            auto result = serde::serialize(serializer, value);
            if(!result) {
                return std::unexpected(result.error());
            }
            return {};
        }

        result_t<value_type> end() {
            return serializer.end_array();
        }

    private:
        Serializer& serializer;
    };

    class SerializeObject {
    public:
        explicit SerializeObject(Serializer& serializer) noexcept : serializer(serializer) {}

        template <typename K, typename V>
        status_t serialize_entry(const K& key, const V& value) {
            auto key_status = serializer.key(serde::spelling::map_key_to_string(key));
            if(!key_status) {
                return std::unexpected(key_status.error());
            }

            return serde::serialize(serializer, value);
        }

        template <typename T>
        status_t serialize_field(std::string_view key, const T& value) {
            auto key_status = serializer.key(key);
            if(!key_status) {
                return std::unexpected(key_status.error());
            }

            return serde::serialize(serializer, value);
        }

        result_t<value_type> end() {
            return serializer.end_object();
        }

    private:
        Serializer& serializer;
    };

    using SerializeSeq = SerializeArray;
    using SerializeTuple = SerializeArray;
    using SerializeMap = SerializeObject;
    using SerializeStruct = SerializeObject;

    Serializer() = default;

    explicit Serializer(std::size_t initial_capacity) : builder(initial_capacity) {}

    result_t<std::string_view> view() const {
        if(!is_valid || !stack.empty() || !root_written) {
            return std::unexpected(current_error());
        }

        std::string_view out{};
        auto err = builder.view().get(out);
        if(err != simdjson::SUCCESS) {
            return std::unexpected(json::make_error(err));
        }
        return out;
    }

    result_t<std::string> str() const {
        auto out = view();
        if(!out) {
            return std::unexpected(out.error());
        }
        return std::string(*out);
    }

    void clear() {
        builder.clear();
        stack.clear();
        root_written = false;
        is_valid = true;
        last_error = simdjson::SUCCESS;
    }

    bool valid() const {
        return is_valid;
    }

    error_type error() const {
        if(is_valid) {
            return error_kind::ok;
        }
        return current_error();
    }

    result_t<value_type> serialize_none() {
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
        auto seq = serialize_seq(value.size());
        if(!seq) {
            return std::unexpected(seq.error());
        }

        for(unsigned char byte: value) {
            auto element = seq->serialize_element(static_cast<std::uint64_t>(byte));
            if(!element) {
                return std::unexpected(element.error());
            }
        }

        return seq->end();
    }

    result_t<value_type> serialize_bytes(std::span<const std::byte> value) {
        auto seq = serialize_seq(value.size());
        if(!seq) {
            return std::unexpected(seq.error());
        }

        for(std::byte byte: value) {
            auto element = seq->serialize_element(
                static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(byte)));
            if(!element) {
                return std::unexpected(element.error());
            }
        }

        return seq->end();
    }

    result_t<SerializeSeq> serialize_seq(std::optional<std::size_t> /*len*/) {
        auto started = begin_array();
        if(!started) {
            return std::unexpected(started.error());
        }
        return SerializeSeq(*this);
    }

    result_t<SerializeTuple> serialize_tuple(std::size_t /*len*/) {
        auto started = begin_array();
        if(!started) {
            return std::unexpected(started.error());
        }
        return SerializeTuple(*this);
    }

    result_t<SerializeMap> serialize_map(std::optional<std::size_t> /*len*/) {
        auto started = begin_object();
        if(!started) {
            return std::unexpected(started.error());
        }
        return SerializeMap(*this);
    }

    result_t<SerializeStruct> serialize_struct(std::string_view /*name*/, std::size_t /*len*/) {
        auto started = begin_object();
        if(!started) {
            return std::unexpected(started.error());
        }
        return SerializeStruct(*this);
    }

private:
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
            return std::unexpected(current_error());
        }

        const auto frame = stack.back();
        if(frame.kind != container_kind::object || !frame.expect_key) {
            mark_invalid();
            return std::unexpected(current_error());
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
            return std::unexpected(current_error());
        }

        if(stack.back().kind != container_kind::array) {
            mark_invalid();
            return std::unexpected(current_error());
        }

        builder.end_array();
        stack.pop_back();
        return status();
    }

    status_t key(std::string_view key_name) {
        if(!is_valid || stack.empty()) {
            mark_invalid();
            return std::unexpected(current_error());
        }

        auto& frame = stack.back();
        if(frame.kind != container_kind::object || !frame.expect_key) {
            mark_invalid();
            return std::unexpected(current_error());
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

    void set_error(simdjson::error_code error) {
        if(last_error == simdjson::SUCCESS) {
            last_error = error;
        }
    }

    void mark_invalid(simdjson::error_code error = simdjson::TAPE_ERROR) {
        is_valid = false;
        set_error(error);
    }

    error_type current_error() const {
        if(last_error != simdjson::SUCCESS) {
            return json::make_error(last_error);
        }
        return error_kind::tape_error;
    }

    status_t status() const {
        if(is_valid) {
            return {};
        }
        return std::unexpected(current_error());
    }

private:
    bool is_valid = true;
    bool root_written = false;
    simdjson::error_code last_error = simdjson::SUCCESS;
    std::vector<container_frame> stack;
    simdjson::builder::string_builder builder;
};

template <typename T>
auto to_json(const T& value, std::optional<std::size_t> initial_capacity = std::nullopt)
    -> std::expected<std::string, error_kind> {
    Serializer serializer =
        initial_capacity.has_value() ? Serializer(*initial_capacity) : Serializer();
    auto result = serde::serialize(serializer, value);
    if(!result) {
        return std::unexpected(result.error());
    }
    return serializer.str();
}

static_assert(serde::serializer_like<Serializer>);

}  // namespace eventide::serde::json::simd
