#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/support/small_vector.h"
#include "kota/codec/detail/backend.h"
#include "kota/codec/detail/codec.h"
#include "kota/codec/detail/config.h"
#include "kota/codec/detail/variant_dispatch.h"
#include "kota/codec/json/error.h"

namespace kota::codec::json {

namespace detail {

struct simdjson_source_adapter {
    struct node_type {
        simdjson::ondemand::value val{};
        simdjson::ondemand::object* obj_ptr = nullptr;
        simdjson::ondemand::array* arr_ptr = nullptr;

        node_type() = default;

        node_type(simdjson::ondemand::value v) : val(v) {}

        explicit node_type(simdjson::ondemand::object* o) : obj_ptr(o) {}

        explicit node_type(simdjson::ondemand::array* a) : arr_ptr(a) {}
    };

    static meta::type_kind kind_of(node_type node) {
        if(node.obj_ptr)
            return meta::type_kind::structure;
        if(node.arr_ptr)
            return meta::type_kind::array;
        simdjson::ondemand::json_type type;
        if(node.val.type().get(type) != simdjson::SUCCESS)
            return meta::type_kind::any;
        switch(type) {
            case simdjson::ondemand::json_type::null: return meta::type_kind::null;
            case simdjson::ondemand::json_type::boolean: return meta::type_kind::boolean;
            case simdjson::ondemand::json_type::number: {
                simdjson::ondemand::number_type nt;
                if(node.val.get_number_type().get(nt) != simdjson::SUCCESS)
                    return meta::type_kind::any;
                if(nt == simdjson::ondemand::number_type::floating_point_number)
                    return meta::type_kind::float64;
                if(nt == simdjson::ondemand::number_type::unsigned_integer)
                    return meta::type_kind::uint64;
                return meta::type_kind::int64;
            }
            case simdjson::ondemand::json_type::string: return meta::type_kind::string;
            case simdjson::ondemand::json_type::array: return meta::type_kind::array;
            case simdjson::ondemand::json_type::object: return meta::type_kind::structure;
            default: return meta::type_kind::any;
        }
    }

    template <typename Fn>
    static void for_each_field(node_type node, Fn&& fn) {
        auto iterate = [&](simdjson::ondemand::object& obj) {
            for(auto field_result: obj) {
                simdjson::ondemand::field field;
                if(std::move(field_result).get(field) != simdjson::SUCCESS)
                    break;
                std::string_view key;
                if(field.unescaped_key().get(key) != simdjson::SUCCESS)
                    break;
                auto field_value = std::move(field).value();
                fn(key, node_type(std::move(field_value)));
            }
        };

        if(node.obj_ptr) {
            iterate(*node.obj_ptr);
        } else {
            simdjson::ondemand::object obj;
            if(node.val.get_object().get(obj) != simdjson::SUCCESS)
                return;
            iterate(obj);
        }
    }

    template <typename Fn>
    static void for_each_element(node_type node, Fn&& fn) {
        auto iterate = [&](simdjson::ondemand::array& arr) {
            std::size_t total = 0;
            if(arr.count_elements().get(total) != simdjson::SUCCESS)
                return;
            std::size_t idx = 0;
            for(auto elem_result: arr) {
                simdjson::ondemand::value elem;
                if(std::move(elem_result).get(elem) != simdjson::SUCCESS)
                    break;
                fn(idx, total, node_type(std::move(elem)));
                ++idx;
            }
        };

        if(node.arr_ptr) {
            iterate(*node.arr_ptr);
        } else {
            simdjson::ondemand::array arr;
            if(node.val.get_array().get(arr) != simdjson::SUCCESS)
                return;
            iterate(arr);
        }
    }
};

}  // namespace detail

template <typename Config = config::default_config>
class Deserializer {
public:
    using config_type = Config;
    using error_type = json::error;

    constexpr static auto backend_kind_v = backend_kind::streaming;
    constexpr static auto field_mode_v = field_mode::by_name;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    explicit Deserializer(std::string_view json) : json_buffer(json) {
        initialize_document(static_cast<simdjson::padded_string_view>(json_buffer));
    }

    explicit Deserializer(simdjson::padded_string_view json) {
        initialize_document(json);
    }

