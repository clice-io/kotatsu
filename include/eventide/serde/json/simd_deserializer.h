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

#include "eventide/serde/detail/narrow.h"
#include "eventide/serde/json/error.h"
#include "eventide/serde/serde.h"
#include "eventide/serde/variant.h"

namespace eventide::serde::json::simd {

class Deserializer {
public:
    using error_type = json::error_kind;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    class DeserializeArray {
    public:
        result_t<bool> has_next() {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.current_error());
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
                deserializer.mark_invalid(err);
                return std::unexpected(deserializer.current_error());
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
                return std::unexpected(deserializer.current_error());
            }

            auto has_next_result = has_next();
            if(!has_next_result) {
                return std::unexpected(has_next_result.error());
            }

            if(is_strict_length) {
                if(consumed_count != expected_length || *has_next_result) {
                    deserializer.mark_invalid();
                    return std::unexpected(deserializer.current_error());
                }
                return {};
            }

            while(*has_next_result) {
                auto skipped = skip_element();
                if(!skipped) {
                    return std::unexpected(skipped.error());
                }

                has_next_result = has_next();
                if(!has_next_result) {
                    return std::unexpected(has_next_result.error());
                }
            }

            return {};
        }

    private:
        friend class Deserializer;

        template <typename Action>
        status_t consume_next(Action&& action) {
            auto has_next_result = has_next();
            if(!has_next_result) {
                return std::unexpected(has_next_result.error());
            }
            if(!*has_next_result) {
                deserializer.mark_invalid();
                return std::unexpected(deserializer.current_error());
            }

            auto result = std::forward<Action>(action)(pending_value);
            if(!result) {
                return std::unexpected(result.error());
            }

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
                deserializer.mark_invalid(begin_err);
                return;
            }

            auto end_result = this->array.end();
            auto end_err = std::move(end_result).get(end_iter);
            if(end_err != simdjson::SUCCESS) {
                deserializer.mark_invalid(end_err);
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
                return std::unexpected(deserializer.current_error());
            }
            if(has_pending_value) {
                deserializer.mark_invalid();
                return std::unexpected(deserializer.current_error());
            }
            if(iter == end_iter) {
                return std::optional<std::string_view>{};
            }

            simdjson::ondemand::field field{};
            auto field_result = *iter;
            auto field_err = std::move(field_result).get(field);
            if(field_err != simdjson::SUCCESS) {
                deserializer.mark_invalid(field_err);
                return std::unexpected(deserializer.current_error());
            }

