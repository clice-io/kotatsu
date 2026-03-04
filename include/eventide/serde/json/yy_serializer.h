#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "eventide/serde/json/dom.h"
#include "eventide/serde/json/error.h"
#include "eventide/serde/serde.h"

namespace eventide::serde::json::yy {

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

    [[nodiscard]] bool valid() const noexcept {
        return builder.valid();
    }

    [[nodiscard]] error_type error() const noexcept {
        return builder.error();
    }

    result_t<json::Value> dom_value() const {
        return builder.dom_value();
    }

    result_t<std::string> str() const {
        return builder.to_json_string();
    }

    result_t<value_type> serialize_none() {
        return builder.value(nullptr);
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
        return builder.value(value);
    }

    result_t<value_type> serialize_int(std::int64_t value) {
        return builder.value(value);
    }

    result_t<value_type> serialize_uint(std::uint64_t value) {
        return builder.value(value);
    }

    result_t<value_type> serialize_float(double value) {
        if(std::isfinite(value)) {
            return builder.value(value);
        }
        return serialize_none();
    }

    result_t<value_type> serialize_char(char value) {
        const char chars[1] = {value};
        return builder.value(std::string_view(chars, 1));
    }

    result_t<value_type> serialize_str(std::string_view value) {
        return builder.value(value);
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

    result_t<value_type> append_json_value(const json::Value& value) {
        return builder.value(value);
    }

    result_t<value_type> append_json_value(const json::Array& value) {
        return builder.value(value);
    }

    result_t<value_type> append_json_value(const json::Object& value) {
        return builder.value(value);
    }

private:
    status_t begin_object() {
        return builder.begin_object();
    }

    result_t<value_type> end_object() {
        return builder.end_object();
    }

    status_t begin_array() {
        return builder.begin_array();
    }

    result_t<value_type> end_array() {
        return builder.end_array();
    }

    status_t key(std::string_view key_name) {
        return builder.key(key_name);
    }

private:
    json::Builder builder;
};

template <typename T>
auto to_json(const T& value) -> std::expected<std::string, Serializer::error_type> {
    Serializer serializer;
    if(!serializer.valid()) {
        return std::unexpected(serializer.error());
    }

    auto result = serde::serialize(serializer, value);
    if(!result) {
        return std::unexpected(result.error());
    }

    auto json = serializer.str();
    if(!json) {
        return std::unexpected(json.error());
    }
    return std::move(*json);
}

static_assert(serde::serializer_like<Serializer>);

}  // namespace eventide::serde::json::yy