    bool valid() const {
        return is_valid;
    }

    error_type error() const {
        return last_error;
    }

    status_t finish() {
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        if(!root_consumed) {
            return mark_invalid();
        }
        if(!document.at_end()) {
            return mark_invalid(simdjson::TRAILING_CONTENT);
        }
        return {};
    }

    result_t<bool> deserialize_none() {
        if(!is_valid) {
            return std::unexpected(last_error);
        }

        // pending_object/pending_array means we're positioned at a container, not null
        if(pending_object || pending_array) {
            return false;
        }

        bool is_none = false;
        simdjson::error_code err = simdjson::SUCCESS;
        if(current_value != nullptr) {
            err = current_value->is_null().get(is_none);
        } else {
            if(root_consumed) {
                return mark_invalid();
            }

            err = document.is_null().get(is_none);
            if(err == simdjson::SUCCESS && is_none) {
                root_consumed = true;
            }
        }

        if(err != simdjson::SUCCESS) {
            return mark_invalid(err);
        }
        return is_none;
    }

    KOTA_ALWAYS_INLINE status_t deserialize_bool(bool& value) {
        return read_value(value, [] KOTA_ALWAYS_INLINE(auto& src) { return src.get_bool(); });
    }

    template <codec::int_like T>
    KOTA_ALWAYS_INLINE status_t deserialize_int(T& value) {
        std::int64_t parsed;
        KOTA_EXPECTED_TRY(
            read_value(parsed, [] KOTA_ALWAYS_INLINE(auto& src) { return src.get_int64(); }));
        if(!std::in_range<T>(parsed)) [[unlikely]] {
            return mark_invalid(error_kind::number_out_of_range);
        }
        value = static_cast<T>(parsed);
        return {};
    }

    template <codec::uint_like T>
    KOTA_ALWAYS_INLINE status_t deserialize_uint(T& value) {
        std::uint64_t parsed;
        KOTA_EXPECTED_TRY(
            read_value(parsed, [] KOTA_ALWAYS_INLINE(auto& src) { return src.get_uint64(); }));
        if(!std::in_range<T>(parsed)) [[unlikely]] {
            return mark_invalid(error_kind::number_out_of_range);
        }
        value = static_cast<T>(parsed);
        return {};
    }

    template <codec::floating_like T>
    KOTA_ALWAYS_INLINE status_t deserialize_float(T& value) {
        double parsed;
        KOTA_EXPECTED_TRY(
            read_value(parsed, [] KOTA_ALWAYS_INLINE(auto& src) { return src.get_double(); }));
        if constexpr(!std::same_as<T, double>) {
            if(std::isfinite(parsed)) {
                const auto v = static_cast<long double>(parsed);
                if(v < static_cast<long double>((std::numeric_limits<T>::lowest)()) ||
                   v > static_cast<long double>((std::numeric_limits<T>::max)())) [[unlikely]] {
                    return mark_invalid(error_kind::number_out_of_range);
                }
            }
        }
        value = static_cast<T>(parsed);
        return {};
    }

    status_t deserialize_char(char& value) {
        std::string_view text;
        KOTA_EXPECTED_TRY(
            read_value(text, [] KOTA_ALWAYS_INLINE(auto& src) { return src.get_string(); }));
        if(text.size() != 1) [[unlikely]] {
            return mark_invalid(error_kind::type_mismatch);
        }
        value = text.front();
        return {};
    }

