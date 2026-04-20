#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
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
#include "kota/codec/content/document.h"
#include "kota/codec/content/error.h"
#include "kota/codec/detail/narrow.h"

namespace kota::codec::content {

template <typename Config = config::default_config>
class Deserializer {
public:
    using config_type = Config;
    using error_type = content::error;

    constexpr static auto backend_kind_v = backend_kind::streaming;
    constexpr static auto field_mode_v = field_mode::by_name;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    explicit Deserializer(const content::Value& value) :
        owned_root_value(value), root_value(owned_root_value->cursor()) {
        if(!root_value.valid()) {
            (void)mark_invalid();
        }
    }

    explicit Deserializer(content::Value&& value) :
        owned_root_value(std::move(value)), root_value(owned_root_value->cursor()) {
        if(!root_value.valid()) {
            (void)mark_invalid();
        }
    }

    explicit Deserializer(content::Cursor value) : root_value(value) {
        if(!root_value.valid()) {
            (void)mark_invalid();
        }
    }

    Deserializer(const Deserializer&) = delete;
    Deserializer(Deserializer&&) = delete;
    auto operator=(const Deserializer&) -> Deserializer& = delete;
    auto operator=(Deserializer&&) -> Deserializer& = delete;

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

        auto result = codec::try_variant_dispatch<Deserializer>(*source,
                                                                map_to_type_hint(*valueKind),
                                                                value,
                                                                error_type::type_mismatch);
        if(!result) {
            return mark_invalid(result.error());
        }
        return {};
    }

