#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "eventide/common/expected_try.h"
#include "eventide/serde/content/deserializer.h"
#include "eventide/serde/json/error.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/serde.h"
#include "eventide/serde/serde/utils/narrow.h"

namespace eventide::serde::json {

template <typename Config = config::default_config>
class Deserializer {
public:
    using config_type = Config;
    using error_type = json::error;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    class DeserializeArray {
    public:
        result_t<bool> has_next() {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.error());
            }
            if(has_pending_value) {
                return true;
            }
            if(iter == end_iter) {
                return false;
            }

            auto value_result = *iter;
            auto err = std::move(value_result).get(pending_value);
            if(err != simdjson::SUCCESS) {
                return deserializer.mark_invalid(err);
            }

            has_pending_value = true;
            return true;
        }

        template <typename T>
        status_t deserialize_element(T& value) {
            return consume_next(
                [&](auto& v) { return deserializer.deserialize_from_value(v, value); });
        }

        status_t skip_element() {
            return consume_next([&](auto& v) { return deserializer.skip_value(v); });
        }

        status_t end() {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.error());
            }

            ETD_EXPECTED_TRY_V(auto has_next_result, has_next());

            if(is_strict_length) {
                if(consumed_count != expected_length || has_next_result) {
                    return deserializer.mark_invalid();
                }
                return {};
            }

            while(has_next_result) {
                ETD_EXPECTED_TRY(skip_element());

                ETD_EXPECTED_TRY_V(auto next, has_next());
                has_next_result = next;
            }

            return {};
        }

    private:
        friend class Deserializer;

        template <typename Action>
        status_t consume_next(Action&& action) {
            ETD_EXPECTED_TRY_V(auto has_next_result, has_next());
            if(!has_next_result) {
                return deserializer.mark_invalid();
            }

            ETD_EXPECTED_TRY(std::forward<Action>(action)(pending_value));

            ++iter;
            has_pending_value = false;
            ++consumed_count;
            return {};
        }

        DeserializeArray(Deserializer& deserializer,
                         simdjson::ondemand::array&& array,
                         std::size_t expected_length,
                         bool is_strict_length) :
            deserializer(deserializer), array(std::move(array)), expected_length(expected_length),
            is_strict_length(is_strict_length) {
            auto begin_result = this->array.begin();
            auto begin_err = std::move(begin_result).get(iter);
            if(begin_err != simdjson::SUCCESS) {
                (void)deserializer.mark_invalid(begin_err);
                return;
            }

            auto end_result = this->array.end();
            auto end_err = std::move(end_result).get(end_iter);
            if(end_err != simdjson::SUCCESS) {
                (void)deserializer.mark_invalid(end_err);
                return;
            }
        }

        Deserializer& deserializer;
        simdjson::ondemand::array array{};
        simdjson::ondemand::array_iterator iter{};
        simdjson::ondemand::array_iterator end_iter{};
        simdjson::ondemand::value pending_value{};
        std::size_t expected_length = 0;
        std::size_t consumed_count = 0;
        bool is_strict_length = false;
        bool has_pending_value = false;
    };

    class DeserializeObject {
    public:
        result_t<std::optional<std::string_view>> next_key() {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.error());
            }
            if(has_pending_value) {
                return deserializer.mark_invalid();
            }
            if(iter == end_iter) {
                return std::optional<std::string_view>{};
            }

            simdjson::ondemand::field field{};
            auto field_result = *iter;
            auto field_err = std::move(field_result).get(field);
            if(field_err != simdjson::SUCCESS) {
                return deserializer.mark_invalid(field_err);
            }

            auto key_err = field.unescaped_key(pending_key);
            if(key_err != simdjson::SUCCESS) {
                return deserializer.mark_invalid(key_err);
            }
            pending_value = std::move(field).value();
            has_pending_value = true;
            return std::optional<std::string_view>{std::string_view(pending_key)};
        }

        status_t invalid_key(std::string_view /*key_name*/) {
            return deserializer.mark_invalid(simdjson::INCORRECT_TYPE);
        }

        template <typename T>
        status_t deserialize_value(T& value) {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.error());
            }
            if(!has_pending_value) {
                return deserializer.mark_invalid();
            }

            ETD_EXPECTED_TRY(deserializer.deserialize_from_value(pending_value, value));

            ++iter;
            has_pending_value = false;
            return {};
        }

        status_t skip_value() {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.error());
            }
            if(!has_pending_value) {
                return deserializer.mark_invalid();
            }

            ETD_EXPECTED_TRY(deserializer.skip_value(pending_value));

            ++iter;
            has_pending_value = false;
            return {};
        }

        status_t end() {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.error());
            }

            if(has_pending_value) {
                ETD_EXPECTED_TRY(skip_value());
            }

            while(iter != end_iter) {
                simdjson::ondemand::field field{};
                auto field_result = *iter;
                auto field_err = std::move(field_result).get(field);
                if(field_err != simdjson::SUCCESS) {
                    return deserializer.mark_invalid(field_err);
                }

                auto value = std::move(field).value();
                ETD_EXPECTED_TRY(deserializer.skip_value(value));

                ++iter;
            }

            return {};
        }

    private:
        friend class Deserializer;

        DeserializeObject(Deserializer& deserializer, simdjson::ondemand::object&& object) :
            deserializer(deserializer), object(std::move(object)) {
            auto begin_result = this->object.begin();
            auto begin_err = std::move(begin_result).get(iter);
            if(begin_err != simdjson::SUCCESS) {
                (void)deserializer.mark_invalid(begin_err);
                return;
            }

            auto end_result = this->object.end();
            auto end_err = std::move(end_result).get(end_iter);
            if(end_err != simdjson::SUCCESS) {
                (void)deserializer.mark_invalid(end_err);
                return;
            }
        }

        Deserializer& deserializer;
        simdjson::ondemand::object object{};
        simdjson::ondemand::object_iterator iter{};
        simdjson::ondemand::object_iterator end_iter{};
        simdjson::ondemand::value pending_value{};
        std::string pending_key;
        bool has_pending_value = false;
    };

    using DeserializeSeq = DeserializeArray;
    using DeserializeTuple = DeserializeArray;
    using DeserializeMap = DeserializeObject;
    using DeserializeStruct = DeserializeObject;

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

        auto json_type = peek_json_type();
        if(!json_type) {
            return std::unexpected(json_type.error());
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

        if(*json_type == simdjson::ondemand::json_type::object) {
            /// TODO(eventide/serde): Optimize object-variant dispatch with a two-pass strategy.
            /// Pass 1: walk fields once to collect a lightweight structural signature
            /// (field names + coarse JSON value categories).
            /// Use that signature to select a likely target alternative (especially among
            /// many struct-like alternatives) without trying each candidate parser.
            /// Pass 2: deserialize exactly once into the selected alternative.
            /// Target complexity: at most two object traversals regardless of the number
            /// of struct alternatives in std::variant.
            bool matched = false;
            bool considered = false;
            error_type obj_last_error = error_type::type_mismatch;

            auto try_alternative = [&](auto type_tag) {
                if(matched) {
                    return;
                }

                using alt_t = typename decltype(type_tag)::type;
                if(!variant_candidate_matches<alt_t>(*json_type, number_type)) {
                    return;
                }
                considered = true;

                auto candidate_status = deserialize_variant_candidate<alt_t>(*raw, value);
                if(candidate_status) {
                    matched = true;
                } else {
                    obj_last_error = candidate_status.error();
                }
            };

            (try_alternative(std::type_identity<Ts>{}), ...);

            if(!matched) {
                return mark_invalid(considered ? obj_last_error.kind : error_kind::type_mismatch);
            }
            return {};
        }

        bool matched = false;
        bool considered = false;
        error_type variant_last_error = error_type::type_mismatch;
        auto try_alternative = [&](auto type_tag) {
            if(matched) {
                return;
            }

            using alt_t = typename decltype(type_tag)::type;
            if(!variant_candidate_matches<alt_t>(*json_type, number_type)) {
                return;
            }

            considered = true;
            auto candidate_status = deserialize_variant_candidate<alt_t>(*raw, value);
            if(candidate_status) {
                matched = true;
            } else {
                variant_last_error = candidate_status.error();
            }
        };

        (try_alternative(std::type_identity<Ts>{}), ...);

        if(!considered) {
            return mark_invalid(error_kind::type_mismatch);
        }
        if(!matched) {
            return mark_invalid(variant_last_error.kind);
        }
        return {};
    }

    status_t deserialize_bool(bool& value) {
        return read_scalar(
            value,
            [](simdjson::ondemand::document& doc) { return doc.get_bool(); },
            [](simdjson::ondemand::value& value) { return value.get_bool(); });
    }

    template <serde::int_like T>
    status_t deserialize_int(T& value) {
        std::int64_t parsed = 0;
        auto status = read_scalar(
            parsed,
            [](simdjson::ondemand::document& doc) { return doc.get_int64(); },
            [](simdjson::ondemand::value& value) { return value.get_int64(); });
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
        auto status = read_scalar(
            parsed,
            [](simdjson::ondemand::document& doc) { return doc.get_uint64(); },
            [](simdjson::ondemand::value& value) { return value.get_uint64(); });
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
        auto status = read_scalar(
            parsed,
            [](simdjson::ondemand::document& doc) { return doc.get_double(); },
            [](simdjson::ondemand::value& value) { return value.get_double(); });
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
        auto status = read_scalar(
            text,
            [](simdjson::ondemand::document& doc) { return doc.get_string(); },
            [](simdjson::ondemand::value& value) { return value.get_string(); });
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
        return serde::detail::deserialize_bytes_from_seq(*this, value);
    }

    result_t<DeserializeSeq> deserialize_seq(std::optional<std::size_t> len) {
        ETD_EXPECTED_TRY_V(auto array, open_array());

        DeserializeSeq seq(*this, std::move(array), len.value_or(0), false);
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        return seq;
    }

    result_t<DeserializeTuple> deserialize_tuple(std::size_t len) {
        ETD_EXPECTED_TRY_V(auto array, open_array());

        DeserializeTuple tuple(*this, std::move(array), len, true);
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        return tuple;
    }

    result_t<DeserializeMap> deserialize_map(std::optional<std::size_t> /*len*/) {
        ETD_EXPECTED_TRY_V(auto object, open_object());

        DeserializeMap map(*this, std::move(object));
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        return map;
    }

    result_t<DeserializeStruct> deserialize_struct(std::string_view /*name*/, std::size_t /*len*/) {
        ETD_EXPECTED_TRY_V(auto object, open_object());

        DeserializeStruct s(*this, std::move(object));
        if(!is_valid) {
            return std::unexpected(last_error);
        }
        return s;
    }

    result_t<content::Value> capture_dom_value() {
        ETD_EXPECTED_TRY_V(auto raw, consume_raw_json_view());
        auto parsed = content::Value::parse(std::string_view(raw.data(), raw.size()));
        if(!parsed) {
            return std::unexpected(json::make_read_error(parsed.error()));
        }
        return std::move(*parsed);
    }

    result_t<simdjson::padded_string_view> deserialize_raw_json_view() {
        return consume_raw_json_view();
    }

    result_t<simdjson::ondemand::json_type> peek_type() {
        return read_source<simdjson::ondemand::json_type>([](auto& doc) { return doc.type(); },
                                                          [](auto& val) { return val.type(); },
                                                          false);
    }