    KOTA_ALWAYS_INLINE status_t deserialize_str(std::string& value) {
        std::string_view text;
        KOTA_EXPECTED_TRY(
            read_value(text, [] KOTA_ALWAYS_INLINE(auto& src) { return src.get_string(); }));
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
                return mark_invalid(error_kind::number_out_of_range);
            }
            value.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(byte_val)));
        }
        return end_array();
    }

    result_t<simdjson::ondemand::json_type> peek_type() {
        return read_source<simdjson::ondemand::json_type>([](auto& src) { return src.type(); },
                                                          false);
    }

    result_t<simdjson::ondemand::number_type> peek_number_type() {
        return read_source<simdjson::ondemand::number_type>(
            [](auto& src) { return src.get_number_type(); },
            false);
    }

    result_t<meta::type_kind> peek_kind() {
        KOTA_EXPECTED_TRY_V(auto type, peek_type());
        std::optional<simdjson::ondemand::number_type> nt;
        if(type == simdjson::ondemand::json_type::number) {
            KOTA_EXPECTED_TRY_V(auto ntype, peek_number_type());
            nt = ntype;
        }
        return map_to_kind(type, nt);
    }

    result_t<simdjson::padded_string_view> deserialize_raw_json_view() {
        KOTA_EXPECTED_TRY_V(auto raw, read_source<std::string_view>([](auto& src) {
                                return src.raw_json();
                            }));
        return to_padded_subview(raw);
    }

    template <typename... Ts>
    status_t deserialize_variant(std::variant<Ts...>& value) {
        static_assert((std::default_initializable<Ts> && ...),
                      "variant deserialization requires default-constructible alternatives");

        auto json_type = peek_type();
        if(!json_type) {
            return std::unexpected(json_type.error());
        }

        std::optional<std::size_t> best;
        using adapter = detail::simdjson_source_adapter;

        if(*json_type == simdjson::ondemand::json_type::object) {
            KOTA_EXPECTED_TRY_V(auto obj, read_source<simdjson::ondemand::object>([](auto& src) {
                                    return src.get_object();
                                }));
            best =
                codec::select_variant_index<adapter, config_type, Ts...>(adapter::node_type(&obj));
            if(auto r = obj.reset(); r.error() != simdjson::SUCCESS) {
                return mark_invalid(r.error());
            }
            pending_object.emplace(std::move(obj));
        } else if(*json_type == simdjson::ondemand::json_type::array) {
            KOTA_EXPECTED_TRY_V(auto arr, read_source<simdjson::ondemand::array>([](auto& src) {
                                    return src.get_array();
                                }));
            best =
                codec::select_variant_index<adapter, config_type, Ts...>(adapter::node_type(&arr));
            if(auto r = arr.reset(); r.error() != simdjson::SUCCESS) {
                return mark_invalid(r.error());
            }
            pending_array.emplace(std::move(arr));
        } else {
            std::optional<simdjson::ondemand::number_type> number_type = std::nullopt;
            if(*json_type == simdjson::ondemand::json_type::number) {
                KOTA_EXPECTED_TRY_V(auto nt, peek_number_type());
                number_type = nt;
            }
            best = codec::select_variant_index<config_type, Ts...>(
                map_to_kind(*json_type, number_type));
        }

        if(!best) {
            pending_object.reset();
            pending_array.reset();
            return mark_invalid(error_kind::type_mismatch);
        }

        auto result = codec::deserialize_variant_at<error_type>(*this, value, *best);
        pending_object.reset();
        pending_array.reset();
        return result;
    }

    KOTA_ALWAYS_INLINE status_t begin_object() {
        if(pending_object) {
            auto obj = std::move(*pending_object);
            pending_object.reset();
            return begin_object(std::move(obj));
        }
        KOTA_EXPECTED_TRY_V(auto obj, read_source<simdjson::ondemand::object>([](auto& src) {
                                return src.get_object();
                            }));
        return begin_object(std::move(obj));
    }

    KOTA_ALWAYS_INLINE result_t<std::optional<std::string_view>> next_field() {
        if(!is_valid || deser_stack.empty()) {
            return mark_invalid();
        }
        auto& frame = deser_stack.back();

        if(frame.has_pending_value) {
            ++frame.iter;
            frame.has_pending_value = false;
        }

        if(frame.iter == frame.end_iter) {
            current_value = nullptr;
            return std::optional<std::string_view>(std::nullopt);
        }

        simdjson::ondemand::field field{};
        auto field_result = *frame.iter;
        auto field_err = std::move(field_result).get(field);
        if(field_err != simdjson::SUCCESS) {
            return mark_invalid(field_err);
        }

        std::string_view key_sv;
        auto key_err = field.unescaped_key().get(key_sv);
        if(key_err != simdjson::SUCCESS) {
            return mark_invalid(key_err);
        }
        frame.pending_key = key_sv;

        frame.pending_value = std::move(field).value();
        frame.has_pending_value = true;
        current_value = &frame.pending_value;
        return std::optional<std::string_view>(std::string_view(frame.pending_key));
    }

    KOTA_ALWAYS_INLINE status_t skip_field_value() {
        if(!is_valid || deser_stack.empty()) {
            return mark_invalid();
        }
        auto& frame = deser_stack.back();
        if(!frame.has_pending_value) {
            return mark_invalid();
        }
        std::string_view raw{};
        auto err = frame.pending_value.raw_json().get(raw);
        if(err != simdjson::SUCCESS) {
            return mark_invalid(err);
        }
        ++frame.iter;
        frame.has_pending_value = false;
        current_value = nullptr;
        return {};
    }

    KOTA_ALWAYS_INLINE status_t end_object() {
        if(!is_valid || deser_stack.empty()) {
            return mark_invalid();
        }
        auto& frame = deser_stack.back();
        if(frame.has_pending_value) {
            ++frame.iter;
        }
        deser_stack.pop_back();
        current_value = nullptr;
        return {};
    }

    KOTA_ALWAYS_INLINE status_t begin_array() {
        if(pending_array) {
            auto arr = std::move(*pending_array);
            pending_array.reset();
            return begin_array(std::move(arr));
        }
        KOTA_EXPECTED_TRY_V(auto arr, read_source<simdjson::ondemand::array>([](auto& src) {
                                return src.get_array();
                            }));
        return begin_array(std::move(arr));
    }

    KOTA_ALWAYS_INLINE result_t<bool> next_element() {
        if(!is_valid || array_stack.empty()) {
            return mark_invalid();
        }
        auto& frame = array_stack.back();

        if(frame.has_pending_value) {
            ++frame.iter;
            frame.has_pending_value = false;
        }

        if(frame.iter == frame.end_iter) {
            current_value = nullptr;
            return false;
        }

        auto value_result = *frame.iter;
        auto err = std::move(value_result).get(frame.pending_value);
        if(err != simdjson::SUCCESS) {
            return mark_invalid(err);
        }

        frame.has_pending_value = true;
        current_value = &frame.pending_value;
        return true;
    }

    KOTA_ALWAYS_INLINE status_t end_array() {
        if(!is_valid || array_stack.empty()) {
            return mark_invalid();
        }
        auto& frame = array_stack.back();
        if(frame.has_pending_value) {
            ++frame.iter;
        }
        array_stack.pop_back();
        current_value = nullptr;
        return {};
    }

    result_t<std::string> scan_object_field(std::string_view field_name) {
        KOTA_EXPECTED_TRY_V(auto obj, read_source<simdjson::ondemand::object>([](auto& src) {
                                return src.get_object();
                            }));
        current_value = nullptr;
        std::string result;
        bool found = false;
        for(auto field_result: obj) {
            simdjson::ondemand::field field;
            if(std::move(field_result).get(field) != simdjson::SUCCESS)
                break;
            std::string_view key;
            if(field.unescaped_key().get(key) != simdjson::SUCCESS)
                break;
            if(key == field_name) {
                std::string_view val;
                auto err = std::move(field).value().get_string().get(val);
                if(err != simdjson::SUCCESS) {
                    return mark_invalid(err);
                }
                result = std::string(val);
                found = true;
                break;
            }
        }
        if(!found) {
            return std::unexpected(error_type::missing_field(field_name));
        }
        if(auto r = obj.reset(); r.error() != simdjson::SUCCESS) {
            return mark_invalid(r.error());
        }
        pending_object.emplace(std::move(obj));
        return result;
    }

    result_t<std::string> buffer_raw_field_value() {
        if(!is_valid || deser_stack.empty()) {
            return mark_invalid();
        }
        auto& frame = deser_stack.back();
        if(!frame.has_pending_value) {
            return mark_invalid();
        }
        std::string_view raw;
        auto err = frame.pending_value.raw_json().get(raw);
        if(err != simdjson::SUCCESS) {
            return mark_invalid(err);
        }
        std::string result(raw);
        ++frame.iter;
        frame.has_pending_value = false;
        current_value = nullptr;
        return result;
    }

    template <typename T>
    status_t replay_buffered_field(std::string_view raw_json, T& value) {
        Deserializer sub(raw_json);
        KOTA_EXPECTED_TRY(codec::deserialize(sub, value));
        KOTA_EXPECTED_TRY(sub.finish());
        return {};
    }

