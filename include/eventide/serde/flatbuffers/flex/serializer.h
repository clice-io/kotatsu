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

#include "eventide/serde/flatbuffers/error.h"
#include "eventide/serde/serde.h"

#if __has_include(<flatbuffers/flexbuffers.h>)
#include <flatbuffers/flexbuffers.h>
#else
#error                                                                                             \
    "flatbuffers/flexbuffers.h not found. Enable EVENTIDE_SERDE_ENABLE_FLATBUFFERS or add flatbuffers include paths."
#endif

namespace eventide::serde::flex {

class Serializer {
public:
    using value_type = void;
    using error_type = error_code;

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
    explicit Serializer(std::size_t initial_capacity,
                        ::flexbuffers::BuilderFlag flags = ::flexbuffers::BUILDER_FLAG_SHARE_KEYS);

    result_t<std::span<const std::uint8_t>> view();
    result_t<std::vector<std::uint8_t>> bytes();

    void clear();

    bool valid() const;
    error_type error() const;

    result_t<value_type> serialize_null();

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

    result_t<value_type> serialize_bool(bool value);
    result_t<value_type> serialize_int(std::int64_t value);
    result_t<value_type> serialize_uint(std::uint64_t value);
    result_t<value_type> serialize_float(double value);
    result_t<value_type> serialize_char(char value);
    result_t<value_type> serialize_str(std::string_view value);
    result_t<value_type> serialize_bytes(std::string_view value);
    result_t<value_type> serialize_bytes(std::span<const std::byte> value);

    result_t<SerializeSeq> serialize_seq(std::optional<std::size_t> len);
    result_t<SerializeTuple> serialize_tuple(std::size_t len);
    result_t<SerializeMap> serialize_map(std::optional<std::size_t> len);
    result_t<SerializeStruct> serialize_struct(std::string_view name, std::size_t len);

private:
    enum class container_kind : std::uint8_t { array, object };

    struct container_frame {
        container_kind kind = container_kind::array;
        std::size_t start = 0;
        bool expect_key = true;
        std::string pending_key;
    };

    status_t begin_object();
    result_t<value_type> end_object();

    status_t begin_array();
    result_t<value_type> end_array();

    status_t key(std::string_view key_name);

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

    result_t<std::size_t> start_container(container_kind kind);
    std::optional<std::string> consume_parent_key();

    bool before_value();
    status_t finalize();

    void set_error(error_type error);
    void mark_invalid(error_type error = error_code::invalid_state);
    error_type current_error() const;

    status_t status() const;

private:
    bool is_valid = true;
    bool root_written = false;
    bool is_finished = false;
    error_type last_error = error_code::none;
    std::vector<container_frame> stack;
    ::flexbuffers::Builder builder;
};

template <typename T>
auto to_flatbuffer(const T& value, std::optional<std::size_t> initial_capacity = std::nullopt)
    -> std::expected<std::vector<std::uint8_t>, error_code> {
    Serializer serializer =
        initial_capacity.has_value() ? Serializer(*initial_capacity) : Serializer();
    auto result = serde::serialize(serializer, value);
    if(!result) {
        return std::unexpected(result.error());
    }
    return serializer.bytes();
}

static_assert(serde::serializer_like<Serializer>);

}  // namespace eventide::serde::flex
