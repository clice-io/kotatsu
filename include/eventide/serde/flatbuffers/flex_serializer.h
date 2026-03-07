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

#include "eventide/serde/config.h"
#include "eventide/serde/detail/serialize_helpers.h"
#include "eventide/serde/flatbuffers/error.h"
#include "eventide/serde/serde.h"

#if __has_include(<flatbuffers/flexbuffers.h>)
#include <flatbuffers/flexbuffers.h>
#else
#error                                                                                             \
    "flatbuffers/flexbuffers.h not found. Enable EVENTIDE_SERDE_ENABLE_FLATBUFFERS or add flatbuffers include paths."
#endif

namespace eventide::serde::flex {

template <typename Config = config::default_config>
class Serializer {
public:
    using config_type = Config;
    using value_type = void;
    using error_type = error_code;

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

    explicit Serializer(std::size_t initial_capacity,
                        ::flexbuffers::BuilderFlag flags = ::flexbuffers::BUILDER_FLAG_SHARE_KEYS) :
        builder(initial_capacity, flags) {}

    result_t<std::span<const std::uint8_t>> view() {
        auto finalized = finalize();
        if(!finalized) {
            return std::unexpected(finalized.error());
        }

        const auto& out = builder.GetBuffer();
        return std::span<const std::uint8_t>(out.data(), out.size());
    }

    result_t<std::vector<std::uint8_t>> bytes() {
        auto out = view();
        if(!out) {
            return std::unexpected(out.error());
        }
        return std::vector<std::uint8_t>(out->begin(), out->end());
    }

    void clear() {
        builder.Clear();
        stack.clear();
        root_written = false;
        is_finished = false;
        is_valid = true;
        last_error = error_code::none;
    }

    bool valid() const {
        return is_valid;
    }

    error_type error() const {
        if(is_valid) {
            return error_code::none;
        }
        return current_error();
    }

    result_t<value_type> serialize_null() {
        return write_leaf([&] { builder.Null(); },
                          [&](const std::string& key_name) { builder.Null(key_name.c_str()); });
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
        return write_leaf(
            [&] { builder.Bool(value); },
            [&](const std::string& key_name) { builder.Bool(key_name.c_str(), value); });
    }

    result_t<value_type> serialize_int(std::int64_t value) {
        return write_leaf(
            [&] { builder.Int(value); },
            [&](const std::string& key_name) { builder.Int(key_name.c_str(), value); });
    }

    result_t<value_type> serialize_uint(std::uint64_t value) {
        return write_leaf(
            [&] { builder.UInt(value); },
            [&](const std::string& key_name) { builder.UInt(key_name.c_str(), value); });
    }

    result_t<value_type> serialize_float(double value) {
        if(!std::isfinite(value)) {
            return serialize_null();
        }

        return write_leaf(
            [&] { builder.Double(value); },
            [&](const std::string& key_name) { builder.Double(key_name.c_str(), value); });
    }

    result_t<value_type> serialize_char(char value) {
        const std::string text(1, value);
        return write_leaf(
            [&] { builder.String(text); },
            [&](const std::string& key_name) { builder.String(key_name.c_str(), text); });
    }

    result_t<value_type> serialize_str(std::string_view value) {
        return write_leaf([&] { builder.String(value.data(), value.size()); },
                          [&](const std::string& key_name) {
                              builder.Key(key_name.c_str());
                              builder.String(value.data(), value.size());
                          });
    }