    status_t deserialize_bool(bool& value) {
        return read_scalar(value, [](content::Cursor ref) -> result_t<bool> {
            auto parsed = ref.get_bool();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
    }

    template <codec::int_like T>
    status_t deserialize_int(T& value) {
        std::int64_t parsed = 0;
        auto status = read_scalar(parsed, [](content::Cursor ref) -> result_t<std::int64_t> {
            auto parsed = ref.get_int();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        auto narrowed = codec::detail::narrow_int<T>(parsed, error_type::number_out_of_range);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }

        value = *narrowed;
        return {};
    }

    template <codec::uint_like T>
    status_t deserialize_uint(T& value) {
        std::uint64_t parsed = 0;
        auto status = read_scalar(parsed, [](content::Cursor ref) -> result_t<std::uint64_t> {
            auto parsed = ref.get_uint();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        auto narrowed = codec::detail::narrow_uint<T>(parsed, error_type::number_out_of_range);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }

        value = *narrowed;
        return {};
    }

    template <codec::floating_like T>
    status_t deserialize_float(T& value) {
        double parsed = 0.0;
        auto status = read_scalar(parsed, [](content::Cursor ref) -> result_t<double> {
            auto parsed = ref.get_double();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        auto narrowed = codec::detail::narrow_float<T>(parsed, error_type::number_out_of_range);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }

        value = *narrowed;
        return {};
    }

    status_t deserialize_char(char& value) {
        std::string_view text;
        auto status = read_scalar(text, [](content::Cursor ref) -> result_t<std::string_view> {
            auto parsed = ref.get_string();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        auto narrowed = codec::detail::narrow_char(text, error_type::type_mismatch);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }

        value = *narrowed;
        return {};
    }

    status_t deserialize_str(std::string& value) {
        std::string_view text;
        auto status = read_scalar(text, [](content::Cursor ref) -> result_t<std::string_view> {
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
        KOTA_EXPECTED_TRY(begin_array());
        value.clear();
        while(true) {
            KOTA_EXPECTED_TRY_V(auto has_next, next_element());
            if(!has_next) {
                break;
            }
            std::uint64_t byte_val = 0;
            KOTA_EXPECTED_TRY(deserialize_uint(byte_val));
            if(byte_val > 255U) {
                return mark_invalid(error_type::number_out_of_range);
            }
            value.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(byte_val)));
        }
        return end_array();
    }

    result_t<content::Value> capture_dom_value() {
        KOTA_EXPECTED_TRY_V(auto source, consume_value_ref());
        const content::Value* ptr = source.unwrap();
        if(ptr == nullptr) {
            return mark_invalid();
        }
        return *ptr;
    }

    // --- New-style streaming struct interface ---

    status_t begin_object() {
        KOTA_EXPECTED_TRY_V(auto obj, open_object());
        deser_stack.push_back(deser_frame{.object = obj, .it = obj->begin()});
        return {};
    }

    result_t<std::optional<std::string_view>> next_field() {
        if(!is_valid || deser_stack.empty()) {
            return mark_invalid();
        }
        auto& frame = deser_stack.back();
        if(frame.it == frame.object->end()) {
            return std::optional<std::string_view>(std::nullopt);
        }
        const auto& entry = *frame.it;
        ++frame.it;
        current_value = content::Cursor(entry.value);
        has_current_value = true;
        return std::optional<std::string_view>(std::string_view(entry.key));
    }

    status_t skip_field_value() {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        // DOM-based: nothing to actually skip, just clear current value
        has_current_value = false;
        return {};
    }

    status_t end_object() {
        if(!is_valid || deser_stack.empty()) {
            return mark_invalid();
        }
        deser_stack.pop_back();
        return {};
    }

    // --- New-style streaming array interface ---

    status_t begin_array() {
        KOTA_EXPECTED_TRY_V(auto arr, open_array());
        array_stack.push_back({arr, 0});
        return {};
    }

    result_t<bool> next_element() {
        if(!is_valid || array_stack.empty()) {
            return mark_invalid();
        }
        auto& frame = array_stack.back();
        if(frame.index >= frame.array->size()) {
            has_current_value = false;
            return false;
        }
        current_value = content::Cursor((*frame.array)[frame.index]);
        has_current_value = true;
        ++frame.index;
        return true;
    }

    status_t end_array() {
        if(!is_valid || array_stack.empty()) {
            return mark_invalid();
        }
        array_stack.pop_back();
        has_current_value = false;
        return {};
    }

private:
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
    status_t deserialize_from_value_ref(content::Cursor input, T& out) {
        struct value_scope {
            value_scope(Deserializer& deserializer, content::Cursor input) :
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
            content::Cursor previous_current_value;
        };

        value_scope scope(*this, input);
        return codec::deserialize(*this, out);
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

    static codec::type_hint map_to_type_hint(value_kind kind) {
        switch(kind) {
            case value_kind::null: return codec::type_hint::null_like;
            case value_kind::boolean: return codec::type_hint::boolean;
            case value_kind::number: return codec::type_hint::integer | codec::type_hint::floating;
            case value_kind::string: return codec::type_hint::string;
            case value_kind::array: return codec::type_hint::array;
            case value_kind::object: return codec::type_hint::object;
            default: return codec::type_hint::any;
        }
    }

    result_t<content::Cursor> access_value_ref(bool consume) {
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

    result_t<content::Cursor> peek_value_ref() {
        return access_value_ref(false);
    }

    result_t<content::Cursor> consume_value_ref() {
        return access_value_ref(true);
    }

    result_t<const content::Array*> open_array() {
        auto ref = consume_value_ref();
        if(!ref) {
            return std::unexpected(ref.error());
        }

        const content::Array* array = ref->get_array();
        if(array == nullptr) {
            return mark_invalid(error_type::type_mismatch);
        }
        return array;
    }

    result_t<const content::Object*> open_object() {
        auto ref = consume_value_ref();
        if(!ref) {
            return std::unexpected(ref.error());
        }

        const content::Object* object = ref->get_object();
        if(object == nullptr) {
            return mark_invalid(error_type::type_mismatch);
        }
        return object;
    }

    std::unexpected<error_type> mark_invalid(error_type error = error_type::invalid_state) {
        is_valid = false;
        if(last_error == error_type::invalid_state || error != error_type::invalid_state) {
            last_error = error;
        }
        return std::unexpected(last_error);
    }

private:
    struct deser_frame {
        const content::Object* object = nullptr;
        content::Object::const_iterator it{};
    };

    struct array_frame {
        const content::Array* array = nullptr;
        std::size_t index = 0;
    };

    bool is_valid = true;
    bool root_consumed = false;
    error_type last_error = error_type::invalid_state;
    std::optional<content::Value> owned_root_value{};
    content::Cursor root_value{};
    bool has_current_value = false;
    content::Cursor current_value{};
    std::vector<deser_frame> deser_stack;
    std::vector<array_frame> array_stack;
};

static_assert(codec::deserializer_like<Deserializer<>>);

}  // namespace kota::codec::content

namespace kota::codec {

template <typename Config>
struct deserialize_traits<content::Deserializer<Config>, content::Value> {
    using error_type = typename content::Deserializer<Config>::error_type;

    static auto deserialize(content::Deserializer<Config>& d, content::Value& value)
        -> std::expected<void, error_type> {
        auto dom = d.capture_dom_value();
        if(!dom) {
            return std::unexpected(dom.error());
        }
        value = std::move(*dom);
        return {};
    }
};

template <typename Config>
struct deserialize_traits<content::Deserializer<Config>, content::Array> {
    using error_type = typename content::Deserializer<Config>::error_type;

    static auto deserialize(content::Deserializer<Config>& d, content::Array& value)
        -> std::expected<void, error_type> {
        auto dom = d.capture_dom_value();
        if(!dom) {
            return std::unexpected(dom.error());
        }
        content::Array* arr = dom->get_array();
        if(arr == nullptr) {
            return std::unexpected(content::error::type_mismatch);
        }
        value = std::move(*arr);
        return {};
    }
};

template <typename Config>
struct deserialize_traits<content::Deserializer<Config>, content::Object> {
    using error_type = typename content::Deserializer<Config>::error_type;

    static auto deserialize(content::Deserializer<Config>& d, content::Object& value)
        -> std::expected<void, error_type> {
        auto dom = d.capture_dom_value();
        if(!dom) {
            return std::unexpected(dom.error());
        }
        content::Object* obj = dom->get_object();
        if(obj == nullptr) {
            return std::unexpected(content::error::type_mismatch);
        }
        value = std::move(*obj);
        return {};
    }
};

}  // namespace kota::codec
