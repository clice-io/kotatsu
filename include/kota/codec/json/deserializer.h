#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/codec/content/deserializer.h"
#include "kota/codec/content/document.h"
#include "kota/codec/detail/backend.h"
#include "kota/codec/detail/codec.h"
#include "kota/codec/detail/config.h"
#include "kota/codec/detail/narrow.h"
#include "kota/codec/json/error.h"

namespace kota::codec::json {

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

        if(has_preloaded_object) {
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

    template <typename... Ts>
    status_t deserialize_variant(std::variant<Ts...>& value) {
        static_assert((std::default_initializable<Ts> && ...),
                      "variant deserialization requires default-constructible alternatives");

        auto json_type = peek_type();
        if(!json_type) {
            return std::unexpected(json_type.error());
        }

        constexpr std::size_t N = sizeof...(Ts);

        if(*json_type == simdjson::ondemand::json_type::object) {
            auto obj_result =
                read_source<simdjson::ondemand::object>([](auto& doc) { return doc.get_object(); },
                                                        [](auto& val) { return val.get_object(); });
            if(!obj_result) {
                return std::unexpected(obj_result.error());
            }

            auto obj = std::move(*obj_result);

            std::size_t best = codec::decide_variant_index<config_type, Ts...>(
                meta::type_kind::structure,
                [&](auto&& feed) {
                    for(auto field_result: obj) {
                        simdjson::ondemand::field field;
                        if(std::move(field_result).get(field) != simdjson::SUCCESS) {
                            break;
                        }
                        std::string_view key;
                        if(field.unescaped_key().get(key) != simdjson::SUCCESS) {
                            break;
                        }
                        if(feed(key)) {
                            break;
                        }
                    }
                });

            if(best >= N) {
                return mark_invalid(error_kind::type_mismatch);
            }

            if(obj.reset().error()) {
                return mark_invalid(error_kind::invalid_state);
            }
            preloaded_object = std::move(obj);
            has_preloaded_object = true;

            status_t result = std::unexpected(error_type::type_mismatch);
            std::size_t idx = 0;
            auto try_alt = [&](auto type_tag) {
                if(idx++ != best) {
                    return;
                }
                using alt_t = typename decltype(type_tag)::type;
                alt_t candidate{};
                auto status = codec::deserialize(*this, candidate);
                if(status) {
                    value = std::move(candidate);
                    result = {};
                } else {
                    result = std::unexpected(status.error());
                }
            };
            (try_alt(std::type_identity<Ts>{}), ...);

            has_preloaded_object = false;
            return result;
        }

        std::optional<simdjson::ondemand::number_type> number_type = std::nullopt;
        if(*json_type == simdjson::ondemand::json_type::number) {
            auto current_number_type = peek_number_type();
            if(!current_number_type) {
                return std::unexpected(current_number_type.error());
            }
            number_type = *current_number_type;
        }

        auto raw = consume_raw_json_view();
        if(!raw) {
            return std::unexpected(raw.error());
        }

        auto source_kind = map_to_kind(*json_type, number_type);

        auto result = codec::try_variant_dispatch<Deserializer>(*raw,
                                                                source_kind,
                                                                value,
                                                                error_type::type_mismatch);
        if(!result) {
            return mark_invalid(result.error().kind);
        }
        return {};
    }

    status_t deserialize_bool(bool& value) {
        return read_scalar(
            value,
            [](simdjson::ondemand::document& doc) { return doc.get_bool(); },
            [](simdjson::ondemand::value& value) { return value.get_bool(); });
    }

    template <codec::int_like T>
    status_t deserialize_int(T& value) {
        std::int64_t parsed = 0;
        auto status = read_scalar(
            parsed,
            [](simdjson::ondemand::document& doc) { return doc.get_int64(); },
            [](simdjson::ondemand::value& value) { return value.get_int64(); });
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
        auto status = read_scalar(
            parsed,
            [](simdjson::ondemand::document& doc) { return doc.get_uint64(); },
            [](simdjson::ondemand::value& value) { return value.get_uint64(); });
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
        auto status = read_scalar(
            parsed,
            [](simdjson::ondemand::document& doc) { return doc.get_double(); },
            [](simdjson::ondemand::value& value) { return value.get_double(); });
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
        auto status = read_scalar(
            text,
            [](simdjson::ondemand::document& doc) { return doc.get_string(); },
            [](simdjson::ondemand::value& value) { return value.get_string(); });
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
        auto status = read_scalar(
            text,
            [](simdjson::ondemand::document& doc) { return doc.get_string(); },
            [](simdjson::ondemand::value& value) { return value.get_string(); });
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
                return mark_invalid(error_kind::number_out_of_range);
            }
            value.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(byte_val)));
        }
        return end_array();
    }

    result_t<content::Value> capture_dom_value() {
        if(!is_valid) {
            return std::unexpected(last_error);
        }

        content::Value out;
        if(current_value != nullptr) {
            KOTA_EXPECTED_TRY(build_dom_from_value(*current_value, out));
        } else {
            if(root_consumed) {
                return mark_invalid();
            }
            simdjson::ondemand::value root_value;
            auto err = document.get_value().get(root_value);
            if(err != simdjson::SUCCESS) {
                return mark_invalid(err);
            }
            root_consumed = true;
            KOTA_EXPECTED_TRY(build_dom_from_value(root_value, out));
        }
        return out;
    }

    result_t<simdjson::padded_string_view> deserialize_raw_json_view() {
        return consume_raw_json_view();
    }

    result_t<simdjson::ondemand::json_type> peek_type() {
        return read_source<simdjson::ondemand::json_type>([](auto& doc) { return doc.type(); },
                                                          [](auto& val) { return val.type(); },
                                                          false);
    }

    status_t begin_object() {
        simdjson::ondemand::object obj;
        if(has_preloaded_object) {
            obj = std::move(preloaded_object);
            has_preloaded_object = false;
        } else {
            auto obj_result =
                read_source<simdjson::ondemand::object>([](auto& doc) { return doc.get_object(); },
                                                        [](auto& val) { return val.get_object(); });
            if(!obj_result) {
                return std::unexpected(obj_result.error());
            }
            obj = std::move(*obj_result);
        }
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

    result_t<std::optional<std::string_view>> next_field() {
        if(!is_valid || deser_stack.empty()) {
            return mark_invalid();
        }
        auto& frame = deser_stack.back();

        // Advance past the previous field
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

        auto key_err = field.unescaped_key(frame.pending_key);
        if(key_err != simdjson::SUCCESS) {
            return mark_invalid(key_err);
        }

        frame.pending_value = std::move(field).value();
        frame.has_pending_value = true;
        current_value = &frame.pending_value;
        return std::optional<std::string_view>(std::string_view(frame.pending_key));
    }

    status_t skip_field_value() {
        if(!is_valid || deser_stack.empty()) {
            return mark_invalid();
        }
        auto& frame = deser_stack.back();
        if(!frame.has_pending_value) {
            return mark_invalid();
        }
        KOTA_EXPECTED_TRY(skip_value(frame.pending_value));
        ++frame.iter;
        frame.has_pending_value = false;
        current_value = nullptr;
        return {};
    }

    status_t end_object() {
        if(!is_valid || deser_stack.empty()) {
            return mark_invalid();
        }
        auto& frame = deser_stack.back();
        // Advance past the last consumed field if needed
        if(frame.has_pending_value) {
            ++frame.iter;
        }
        deser_stack.pop_back();
        current_value = nullptr;
        return {};
    }

    status_t begin_array() {
        KOTA_EXPECTED_TRY_V(
            auto array,
            read_source<simdjson::ondemand::array>([](auto& doc) { return doc.get_array(); },
                                                   [](auto& val) { return val.get_array(); }));
        current_value = nullptr;

        array_frame frame;
        frame.array = std::move(array);

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

    result_t<bool> next_element() {
        if(!is_valid || array_stack.empty()) {
            return mark_invalid();
        }
        auto& frame = array_stack.back();

        // Advance past previous element
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

    status_t end_array() {
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

private:
    status_t build_dom_from_value(simdjson::ondemand::value& value, content::Value& out) {
        simdjson::ondemand::json_type type;
        auto err = value.type().get(type);
        if(err != simdjson::SUCCESS) {
            return mark_invalid(err);
        }

        switch(type) {
            case simdjson::ondemand::json_type::null: {
                out = content::Value(nullptr);
                return {};
            }
            case simdjson::ondemand::json_type::boolean: {
                bool b = false;
                err = value.get_bool().get(b);
                if(err != simdjson::SUCCESS) {
                    return mark_invalid(err);
                }
                out = content::Value(b);
                return {};
            }
            case simdjson::ondemand::json_type::number: {
                simdjson::ondemand::number_type nt{};
                err = value.get_number_type().get(nt);
                if(err != simdjson::SUCCESS) {
                    return mark_invalid(err);
                }
                if(nt == simdjson::ondemand::number_type::signed_integer) {
                    std::int64_t i = 0;
                    err = value.get_int64().get(i);
                    if(err != simdjson::SUCCESS) {
                        return mark_invalid(err);
                    }
                    out = content::Value(i);
                } else if(nt == simdjson::ondemand::number_type::unsigned_integer) {
                    std::uint64_t u = 0;
                    err = value.get_uint64().get(u);
                    if(err != simdjson::SUCCESS) {
                        return mark_invalid(err);
                    }
                    out = content::Value(u);
                } else {
                    double d = 0.0;
                    err = value.get_double().get(d);
                    if(err != simdjson::SUCCESS) {
                        return mark_invalid(err);
                    }
                    out = content::Value(d);
                }
                return {};
            }
            case simdjson::ondemand::json_type::string: {
                std::string_view s;
                err = value.get_string().get(s);
                if(err != simdjson::SUCCESS) {
                    return mark_invalid(err);
                }
                out = content::Value(std::string(s));
                return {};
            }
            case simdjson::ondemand::json_type::array: {
                simdjson::ondemand::array arr;
                err = value.get_array().get(arr);
                if(err != simdjson::SUCCESS) {
                    return mark_invalid(err);
                }
                content::Array out_arr;
                for(auto item: arr) {
                    simdjson::ondemand::value child_v;
                    auto item_err = std::move(item).get(child_v);
                    if(item_err != simdjson::SUCCESS) {
                        return mark_invalid(item_err);
                    }
                    content::Value child;
                    KOTA_EXPECTED_TRY(build_dom_from_value(child_v, child));
                    out_arr.push_back(std::move(child));
                }
                out = content::Value(std::move(out_arr));
                return {};
            }
            case simdjson::ondemand::json_type::object: {
                simdjson::ondemand::object obj;
                err = value.get_object().get(obj);
                if(err != simdjson::SUCCESS) {
                    return mark_invalid(err);
                }
                content::Object out_obj;
                for(auto field_result: obj) {
                    simdjson::ondemand::field field;
                    auto ferr = std::move(field_result).get(field);
                    if(ferr != simdjson::SUCCESS) {
                        return mark_invalid(ferr);
                    }
                    std::string key;
                    auto kerr = field.unescaped_key(key);
                    if(kerr != simdjson::SUCCESS) {
                        return mark_invalid(kerr);
                    }
                    auto child_v = std::move(field).value();
                    content::Value child;
                    KOTA_EXPECTED_TRY(build_dom_from_value(child_v, child));
                    out_obj.insert(std::move(key), std::move(child));
                }
                out = content::Value(std::move(out_obj));
                return {};
            }
            default: break;
        }
        return mark_invalid();
    }

    /// Unified root-vs-value dispatch. Calls `doc_fn(document)` or `val_fn(*current_value)`,
    /// each returning a simdjson result whose `.get(T&)` populates the output.
    /// When `consume` is true, marks root as consumed on success.
    template <typename T, typename DocFn, typename ValFn>
    result_t<T> read_source(DocFn&& doc_fn, ValFn&& val_fn, bool consume = true) {
        if(!is_valid) {
            return std::unexpected(last_error);
        }

        T out{};
        simdjson::error_code err = simdjson::SUCCESS;
        if(current_value != nullptr) {
            err = std::forward<ValFn>(val_fn)(*current_value).get(out);
        } else {
            if(root_consumed) {
                return mark_invalid();
            }
            err = std::forward<DocFn>(doc_fn)(document).get(out);
            if(err == simdjson::SUCCESS && consume) {
                root_consumed = true;
            }
        }

        if(err != simdjson::SUCCESS) {
            return mark_invalid(err);
        }
        return out;
    }

    template <typename T, typename DocFn, typename ValFn>
    status_t read_scalar(T& out, DocFn&& doc_fn, ValFn&& val_fn) {
        auto result = read_source<T>(std::forward<DocFn>(doc_fn), std::forward<ValFn>(val_fn));
        if(!result) {
            return std::unexpected(result.error());
        }
        out = std::move(*result);
        return {};
    }

    status_t skip_value(simdjson::ondemand::value& value) {
        std::string_view raw{};
        auto err = value.raw_json().get(raw);
        if(err != simdjson::SUCCESS) {
            return mark_invalid(err);
        }
        return {};
    }

    template <typename T>
    status_t deserialize_from_value(simdjson::ondemand::value& value, T& out) {
        struct ValueScope {
            explicit ValueScope(Deserializer& deserializer, simdjson::ondemand::value& value) :
                deserializer(deserializer), previous(deserializer.current_value) {
                deserializer.current_value = &value;
            }

            ~ValueScope() {
                deserializer.current_value = previous;
            }

            Deserializer& deserializer;
            simdjson::ondemand::value* previous;
        };

        ValueScope scope(*this, value);
        return codec::deserialize(*this, out);
    }

    result_t<simdjson::ondemand::number_type> peek_number_type() {
        return read_source<simdjson::ondemand::number_type>(
            [](auto& doc) { return doc.get_number_type(); },
            [](auto& val) { return val.get_number_type(); },
            false);
    }

    static meta::type_kind map_to_kind(simdjson::ondemand::json_type json_type,
                                       std::optional<simdjson::ondemand::number_type> number_type) {
        switch(json_type) {
            case simdjson::ondemand::json_type::null: return meta::type_kind::null;
            case simdjson::ondemand::json_type::boolean: return meta::type_kind::boolean;
            case simdjson::ondemand::json_type::number: {
                if(number_type.has_value() &&
                   *number_type == simdjson::ondemand::number_type::floating_point_number) {
                    return meta::type_kind::float64;
                }
                return meta::type_kind::int64;
            }
            case simdjson::ondemand::json_type::string: return meta::type_kind::string;
            case simdjson::ondemand::json_type::array: return meta::type_kind::array;
            case simdjson::ondemand::json_type::object: return meta::type_kind::structure;
            default: return meta::type_kind::any;
        }
    }

    template <typename Alt, typename... Ts>
    static auto deserialize_variant_candidate(simdjson::padded_string_view raw,
                                              std::variant<Ts...>& value) -> status_t {
        Alt candidate{};
        Deserializer probe(raw);
        if(!probe.valid()) {
            return std::unexpected(probe.error());
        }

        KOTA_EXPECTED_TRY(codec::deserialize(probe, candidate));
        KOTA_EXPECTED_TRY(probe.finish());

        value = std::move(candidate);
        return {};
    }

    result_t<simdjson::padded_string_view> consume_raw_json_view() {
        KOTA_EXPECTED_TRY_V(
            auto raw,
            read_source<std::string_view>([](auto& doc) { return doc.raw_json(); },
                                          [](auto& val) { return val.raw_json(); }));
        return to_padded_subview(raw);
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

        // Compute line and column by scanning input
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

private:
    bool is_valid = true;
    bool root_consumed = false;
    bool has_preloaded_object = false;
    error_type last_error;
    simdjson::ondemand::value* current_value = nullptr;
    simdjson::ondemand::object preloaded_object{};

    struct deser_frame {
        simdjson::ondemand::object object{};
        simdjson::ondemand::object_iterator iter{};
        simdjson::ondemand::object_iterator end_iter{};
        simdjson::ondemand::value pending_value{};
        std::string pending_key;
        bool has_pending_value = false;
    };

    struct array_frame {
        simdjson::ondemand::array array{};
        simdjson::ondemand::array_iterator iter{};
        simdjson::ondemand::array_iterator end_iter{};
        simdjson::ondemand::value pending_value{};
        bool has_pending_value = false;
    };

    std::vector<deser_frame> deser_stack;
    std::vector<array_frame> array_stack;

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
