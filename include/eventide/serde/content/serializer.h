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
#include <vector>

#include "eventide/common/expected_try.h"
#include "eventide/serde/content/dom.h"
#include "eventide/serde/content/error.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/serde.h"
#include "eventide/serde/serde/utils/backend_helpers.h"

namespace eventide::serde::content {

template <typename Config = config::default_config>
class Serializer {
public:
    using config_type = Config;
    using value_type = void;
    using error_type = content::error_kind;

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

    [[nodiscard]] bool valid() const noexcept {
        return is_valid && doc.valid();
    }

    [[nodiscard]] error_type error() const noexcept {
        return last_error;
    }

    result_t<content::Value> dom_value() const {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        if(!root_is_written || !stack.empty()) {
            return std::unexpected(error_type::invalid_state);
        }
        return doc.dom_value();
    }

    result_t<std::string> str() const {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        if(!root_is_written || !stack.empty()) {
            return std::unexpected(error_type::invalid_state);
        }
        return doc.to_json_string();
    }

    result_t<value_type> serialize_null() {
        return append_value(doc.unchecked_make_null());
    }

    template <typename T>
    result_t<value_type> serialize_some(const T& value) {
        return serde::serialize(*this, value);
    }

    result_t<value_type> serialize_bool(bool value) {
        return append_value(doc.unchecked_make_bool(value));
    }

    result_t<value_type> serialize_int(std::int64_t value) {
        return append_value(doc.unchecked_make_int(value));
    }

    result_t<value_type> serialize_uint(std::uint64_t value) {
        return append_value(doc.unchecked_make_uint(value));
    }

    result_t<value_type> serialize_float(double value) {
        if(std::isfinite(value)) {
            return append_value(doc.unchecked_make_real(value));
        }
        return serialize_null();
    }

    result_t<value_type> serialize_char(char value) {
        const char chars[1] = {value};
        return append_value(doc.unchecked_make_str(std::string_view(chars, 1)));
    }

    result_t<value_type> serialize_str(std::string_view value) {
        return append_value(doc.unchecked_make_str(value));
    }

    result_t<value_type> serialize_bytes(std::string_view value) {
        ET_EXPECTED_TRY_V(auto seq, serialize_seq(value.size()));

        for(unsigned char byte: value) {
            ET_EXPECTED_TRY(seq.serialize_element(static_cast<std::uint64_t>(byte)));
        }
        return seq.end();
    }

    result_t<value_type> serialize_bytes(std::span<const std::byte> value) {
        ET_EXPECTED_TRY_V(auto seq, serialize_seq(value.size()));

        for(std::byte byte: value) {
            ET_EXPECTED_TRY(seq.serialize_element(
                static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(byte))));
        }
        return seq.end();
    }

    result_t<SerializeSeq> serialize_seq(std::optional<std::size_t> /*len*/) {
        ET_EXPECTED_TRY(begin_array());
        return SerializeSeq(*this);
    }

    result_t<SerializeTuple> serialize_tuple(std::size_t /*len*/) {
        ET_EXPECTED_TRY(begin_array());
        return SerializeTuple(*this);
    }

    result_t<SerializeMap> serialize_map(std::optional<std::size_t> /*len*/) {
        ET_EXPECTED_TRY(begin_object());
        return SerializeMap(*this);
    }

    result_t<SerializeStruct> serialize_struct(std::string_view /*name*/, std::size_t /*len*/) {
        ET_EXPECTED_TRY(begin_object());
        return SerializeStruct(*this);
    }

    result_t<value_type> append_dom_value(const content::Value& value) {
        return append_dom_value_impl(value);
    }

    result_t<value_type> append_dom_value(const content::Array& value) {
        return append_dom_value_impl(value);
    }

    result_t<value_type> append_dom_value(const content::Object& value) {
        return append_dom_value_impl(value);
    }

private:
    friend class serde::detail::SerializeArray<Serializer<Config>>;
    friend class serde::detail::SerializeObject<Serializer<Config>>;

    status_t begin_object() {
        if(!valid()) {
            return std::unexpected(last_error);
        }
        auto* obj = doc.unchecked_make_object();
        if(obj == nullptr) {
            mark_invalid(error_type::allocation_failed);
            return std::unexpected(last_error);
        }
        auto appended = append_value(obj);
        if(!appended) {
            return std::unexpected(appended.error());
        }
        stack.push_back(container_frame{
            .kind = container_kind::object,
            .value = obj,
            .expect_key = true,
        });
        return {};
    }

