#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/codec/content/dom.h"
#include "kota/codec/content/error.h"
#include "kota/codec/config.h"
#include "kota/codec/serde.h"
#include "kota/codec/detail/backend_helpers.h"
#include "kota/codec/detail/narrow.h"

namespace kota::codec::content {

template <typename Config = config::default_config>
class Deserializer {
public:
    using config_type = Config;
    using error_type = content::error;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    class DeserializeArray :
        public serde::detail::IndexedArrayDeserializer<Deserializer, content::ArrayRef> {
        using Base = serde::detail::IndexedArrayDeserializer<Deserializer, content::ArrayRef>;
        friend class Deserializer;

        DeserializeArray(Deserializer& deserializer,
                         content::ArrayRef array,
                         std::size_t expectedLength,
                         bool isStrictLength) :
            Base(deserializer, array, array.size(), expectedLength, isStrictLength) {}
    };

    class DeserializeObject :
        public serde::detail::IndexedObjectDeserializer<Deserializer, content::ValueRef> {
        using Base = serde::detail::IndexedObjectDeserializer<Deserializer, content::ValueRef>;
        friend class Deserializer;

        DeserializeObject(Deserializer& deserializer, content::ObjectRef object) :
            Base(deserializer) {
            auto collected = deserializer.collect_object_entries(object);
            if(!collected) {
                deserializer.mark_invalid(collected.error());
                return;
            }
            this->entries = std::move(*collected);
        }
    };

    using DeserializeSeq = DeserializeArray;
    using DeserializeTuple = DeserializeArray;
    using DeserializeMap = DeserializeObject;
    using DeserializeStruct = DeserializeObject;

    explicit Deserializer(const content::Value& value) :
        owned_root_value(value), root_value(owned_root_value->as_ref()) {
        if(!root_value.valid()) {
            mark_invalid();
        }
    }

    explicit Deserializer(content::Value&& value) :
        owned_root_value(std::move(value)), root_value(owned_root_value->as_ref()) {
        if(!root_value.valid()) {
            mark_invalid();
        }
    }