private:
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
        return serde::deserialize(*this, out);
    }

    result_t<simdjson::ondemand::json_type> peek_json_type() {
        return peek_type();
    }

    result_t<simdjson::ondemand::number_type> peek_number_type() {
        return read_source<simdjson::ondemand::number_type>(
            [](auto& doc) { return doc.get_number_type(); },
            [](auto& val) { return val.get_number_type(); },
            false);
    }

    static serde::type_hint
        map_to_type_hint(simdjson::ondemand::json_type json_type,
                         std::optional<simdjson::ondemand::number_type> number_type) {
        switch(json_type) {
            case simdjson::ondemand::json_type::null: return serde::type_hint::null_like;
            case simdjson::ondemand::json_type::boolean: return serde::type_hint::boolean;
            case simdjson::ondemand::json_type::number: {
                if(!number_type.has_value()) {
                    return serde::type_hint::integer | serde::type_hint::floating;
                }
                if(*number_type == simdjson::ondemand::number_type::signed_integer ||
                   *number_type == simdjson::ondemand::number_type::unsigned_integer) {
                    return serde::type_hint::integer;
                }
                return serde::type_hint::floating;
            }
            case simdjson::ondemand::json_type::string: return serde::type_hint::string;
            case simdjson::ondemand::json_type::array: return serde::type_hint::array;
            case simdjson::ondemand::json_type::object: return serde::type_hint::object;
            default: return serde::type_hint::any;
        }
    }

    template <typename T>
    constexpr static bool
        variant_candidate_matches(simdjson::ondemand::json_type json_type,
                                  std::optional<simdjson::ondemand::number_type> number_type) {
        return serde::has_any(serde::expected_type_hints<T>(),
                              map_to_type_hint(json_type, number_type));
    }

    template <typename Alt, typename... Ts>
    static auto deserialize_variant_candidate(simdjson::padded_string_view raw,
                                              std::variant<Ts...>& value) -> status_t {
        Alt candidate{};
        Deserializer probe(raw);
        if(!probe.valid()) {
            return std::unexpected(probe.error());
        }

        ETD_EXPECTED_TRY(serde::deserialize(probe, candidate));
        ETD_EXPECTED_TRY(probe.finish());

        value = std::move(candidate);
        return {};
    }

    result_t<simdjson::padded_string_view> consume_raw_json_view() {
        ETD_EXPECTED_TRY_V(auto raw,
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

    result_t<simdjson::ondemand::array> open_array() {
        return read_source<simdjson::ondemand::array>([](auto& doc) { return doc.get_array(); },
                                                      [](auto& val) { return val.get_array(); });
    }

    result_t<simdjson::ondemand::object> open_object() {
        return read_source<simdjson::ondemand::object>([](auto& doc) { return doc.get_object(); },
                                                       [](auto& val) { return val.get_object(); });
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

    std::optional<serde::source_location> compute_location() {
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

        return serde::source_location{line, col, offset};
    }

private:
    bool is_valid = true;
    bool root_consumed = false;
    error_type last_error;
    simdjson::ondemand::value* current_value = nullptr;

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

    ETD_EXPECTED_TRY(serde::deserialize(deserializer, value));

    return deserializer.finish();
}

template <typename Config = config::default_config, typename T>
auto from_json(simdjson::padded_string_view json, T& value) -> std::expected<void, error> {
    Deserializer<Config> deserializer(json);
    if(!deserializer.valid()) {
        return std::unexpected(deserializer.error());
    }

    ETD_EXPECTED_TRY(serde::deserialize(deserializer, value));

    return deserializer.finish();
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_json(std::string_view json) -> std::expected<T, error> {
    T value{};
    ETD_EXPECTED_TRY(from_json<Config>(json, value));
    return value;
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_json(simdjson::padded_string_view json) -> std::expected<T, error> {
    T value{};
    ETD_EXPECTED_TRY(from_json<Config>(json, value));
    return value;
}

static_assert(serde::deserializer_like<Deserializer<>>);

}  // namespace eventide::serde::json