private:
    template <typename T, typename Fn>
    result_t<T> read_source(Fn&& fn, bool consume = true) {
        if(!is_valid) {
            return std::unexpected(last_error);
        }

        T out{};
        simdjson::error_code err = simdjson::SUCCESS;
        if(current_value != nullptr) {
            err = fn(*current_value).get(out);
        } else {
            if(root_consumed) {
                return mark_invalid();
            }
            err = fn(document).get(out);
            if(err == simdjson::SUCCESS && consume) {
                root_consumed = true;
            }
        }

        if(err != simdjson::SUCCESS) {
            return mark_invalid(err);
        }
        return out;
    }

    template <typename T, typename Fn>
    KOTA_ALWAYS_INLINE status_t read_value(T& out, Fn&& fn) {
        if(!is_valid) [[unlikely]] {
            return std::unexpected(last_error);
        }
        simdjson::error_code err;
        if(current_value != nullptr) {
            err = fn(*current_value).get(out);
        } else {
            if(root_consumed) {
                return mark_invalid();
            }
            err = fn(document).get(out);
            if(err == simdjson::SUCCESS) {
                root_consumed = true;
            }
        }
        if(err != simdjson::SUCCESS) [[unlikely]] {
            return mark_invalid(err);
        }
        return {};
    }

    KOTA_ALWAYS_INLINE status_t begin_object(simdjson::ondemand::object obj) {
        current_value = nullptr;
        deser_frame frame;
        frame.object = std::move(obj);

        auto begin_result = frame.object.begin();
        auto begin_err = std::move(begin_result).get(frame.iter);
        if(begin_err != simdjson::SUCCESS) {
            return mark_invalid(begin_err);
        }

        auto end_result = frame.object.end();
        auto end_err = std::move(end_result).get(frame.end_iter);
        if(end_err != simdjson::SUCCESS) {
            return mark_invalid(end_err);
        }

        deser_stack.push_back(std::move(frame));
        return {};
    }

    KOTA_ALWAYS_INLINE status_t begin_array(simdjson::ondemand::array arr) {
        current_value = nullptr;
        array_frame frame;
        frame.array = std::move(arr);

        auto begin_result = frame.array.begin();
        auto begin_err = std::move(begin_result).get(frame.iter);
        if(begin_err != simdjson::SUCCESS) {
            return mark_invalid(begin_err);
        }

        auto end_result = frame.array.end();
        auto end_err = std::move(end_result).get(frame.end_iter);
        if(end_err != simdjson::SUCCESS) {
            return mark_invalid(end_err);
        }

        array_stack.push_back(std::move(frame));
        return {};
    }

    static meta::type_kind map_to_kind(simdjson::ondemand::json_type json_type,
                                       std::optional<simdjson::ondemand::number_type> number_type) {
        switch(json_type) {
            case simdjson::ondemand::json_type::null: return meta::type_kind::null;
            case simdjson::ondemand::json_type::boolean: return meta::type_kind::boolean;
            case simdjson::ondemand::json_type::number:
                if(number_type.has_value()) {
                    if(*number_type == simdjson::ondemand::number_type::floating_point_number)
                        return meta::type_kind::float64;
                    if(*number_type == simdjson::ondemand::number_type::unsigned_integer)
                        return meta::type_kind::uint64;
                }
                return meta::type_kind::int64;
            case simdjson::ondemand::json_type::string: return meta::type_kind::string;
            case simdjson::ondemand::json_type::array: return meta::type_kind::array;
            case simdjson::ondemand::json_type::object: return meta::type_kind::structure;
            default: return meta::type_kind::any;
        }
    }

    result_t<simdjson::padded_string_view> to_padded_subview(std::string_view raw) {
        const char* base = input_view.data();
        if(base == nullptr) {
            return mark_invalid();
        }

        const auto raw_addr = reinterpret_cast<std::uintptr_t>(raw.data());
        const auto base_addr = reinterpret_cast<std::uintptr_t>(base);
        const std::size_t total_capacity = input_view.capacity();

        if(raw_addr < base_addr) {
            return mark_invalid();
        }

        const std::size_t offset = static_cast<std::size_t>(raw_addr - base_addr);
        if(offset > total_capacity || raw.size() > (total_capacity - offset)) {
            return mark_invalid();
        }

        const std::size_t remaining_capacity = total_capacity - offset;
        return simdjson::padded_string_view(raw, remaining_capacity);
    }

    void initialize_document(simdjson::padded_string_view json) {
        input_view = json;

        auto document_result = parser.iterate(json);
        auto err = std::move(document_result).get(document);
        if(err != simdjson::SUCCESS) {
            (void)mark_invalid(err);
        }
    }

    std::unexpected<error_type> mark_invalid(error_kind err = error_kind::invalid_state) {
        is_valid = false;
        error_type error(err);
        if(auto loc = compute_location()) {
            error.set_location(*loc);
        }
        last_error = error;
        return std::unexpected(last_error);
    }

    std::unexpected<error_type> mark_invalid(simdjson::error_code err) {
        return mark_invalid(json::make_error(err));
    }

    std::optional<codec::source_location> compute_location() {
        auto loc_result = document.current_location();
        const char* loc = nullptr;
        if(std::move(loc_result).get(loc) != simdjson::SUCCESS || loc == nullptr) {
            return std::nullopt;
        }

        const char* base = input_view.data();
        if(base == nullptr || loc < base) {
            return std::nullopt;
        }

        std::size_t offset = static_cast<std::size_t>(loc - base);
        std::size_t total = input_view.size();
        if(offset > total) {
            offset = total;
        }

        std::size_t line = 1;
        std::size_t col = 1;
        for(std::size_t i = 0; i < offset; ++i) {
            if(base[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }

        return codec::source_location{line, col, offset};
    }

    struct deser_frame {
        simdjson::ondemand::object object{};
        simdjson::ondemand::object_iterator iter{};
        simdjson::ondemand::object_iterator end_iter{};
        simdjson::ondemand::value pending_value{};
        std::string_view pending_key;
        bool has_pending_value = false;
    };

    struct array_frame {
        simdjson::ondemand::array array{};
        simdjson::ondemand::array_iterator iter{};
        simdjson::ondemand::array_iterator end_iter{};
        simdjson::ondemand::value pending_value{};
        bool has_pending_value = false;
    };

    bool is_valid = true;
    bool root_consumed = false;
    error_type last_error;
    simdjson::ondemand::value* current_value = nullptr;

    std::optional<simdjson::ondemand::object> pending_object;
    std::optional<simdjson::ondemand::array> pending_array;

    kota::small_vector<deser_frame, 4> deser_stack;
    kota::small_vector<array_frame, 4> array_stack;

    simdjson::ondemand::parser parser;
    simdjson::padded_string json_buffer;
    simdjson::padded_string_view input_view{};
    simdjson::ondemand::document document;
};

template <typename Config = config::default_config, typename T>
auto from_json(std::string_view json, T& value) -> std::expected<void, error> {
    Deserializer<Config> deserializer(json);
    if(!deserializer.valid()) {
        return std::unexpected(deserializer.error());
    }

    KOTA_EXPECTED_TRY(codec::deserialize(deserializer, value));

    return deserializer.finish();
}

template <typename Config = config::default_config, typename T>
auto from_json(simdjson::padded_string_view json, T& value) -> std::expected<void, error> {
    Deserializer<Config> deserializer(json);
    if(!deserializer.valid()) {
        return std::unexpected(deserializer.error());
    }

    KOTA_EXPECTED_TRY(codec::deserialize(deserializer, value));

    return deserializer.finish();
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_json(std::string_view json) -> std::expected<T, error> {
    T value{};
    KOTA_EXPECTED_TRY(from_json<Config>(json, value));
    return value;
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_json(simdjson::padded_string_view json) -> std::expected<T, error> {
    T value{};
    KOTA_EXPECTED_TRY(from_json<Config>(json, value));
    return value;
}

static_assert(codec::deserializer_like<Deserializer<>>);

}  // namespace kota::codec::json