    explicit Deserializer(content::ValueRef value) : root_value(value) {
        if(!root_value.valid()) {
            mark_invalid();
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return is_valid;
    }

    [[nodiscard]] error_type error() const noexcept {
        return last_error;
    }

    status_t finish() {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        if(!root_consumed) {
            return mark_invalid();
        }
        return {};
    }

    result_t<bool> deserialize_none() {
        if(!is_valid) {
            return std::unexpected(last_error);
        }

        auto ref = peek_value_ref();
        if(!ref) {
            return std::unexpected(ref.error());
        }

        const bool isNone = ref->is_null();
        if(isNone && !has_current_value) {
            root_consumed = true;
        }
        return isNone;
    }

    template <typename... Ts>
    status_t deserialize_variant(std::variant<Ts...>& value) {
        auto valueKind = peek_value_kind();
        if(!valueKind) {
            return std::unexpected(valueKind.error());
        }

        auto source = consume_value_ref();
        if(!source) {
            return std::unexpected(source.error());
        }

        auto result = serde::try_variant_dispatch<Deserializer>(*source,
                                                                map_to_type_hint(*valueKind),
                                                                value,
                                                                error_type::type_mismatch);
        if(!result) {
            return mark_invalid(result.error());
        }
        return {};
    }

    status_t deserialize_bool(bool& value) {
        return read_scalar(value, [](content::ValueRef ref) -> result_t<bool> {
            auto parsed = ref.get_bool();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
    }

    template <serde::int_like T>
    status_t deserialize_int(T& value) {
        std::int64_t parsed = 0;
        auto status = read_scalar(parsed, [](content::ValueRef ref) -> result_t<std::int64_t> {
            auto parsed = ref.get_int();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        auto narrowed = serde::detail::narrow_int<T>(parsed, error_type::number_out_of_range);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }

        value = *narrowed;
        return {};
    }

    template <serde::uint_like T>
    status_t deserialize_uint(T& value) {
        std::uint64_t parsed = 0;
        auto status = read_scalar(parsed, [](content::ValueRef ref) -> result_t<std::uint64_t> {
            auto parsed = ref.get_uint();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        auto narrowed = serde::detail::narrow_uint<T>(parsed, error_type::number_out_of_range);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }

        value = *narrowed;
        return {};
    }

    template <serde::floating_like T>
    status_t deserialize_float(T& value) {
        double parsed = 0.0;
        auto status = read_scalar(parsed, [](content::ValueRef ref) -> result_t<double> {
            auto parsed = ref.get_double();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        auto narrowed = serde::detail::narrow_float<T>(parsed, error_type::number_out_of_range);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }

        value = *narrowed;
        return {};
    }

    status_t deserialize_char(char& value) {
        std::string_view text;
        auto status = read_scalar(text, [](content::ValueRef ref) -> result_t<std::string_view> {
            auto parsed = ref.get_string();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        auto narrowed = serde::detail::narrow_char(text, error_type::type_mismatch);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }

        value = *narrowed;
        return {};
    }

    status_t deserialize_str(std::string& value) {
        std::string_view text;
        auto status = read_scalar(text, [](content::ValueRef ref) -> result_t<std::string_view> {
            auto parsed = ref.get_string();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        value.assign(text.data(), text.size());
        return {};
    }

    status_t deserialize_bytes(std::vector<std::byte>& value) {
        return serde::detail::deserialize_bytes_from_seq(*this, value);
    }

    result_t<DeserializeSeq> deserialize_seq(std::optional<std::size_t> len) {
        KOTA_EXPECTED_TRY_V(auto array, open_array());
        return DeserializeSeq(*this, array, len.value_or(0), false);
    }

    result_t<DeserializeTuple> deserialize_tuple(std::size_t len) {
        KOTA_EXPECTED_TRY_V(auto array, open_array());
        return DeserializeTuple(*this, array, len, true);
    }

    result_t<DeserializeMap> deserialize_map(std::optional<std::size_t> /*len*/) {
        KOTA_EXPECTED_TRY_V(auto object, open_object());

        DeserializeMap map(*this, object);
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        return map;
    }

    result_t<DeserializeStruct> deserialize_struct(std::string_view /*name*/, std::size_t /*len*/) {
        KOTA_EXPECTED_TRY_V(auto object, open_object());

        DeserializeStruct structure(*this, object);
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        return structure;
    }

    result_t<content::Value> capture_dom_value() {
        KOTA_EXPECTED_TRY_V(auto source, consume_value_ref());
        auto copied = content::Value::copy_of(source);
        if(!copied.has_value()) {
            return mark_invalid(copied.error());
        }
        return std::move(*copied);
    }

private:
    friend class serde::detail::IndexedArrayDeserializer<Deserializer, content::ArrayRef>;
    friend class serde::detail::IndexedObjectDeserializer<Deserializer, content::ValueRef>;

    enum class value_kind : std::uint8_t {
        null,
        boolean,
        number,
        string,
        array,
        object,
    };

    template <typename T, typename Reader>
    status_t read_scalar(T& out, Reader&& reader) {
        if(!is_valid) {
            return std::unexpected(last_error);
        }

        auto ref = peek_value_ref();
        if(!ref) {
            return std::unexpected(ref.error());
        }

        auto parsed = std::forward<Reader>(reader)(*ref);
        if(!parsed) {
            return mark_invalid(parsed.error());
        }

        out = *parsed;
        if(!has_current_value) {
            root_consumed = true;
        }
        return {};
    }

    template <typename T>
    status_t deserialize_from_value_ref(content::ValueRef input, T& out) {
        struct value_scope {
            value_scope(Deserializer& deserializer, content::ValueRef input) :
                deserializer(deserializer),
                previous_has_current_value(deserializer.has_current_value),
                previous_current_value(deserializer.current_value) {
                deserializer.current_value = input;
                deserializer.has_current_value = true;
            }

            ~value_scope() {
                deserializer.current_value = previous_current_value;
                deserializer.has_current_value = previous_has_current_value;
            }

            Deserializer& deserializer;
            bool previous_has_current_value;
            content::ValueRef previous_current_value;
        };

        value_scope scope(*this, input);
        return serde::deserialize(*this, out);
    }

    template <typename T>
    status_t deserialize_element_value(content::ArrayRef array, std::size_t index, T& out) {
        return deserialize_from_value_ref(array[index], out);
    }

    template <typename T>
    status_t deserialize_entry_value(content::ValueRef value, T& out) {
        return deserialize_from_value_ref(value, out);
    }

    result_t<value_kind> peek_value_kind() {
        auto ref = peek_value_ref();
        if(!ref) {
            return std::unexpected(ref.error());
        }

        if(ref->is_null()) {
            return value_kind::null;
        }
        if(ref->is_bool()) {
            return value_kind::boolean;
        }
        if(ref->is_number()) {
            return value_kind::number;
        }
        if(ref->is_string()) {
            return value_kind::string;
        }
        if(ref->is_array()) {
            return value_kind::array;
        }
        if(ref->is_object()) {
            return value_kind::object;
        }

        return mark_invalid(error_type::type_mismatch);
    }

    static serde::type_hint map_to_type_hint(value_kind kind) {
        switch(kind) {
            case value_kind::null: return serde::type_hint::null_like;
            case value_kind::boolean: return serde::type_hint::boolean;
            case value_kind::number: return serde::type_hint::integer | serde::type_hint::floating;
            case value_kind::string: return serde::type_hint::string;
            case value_kind::array: return serde::type_hint::array;
            case value_kind::object: return serde::type_hint::object;
            default: return serde::type_hint::any;
        }
    }

    result_t<std::vector<typename DeserializeObject::entry>>
        collect_object_entries(content::ObjectRef object) {
        std::vector<typename DeserializeObject::entry> entries;
        entries.reserve(object.size());

        for(auto entry: object) {
            entries.push_back(typename DeserializeObject::entry{
                .key = entry.key,
                .value = entry.value,
            });
        }

        return entries;
    }

    result_t<content::ValueRef> access_value_ref(bool consume) {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        if(has_current_value) {
            return current_value;
        }
        if(root_consumed || !root_value.valid()) {
            return mark_invalid();
        }
        if(consume) {
            root_consumed = true;
        }
        return root_value;
    }

    result_t<content::ValueRef> peek_value_ref() {
        return access_value_ref(false);
    }

    result_t<content::ValueRef> consume_value_ref() {
        return access_value_ref(true);
    }

    result_t<content::ArrayRef> open_array() {
        auto ref = consume_value_ref();
        if(!ref) {
            return std::unexpected(ref.error());
        }

        auto array = ref->get_array();
        if(!array) {
            return mark_invalid(error_type::type_mismatch);
        }
        return *array;
    }

    result_t<content::ObjectRef> open_object() {
        auto ref = consume_value_ref();
        if(!ref) {
            return std::unexpected(ref.error());
        }

        auto object = ref->get_object();
        if(!object) {
            return mark_invalid(error_type::type_mismatch);
        }
        return std::move(*object);
    }

    std::unexpected<error_type> mark_invalid(error_type error = error_type::invalid_state) {
        is_valid = false;
        if(last_error == error_type::invalid_state || error != error_type::invalid_state) {
            last_error = error;
        }
        return std::unexpected(last_error);
    }

private:
    bool is_valid = true;
    bool root_consumed = false;
    error_type last_error = error_type::invalid_state;
    std::optional<content::Value> owned_root_value{};
    content::ValueRef root_value{};
    bool has_current_value = false;
    content::ValueRef current_value{};
};

static_assert(serde::deserializer_like<Deserializer<>>);

}  // namespace kota::codec::content