    result_t<value_type> end_object() {
        if(!valid() || stack.empty()) {
            mark_invalid();
            return std::unexpected(last_error);
        }
        const auto& frame = stack.back();
        if(frame.kind != container_kind::object || !frame.expect_key) {
            mark_invalid();
            return std::unexpected(last_error);
        }
        stack.pop_back();
        return {};
    }

    status_t begin_array() {
        if(!valid()) {
            return std::unexpected(last_error);
        }
        auto* arr = doc.unchecked_make_array();
        if(arr == nullptr) {
            mark_invalid(error_type::allocation_failed);
            return std::unexpected(last_error);
        }
        auto appended = append_value(arr);
        if(!appended) {
            return std::unexpected(appended.error());
        }
        stack.push_back(container_frame{
            .kind = container_kind::array,
            .value = arr,
            .expect_key = false,
        });
        return {};
    }

    result_t<value_type> end_array() {
        if(!valid() || stack.empty()) {
            mark_invalid();
            return std::unexpected(last_error);
        }
        if(stack.back().kind != container_kind::array) {
            mark_invalid();
            return std::unexpected(last_error);
        }
        stack.pop_back();
        return {};
    }

    status_t key(std::string_view key_name) {
        if(!valid() || stack.empty()) {
            mark_invalid();
            return std::unexpected(last_error);
        }
        auto& frame = stack.back();
        if(frame.kind != container_kind::object || !frame.expect_key) {
            mark_invalid();
            return std::unexpected(last_error);
        }
        frame.pending_key = doc.unchecked_make_str(key_name);
        if(frame.pending_key == nullptr) {
            mark_invalid(error_type::allocation_failed);
            return std::unexpected(last_error);
        }
        frame.expect_key = false;
        return {};
    }

    template <typename T>
    result_t<value_type> append_dom_value_impl(const T& value) {
        if(!valid() || !value.valid()) {
            mark_invalid();
            return std::unexpected(last_error);
        }
        auto mut_val = Value::make_mut_value(doc.raw(), value);
        if(!mut_val) {
            mark_invalid(mut_val.error());
            return std::unexpected(last_error);
        }
        return append_value(*mut_val);
    }

    status_t append_value(yyjson_mut_val* val) {
        if(!valid()) {
            return std::unexpected(last_error);
        }
        if(val == nullptr) {
            mark_invalid(error_type::allocation_failed);
            return std::unexpected(last_error);
        }
        if(stack.empty()) {
            if(root_is_written) {
                mark_invalid();
                return std::unexpected(last_error);
            }
            doc.unchecked_set_root(val);
            root_is_written = true;
            return {};
        }

        auto& frame = stack.back();
        if(frame.kind == container_kind::array) {
            if(!Document::unchecked_arr_add_val(frame.value, val)) {
                mark_invalid(error_type::allocation_failed);
                return std::unexpected(last_error);
            }
            return {};
        }

        if(frame.expect_key) {
            mark_invalid();
            return std::unexpected(last_error);
        }

        if(!Document::unchecked_obj_add(frame.value, frame.pending_key, val)) {
            mark_invalid(error_type::allocation_failed);
            return std::unexpected(last_error);
        }
        frame.pending_key = nullptr;
        frame.expect_key = true;
        return {};
    }

    void mark_invalid(error_type error = error_type::invalid_state) noexcept {
        is_valid = false;
        if(last_error == error_type::invalid_state || error != error_type::invalid_state) {
            last_error = error;
        }
    }

private:
    enum class container_kind : std::uint8_t { array, object };

    struct container_frame {
        container_kind kind;
        yyjson_mut_val* value = nullptr;
        bool expect_key = true;
        yyjson_mut_val* pending_key = nullptr;
    };

    bool is_valid = true;
    bool root_is_written = false;
    error_type last_error = error_type::invalid_state;
    std::vector<container_frame> stack;
    content::Document doc;
};

static_assert(serde::serializer_like<Serializer<>>);

}  // namespace eventide::serde::content