    result_t<value_type> serialize_bytes(std::string_view value) {
        return write_leaf(
            [&] {
                builder.Blob(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
            },
            [&](const std::string& key_name) {
                builder.Blob(key_name.c_str(),
                             reinterpret_cast<const std::uint8_t*>(value.data()),
                             value.size());
            });
    }

    result_t<value_type> serialize_bytes(std::span<const std::byte> value) {
        return write_leaf(
            [&] {
                builder.Blob(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
            },
            [&](const std::string& key_name) {
                builder.Blob(key_name.c_str(),
                             reinterpret_cast<const std::uint8_t*>(value.data()),
                             value.size());
            });
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
    friend class serde::detail::SerializeArray<Serializer<Config>>;
    friend class serde::detail::SerializeObject<Serializer<Config>>;

    enum class container_kind : std::uint8_t { array, object };

    struct container_frame {
        container_kind kind = container_kind::array;
        std::size_t start = 0;
        bool expect_key = true;
        std::string pending_key;
    };

    status_t begin_object() {
        auto start = start_container(container_kind::object);
        if(!start) {
            return std::unexpected(start.error());
        }

        stack.push_back(container_frame{
            .kind = container_kind::object,
            .start = *start,
            .expect_key = true,
            .pending_key = {},
        });
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

        builder.EndMap(frame.start);
        if(builder.HasDuplicateKeys()) {
            mark_invalid(error_code::duplicate_keys);
            return std::unexpected(current_error());
        }

        stack.pop_back();
        return status();
    }

    status_t begin_array() {
        auto start = start_container(container_kind::array);
        if(!start) {
            return std::unexpected(start.error());
        }

        stack.push_back(container_frame{
            .kind = container_kind::array,
            .start = *start,
            .expect_key = false,
            .pending_key = {},
        });
        return {};
    }

    result_t<value_type> end_array() {
        if(!is_valid || stack.empty()) {
            mark_invalid();
            return std::unexpected(current_error());
        }

        const auto frame = stack.back();
        if(frame.kind != container_kind::array) {
            mark_invalid();
            return std::unexpected(current_error());
        }

        builder.EndVector(frame.start, false, false);
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

        frame.pending_key.assign(key_name.data(), key_name.size());
        frame.expect_key = false;
        return {};
    }

    template <typename WriteNoKey, typename WriteWithKey>
    result_t<value_type> write_leaf(WriteNoKey&& write_no_key, WriteWithKey&& write_with_key) {
        if(!before_value()) {
            return std::unexpected(current_error());
        }

        auto key_name = consume_parent_key();
        if(key_name.has_value()) {
            std::forward<WriteWithKey>(write_with_key)(*key_name);
        } else {
            std::forward<WriteNoKey>(write_no_key)();
        }

        return status();
    }

    result_t<std::size_t> start_container(container_kind kind) {
        if(!before_value()) {
            return std::unexpected(current_error());
        }

        auto key_name = consume_parent_key();
        if(key_name.has_value()) {
            if(kind == container_kind::object) {
                return builder.StartMap(key_name->c_str());
            }
            return builder.StartVector(key_name->c_str());
        }

        if(kind == container_kind::object) {
            return builder.StartMap();
        }
        return builder.StartVector();
    }

    std::optional<std::string> consume_parent_key() {
        if(stack.empty()) {
            return std::nullopt;
        }

        auto& frame = stack.back();
        if(frame.kind != container_kind::object) {
            return std::nullopt;
        }

        auto out = std::move(frame.pending_key);
        frame.pending_key.clear();
        return out;
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
            return true;
        }

        if(frame.expect_key) {
            mark_invalid();
            return false;
        }

        frame.expect_key = true;
        return true;
    }

    status_t finalize() {
        if(!is_valid) {
            return std::unexpected(current_error());
        }

        if(is_finished) {
            return {};
        }

        if(!root_written || !stack.empty()) {
            mark_invalid();
            return std::unexpected(current_error());
        }

        builder.Finish();
        is_finished = true;

        if(builder.HasDuplicateKeys()) {
            mark_invalid(error_code::duplicate_keys);
            return std::unexpected(current_error());
        }

        return {};
    }

    void set_error(error_type error) {
        if(last_error == error_code::none) {
            last_error = error;
        }
    }

    void mark_invalid(error_type error = error_code::invalid_state) {
        is_valid = false;
        set_error(error);
    }

    error_type current_error() const {
        if(last_error != error_code::none) {
            return last_error;
        }
        return error_code::unknown;
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
    bool is_finished = false;
    error_type last_error = error_code::none;
    std::vector<container_frame> stack;
    ::flexbuffers::Builder builder;
};

template <typename Config = config::default_config, typename T>
auto to_flatbuffer(const T& value, std::optional<std::size_t> initial_capacity = std::nullopt)
    -> std::expected<std::vector<std::uint8_t>, error_code> {
    Serializer<Config> serializer =
        initial_capacity.has_value() ? Serializer<Config>(*initial_capacity) : Serializer<Config>();
    auto result = serde::serialize(serializer, value);
    if(!result) {
        return std::unexpected(result.error());
    }
    return serializer.bytes();
}

static_assert(serde::serializer_like<Serializer<>>);

}  // namespace eventide::serde::flex