            auto key_err = field.unescaped_key(pending_key);
            if(key_err != simdjson::SUCCESS) {
                deserializer.mark_invalid(key_err);
                return std::unexpected(deserializer.current_error());
            }
            pending_value = std::move(field).value();
            has_pending_value = true;
            return std::optional<std::string_view>{std::string_view(pending_key)};
        }

        status_t invalid_key(std::string_view /*key_name*/) {
            deserializer.mark_invalid(simdjson::INCORRECT_TYPE);
            return std::unexpected(deserializer.current_error());
        }

        template <typename T>
        status_t deserialize_value(T& value) {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.current_error());
            }
            if(!has_pending_value) {
                deserializer.mark_invalid();
                return std::unexpected(deserializer.current_error());
            }

            auto parsed = deserializer.deserialize_from_value(pending_value, value);
            if(!parsed) {
                return std::unexpected(parsed.error());
            }

            ++iter;
            has_pending_value = false;
            return {};
        }

        status_t skip_value() {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.current_error());
            }
            if(!has_pending_value) {
                deserializer.mark_invalid();
                return std::unexpected(deserializer.current_error());
            }

            auto skipped = deserializer.skip_value(pending_value);
            if(!skipped) {
                return std::unexpected(skipped.error());
            }

            ++iter;
            has_pending_value = false;
            return {};
        }

        status_t end() {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.current_error());
            }

            if(has_pending_value) {
                auto skipped = skip_value();
                if(!skipped) {
                    return std::unexpected(skipped.error());
                }
            }

            while(iter != end_iter) {
                simdjson::ondemand::field field{};
                auto field_result = *iter;
                auto field_err = std::move(field_result).get(field);
                if(field_err != simdjson::SUCCESS) {
                    deserializer.mark_invalid(field_err);
                    return std::unexpected(deserializer.current_error());
                }

                auto value = std::move(field).value();
                auto skipped = deserializer.skip_value(value);
                if(!skipped) {
                    return std::unexpected(skipped.error());
                }

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
                deserializer.mark_invalid(begin_err);
                return;
            }

            auto end_result = this->object.end();
            auto end_err = std::move(end_result).get(end_iter);
            if(end_err != simdjson::SUCCESS) {
                deserializer.mark_invalid(end_err);
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
        if(is_valid) {
            return error_kind::ok;
        }
        return current_error();
    }

    status_t finish() {
        if(!is_valid) {
            return std::unexpected(current_error());
        }
        if(!root_consumed) {
            mark_invalid();
            return std::unexpected(current_error());
        }
        if(!document.at_end()) {
            mark_invalid(simdjson::TRAILING_CONTENT);
            return std::unexpected(current_error());
        }
        return {};
    }

    result_t<bool> deserialize_none() {
        if(!is_valid) {
            return std::unexpected(current_error());
        }

        bool is_none = false;
        simdjson::error_code err = simdjson::SUCCESS;
        if(current_value != nullptr) {
            err = current_value->is_null().get(is_none);
        } else {
            if(root_consumed) {
                mark_invalid();
                return std::unexpected(current_error());
            }

            err = document.is_null().get(is_none);
            if(err == simdjson::SUCCESS && is_none) {
                root_consumed = true;
            }
        }

        if(err != simdjson::SUCCESS) {
            mark_invalid(err);
            return std::unexpected(current_error());
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
            simdjson::error_code last_error = simdjson::INCORRECT_TYPE;

            auto try_alternative = [&](auto type_tag) {
                if(matched) {
                    return;
                }

                using alt_t = typename decltype(type_tag)::type;
                if(!variant_candidate_matches<alt_t>(*json_type, number_type)) {
                    return;
                }
                considered = true;

                auto candidate_error = deserialize_variant_candidate<alt_t>(*raw, value);
                if(candidate_error == simdjson::SUCCESS) {
                    matched = true;
                } else {
                    last_error = candidate_error;
                }
            };

            (try_alternative(std::type_identity<Ts>{}), ...);

            if(!matched) {
                mark_invalid(considered ? last_error : simdjson::INCORRECT_TYPE);
                return std::unexpected(current_error());
            }
            return {};
        }

        // TODO(eventide/serde): Non-object variant dispatch only tries the first
        // compatible alternative. This can fail for overlapping numeric types
        // (e.g. variant<uint8_t, int64_t> with input 300). Consider adding
        // backtracking here, similar to the object-path strategy above.
        bool considered = false;
        simdjson::error_code candidate_error = simdjson::INCORRECT_TYPE;
        auto try_first_compatible = [&](auto type_tag) {
            if(considered) {
                return;
            }

            using alt_t = typename decltype(type_tag)::type;
            if(!variant_candidate_matches<alt_t>(*json_type, number_type)) {
                return;
            }
            considered = true;
            candidate_error = deserialize_variant_candidate<alt_t>(*raw, value);
        };

        (try_first_compatible(std::type_identity<Ts>{}), ...);

        if(!considered) {
            mark_invalid(simdjson::INCORRECT_TYPE);
            return std::unexpected(current_error());
        }
        if(candidate_error != simdjson::SUCCESS) {
            mark_invalid(candidate_error);
            return std::unexpected(current_error());
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

        auto narrowed =
            serde::detail::narrow_int<T>(parsed, json::make_error(simdjson::NUMBER_OUT_OF_RANGE));
        if(!narrowed) {
            mark_invalid(simdjson::NUMBER_OUT_OF_RANGE);
            return std::unexpected(current_error());
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

        auto narrowed =
            serde::detail::narrow_uint<T>(parsed, json::make_error(simdjson::NUMBER_OUT_OF_RANGE));
        if(!narrowed) {
            mark_invalid(simdjson::NUMBER_OUT_OF_RANGE);
            return std::unexpected(current_error());
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

        auto narrowed =
            serde::detail::narrow_float<T>(parsed, json::make_error(simdjson::NUMBER_OUT_OF_RANGE));
        if(!narrowed) {
            mark_invalid(simdjson::NUMBER_OUT_OF_RANGE);
            return std::unexpected(current_error());
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

        auto narrowed =
            serde::detail::narrow_char(text, json::make_error(simdjson::INCORRECT_TYPE));
        if(!narrowed) {
            mark_invalid(simdjson::INCORRECT_TYPE);
            return std::unexpected(current_error());
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
        auto array = open_array();
        if(!array) {
            return std::unexpected(array.error());
        }

        DeserializeSeq seq(*this, std::move(*array), len.value_or(0), false);
        if(!is_valid) {
            return std::unexpected(current_error());
        }
        return seq;
    }

    result_t<DeserializeTuple> deserialize_tuple(std::size_t len) {
        auto array = open_array();
        if(!array) {
            return std::unexpected(array.error());
        }

        DeserializeTuple tuple(*this, std::move(*array), len, true);
        if(!is_valid) {
            return std::unexpected(current_error());
        }
        return tuple;
    }

    result_t<DeserializeMap> deserialize_map(std::optional<std::size_t> /*len*/) {
        auto object = open_object();
        if(!object) {
            return std::unexpected(object.error());
        }

        DeserializeMap map(*this, std::move(*object));
        if(!is_valid) {
            return std::unexpected(current_error());
        }
        return map;
    }

    result_t<DeserializeStruct> deserialize_struct(std::string_view /*name*/, std::size_t /*len*/) {
        auto object = open_object();
        if(!object) {
            return std::unexpected(object.error());
        }

        DeserializeStruct s(*this, std::move(*object));
        if(!is_valid) {
            return std::unexpected(current_error());
        }
        return s;
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
            return std::unexpected(current_error());
        }

        T out{};
        simdjson::error_code err = simdjson::SUCCESS;
        if(current_value != nullptr) {
            err = std::forward<ValFn>(val_fn)(*current_value).get(out);
        } else {
            if(root_consumed) {
                mark_invalid();
                return std::unexpected(current_error());
            }
            err = std::forward<DocFn>(doc_fn)(document).get(out);
            if(err == simdjson::SUCCESS && consume) {
                root_consumed = true;
            }
        }

        if(err != simdjson::SUCCESS) {
            mark_invalid(err);
            return std::unexpected(current_error());
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
            mark_invalid(err);
            return std::unexpected(current_error());
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
                                              std::variant<Ts...>& value) -> simdjson::error_code {
        Alt candidate{};
        Deserializer probe(raw);
        if(!probe.valid()) {
            return json::to_simdjson_error(probe.error());
        }

        auto status = serde::deserialize(probe, candidate);
        if(!status) {
            return json::to_simdjson_error(status.error());
        }

        auto finished = probe.finish();
        if(!finished) {
            return json::to_simdjson_error(finished.error());
        }

        value = std::move(candidate);
        return simdjson::SUCCESS;
    }

    result_t<simdjson::padded_string_view> consume_raw_json_view() {
        auto raw = read_source<std::string_view>([](auto& doc) { return doc.raw_json(); },
                                                 [](auto& val) { return val.raw_json(); });
        if(!raw) {
            return std::unexpected(raw.error());
        }
        return to_padded_subview(*raw);
    }

    result_t<simdjson::padded_string_view> to_padded_subview(std::string_view raw) {
        const char* base = input_view.data();
        if(base == nullptr) {
            mark_invalid();
            return std::unexpected(current_error());
        }

        const auto raw_addr = reinterpret_cast<std::uintptr_t>(raw.data());
        const auto base_addr = reinterpret_cast<std::uintptr_t>(base);
        const std::size_t total_capacity = input_view.capacity();

        if(raw_addr < base_addr) {
            mark_invalid();
            return std::unexpected(current_error());
        }

        const std::size_t offset = static_cast<std::size_t>(raw_addr - base_addr);
        if(offset > total_capacity || raw.size() > (total_capacity - offset)) {
            mark_invalid();
            return std::unexpected(current_error());
        }

        const std::size_t remaining_capacity = total_capacity - offset;
        return simdjson::padded_string_view(raw, remaining_capacity);
    }

    void initialize_document(simdjson::padded_string_view json) {
        input_view = json;

        auto document_result = parser.iterate(json);
        auto err = std::move(document_result).get(document);
        if(err != simdjson::SUCCESS) {
            mark_invalid(err);
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

private:
    bool is_valid = true;
    bool root_consumed = false;
    simdjson::error_code last_error = simdjson::SUCCESS;
    simdjson::ondemand::value* current_value = nullptr;

    simdjson::ondemand::parser parser;
    simdjson::padded_string json_buffer;
    simdjson::padded_string_view input_view{};
    simdjson::ondemand::document document;
};

template <typename T>
auto from_json(std::string_view json, T& value) -> std::expected<void, error_kind> {
    Deserializer deserializer(json);
    if(!deserializer.valid()) {
        return std::unexpected(deserializer.error());
    }

    auto result = serde::deserialize(deserializer, value);
    if(!result) {
        return std::unexpected(result.error());
    }

    return deserializer.finish();
}

template <typename T>
auto from_json(simdjson::padded_string_view json, T& value) -> std::expected<void, error_kind> {
    Deserializer deserializer(json);
    if(!deserializer.valid()) {
        return std::unexpected(deserializer.error());
    }

    auto result = serde::deserialize(deserializer, value);
    if(!result) {
        return std::unexpected(result.error());
    }

    return deserializer.finish();
}

template <typename T>
    requires std::default_initializable<T>
auto from_json(std::string_view json) -> std::expected<T, error_kind> {
    T value{};
    auto status = from_json(json, value);
    if(!status) {
        return std::unexpected(status.error());
    }
    return value;
}

template <typename T>
    requires std::default_initializable<T>
auto from_json(simdjson::padded_string_view json) -> std::expected<T, error_kind> {
    T value{};
    auto status = from_json(json, value);
    if(!status) {
        return std::unexpected(status.error());
    }
    return value;
}

static_assert(serde::deserializer_like<Deserializer>);

}  // namespace eventide::serde::json::simd
