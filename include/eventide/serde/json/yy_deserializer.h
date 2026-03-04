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

#include "eventide/serde/json/dom.h"
#include "eventide/serde/json/error.h"
#include "eventide/serde/serde.h"

namespace eventide::serde::json::yy {

class Deserializer {
public:
    using error_type = json::error_kind;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    class DeserializeArray {
    public:
        result_t<bool> has_next() {
            if(!deserializer.valid()) {
                return std::unexpected(deserializer.current_error());
            }
            return index < array.size();
        }

        template <typename T>
        status_t deserialize_element(T& value) {
            auto hasNext = has_next();
            if(!hasNext) {
                return std::unexpected(hasNext.error());
            }
            if(!*hasNext) {
                deserializer.mark_invalid();
                return std::unexpected(deserializer.current_error());
            }

            auto status = deserializer.deserialize_from_value_ref(array[index], value);
            if(!status) {
                return std::unexpected(status.error());
            }

            ++index;
            ++consumedCount;
            return {};
        }

        status_t skip_element() {
            auto hasNext = has_next();
            if(!hasNext) {
                return std::unexpected(hasNext.error());
            }
            if(!*hasNext) {
                deserializer.mark_invalid();
                return std::unexpected(deserializer.current_error());
            }

            ++index;
            ++consumedCount;
            return {};
        }

        status_t end() {
            if(!deserializer.valid()) {
                return std::unexpected(deserializer.current_error());
            }

            auto hasNext = has_next();
            if(!hasNext) {
                return std::unexpected(hasNext.error());
            }

            if(isStrictLength) {
                if(consumedCount != expectedLength || *hasNext) {
                    deserializer.mark_invalid();
                    return std::unexpected(deserializer.current_error());
                }
                return {};
            }

            index = array.size();
            return {};
        }

    private:
        friend class Deserializer;

        DeserializeArray(Deserializer& deserializer,
                         json::ArrayRef array,
                         std::size_t expectedLength,
                         bool isStrictLength) :
            deserializer(deserializer), array(array), expectedLength(expectedLength),
            isStrictLength(isStrictLength) {}

        Deserializer& deserializer;
        json::ArrayRef array;
        std::size_t index = 0;
        std::size_t expectedLength = 0;
        std::size_t consumedCount = 0;
        bool isStrictLength = false;
    };

    class DeserializeObject {
    public:
        result_t<std::optional<std::string_view>> next_key() {
            if(!deserializer.valid()) {
                return std::unexpected(deserializer.current_error());
            }
            if(hasPendingValue) {
                deserializer.mark_invalid();
                return std::unexpected(deserializer.current_error());
            }
            if(index == entries.size()) {
                return std::optional<std::string_view>{};
            }

            hasPendingValue = true;
            return std::optional<std::string_view>{entries[index].key};
        }

        status_t invalid_key(std::string_view /*key_name*/) {
            if(!deserializer.valid()) {
                return std::unexpected(deserializer.current_error());
            }
            if(!hasPendingValue) {
                deserializer.mark_invalid();
                return std::unexpected(deserializer.current_error());
            }

            ++index;
            hasPendingValue = false;
            return {};
        }

        template <typename T>
        status_t deserialize_value(T& value) {
            if(!deserializer.valid()) {
                return std::unexpected(deserializer.current_error());
            }
            if(!hasPendingValue) {
                deserializer.mark_invalid();
                return std::unexpected(deserializer.current_error());
            }

            auto status = deserializer.deserialize_from_value_ref(entries[index].value, value);
            if(!status) {
                return std::unexpected(status.error());
            }

            ++index;
            hasPendingValue = false;
            return {};
        }

        status_t skip_value() {
            if(!deserializer.valid()) {
                return std::unexpected(deserializer.current_error());
            }
            if(!hasPendingValue) {
                deserializer.mark_invalid();
                return std::unexpected(deserializer.current_error());
            }

            ++index;
            hasPendingValue = false;
            return {};
        }

        status_t end() {
            if(!deserializer.valid()) {
                return std::unexpected(deserializer.current_error());
            }

            if(hasPendingValue) {
                auto skipped = skip_value();
                if(!skipped) {
                    return std::unexpected(skipped.error());
                }
            }

            index = entries.size();
            return {};
        }

    private:
        friend class Deserializer;

        struct object_entry {
            std::string_view key;
            json::ValueRef value;
        };

        DeserializeObject(Deserializer& deserializer, json::ObjectRef object) :
            deserializer(deserializer) {
            auto collected = deserializer.collect_object_entries(object);
            if(!collected) {
                deserializer.mark_invalid(collected.error());
                return;
            }
            entries = std::move(*collected);
        }

        Deserializer& deserializer;
        std::vector<object_entry> entries;
        std::size_t index = 0;
        bool hasPendingValue = false;
    };

    using DeserializeSeq = DeserializeArray;
    using DeserializeTuple = DeserializeArray;
    using DeserializeMap = DeserializeObject;
    using DeserializeStruct = DeserializeObject;

    explicit Deserializer(const json::Value& value) : rootValue(value.as_ref()) {
        if(!rootValue.valid()) {
            mark_invalid();
        }
    }

    explicit Deserializer(json::ValueRef value) : rootValue(value) {
        if(!rootValue.valid()) {
            mark_invalid();
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return isValid;
    }

    [[nodiscard]] error_type error() const noexcept {
        return current_error();
    }

    status_t finish() {
        if(!isValid) {
            return std::unexpected(current_error());
        }
        if(!rootConsumed) {
            mark_invalid();
            return std::unexpected(current_error());
        }
        return {};
    }

    result_t<bool> deserialize_none() {
        if(!isValid) {
            return std::unexpected(current_error());
        }

        auto ref = peek_value_ref();
        if(!ref) {
            return std::unexpected(ref.error());
        }

        const bool isNone = ref->is_null();
        if(isNone && !hasCurrentValue) {
            rootConsumed = true;
        }
        return isNone;
    }

    template <typename T>
    status_t deserialize_some(T& value) {
        return serde::deserialize(*this, value);
    }

    template <typename... Ts>
    status_t deserialize_variant(std::variant<Ts...>& value) {
        static_assert((std::default_initializable<Ts> && ...),
                      "variant deserialization requires default-constructible alternatives");

        auto valueKind = peek_value_kind();
        if(!valueKind) {
            return std::unexpected(valueKind.error());
        }

        auto source = consume_value_ref();
        if(!source) {
            return std::unexpected(source.error());
        }

        bool matched = false;
        bool considered = false;
        error_type lastError = error_type::type_mismatch;

        auto try_alternative = [&](auto typeTag) {
            if(matched) {
                return;
            }

            using alt_t = typename decltype(typeTag)::type;
            if(!variant_candidate_matches<alt_t>(*valueKind)) {
                return;
            }

            considered = true;
            auto candidate = deserialize_variant_candidate<alt_t>(*source, value);
            if(candidate) {
                matched = true;
            } else {
                lastError = candidate.error();
            }
        };

        (try_alternative(std::type_identity<Ts>{}), ...);

        if(!matched) {
            mark_invalid(considered ? lastError : error_type::type_mismatch);
            return std::unexpected(current_error());
        }
        return {};
    }

    status_t deserialize_bool(bool& value) {
        return read_scalar(value, [](json::ValueRef ref) -> result_t<bool> {
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
        auto status = read_scalar(parsed, [](json::ValueRef ref) -> result_t<std::int64_t> {
            auto parsed = ref.get_int();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        if(!std::in_range<T>(parsed)) {
            mark_invalid(error_type::number_out_of_range);
            return std::unexpected(current_error());
        }

        value = static_cast<T>(parsed);
        return {};
    }

    template <serde::uint_like T>
    status_t deserialize_uint(T& value) {
        std::uint64_t parsed = 0;
        auto status = read_scalar(parsed, [](json::ValueRef ref) -> result_t<std::uint64_t> {
            auto parsed = ref.get_uint();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        if(!std::in_range<T>(parsed)) {
            mark_invalid(error_type::number_out_of_range);
            return std::unexpected(current_error());
        }

        value = static_cast<T>(parsed);
        return {};
    }

    template <serde::floating_like T>
    status_t deserialize_float(T& value) {
        double parsed = 0.0;
        auto status = read_scalar(parsed, [](json::ValueRef ref) -> result_t<double> {
            auto parsed = ref.get_double();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        if constexpr(!std::same_as<T, double>) {
            if(std::isfinite(parsed)) {
                const auto low = static_cast<long double>((std::numeric_limits<T>::lowest)());
                const auto high = static_cast<long double>((std::numeric_limits<T>::max)());
                const auto valueAsLongDouble = static_cast<long double>(parsed);
                if(valueAsLongDouble < low || valueAsLongDouble > high) {
                    mark_invalid(error_type::number_out_of_range);
                    return std::unexpected(current_error());
                }
            }
        }

        value = static_cast<T>(parsed);
        return {};
    }

    status_t deserialize_char(char& value) {
        std::string_view text;
        auto status = read_scalar(text, [](json::ValueRef ref) -> result_t<std::string_view> {
            auto parsed = ref.get_string();
            if(!parsed) {
                return std::unexpected(error_type::type_mismatch);
            }
            return *parsed;
        });
        if(!status) {
            return std::unexpected(status.error());
        }

        if(text.size() != 1) {
            mark_invalid(error_type::type_mismatch);
            return std::unexpected(current_error());
        }

        value = text.front();
        return {};
    }

    status_t deserialize_str(std::string& value) {
        std::string_view text;
        auto status = read_scalar(text, [](json::ValueRef ref) -> result_t<std::string_view> {
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
        auto seq = deserialize_seq(std::nullopt);
        if(!seq) {
            return std::unexpected(seq.error());
        }

        value.clear();
        while(true) {
            auto hasNext = seq->has_next();
            if(!hasNext) {
                return std::unexpected(hasNext.error());
            }
            if(!*hasNext) {
                break;
            }

            std::uint64_t byte = 0;
            auto byteStatus = seq->deserialize_element(byte);
            if(!byteStatus) {
                return std::unexpected(byteStatus.error());
            }
            if(byte > std::numeric_limits<std::uint8_t>::max()) {
                mark_invalid(error_type::number_out_of_range);
                return std::unexpected(current_error());
            }

            value.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(byte)));
        }

        return seq->end();
    }

    result_t<DeserializeSeq> deserialize_seq(std::optional<std::size_t> len) {
        auto array = open_array();
        if(!array) {
            return std::unexpected(array.error());
        }

        return DeserializeSeq(*this, *array, len.value_or(0), false);
    }

    result_t<DeserializeTuple> deserialize_tuple(std::size_t len) {
        auto array = open_array();
        if(!array) {
            return std::unexpected(array.error());
        }

        return DeserializeTuple(*this, *array, len, true);
    }

    result_t<DeserializeMap> deserialize_map(std::optional<std::size_t> /*len*/) {
        auto object = open_object();
        if(!object) {
            return std::unexpected(object.error());
        }

        DeserializeMap map(*this, *object);
        if(!isValid) {
            return std::unexpected(current_error());
        }
        return map;
    }

    result_t<DeserializeStruct> deserialize_struct(std::string_view /*name*/, std::size_t /*len*/) {
        auto object = open_object();
        if(!object) {
            return std::unexpected(object.error());
        }

        DeserializeStruct structure(*this, *object);
        if(!isValid) {
            return std::unexpected(current_error());
        }
        return structure;
    }

    result_t<json::Value> capture_dom_value() {
        auto source = consume_value_ref();
        if(!source) {
            return std::unexpected(source.error());
        } else {
            auto copied = json::Value::copy_of(*source);
            if(!copied.has_value()) {
                mark_invalid(copied.error());
                return std::unexpected(current_error());
            } else {
                return std::move(*copied);
            }
        }
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
        if(!isValid) {
            return std::unexpected(current_error());
        }

        auto ref = peek_value_ref();
        if(!ref) {
            return std::unexpected(ref.error());
        }

        auto parsed = std::forward<Reader>(reader)(*ref);
        if(!parsed) {
            mark_invalid(parsed.error());
            return std::unexpected(current_error());
        }

        out = *parsed;
        if(!hasCurrentValue) {
            rootConsumed = true;
        }
        return {};
    }

    template <typename T>
    status_t deserialize_from_value_ref(json::ValueRef input, T& out) {
        struct value_scope {
            value_scope(Deserializer& deserializer, json::ValueRef input) :
                deserializer(deserializer), previousHasCurrentValue(deserializer.hasCurrentValue),
                previousCurrentValue(deserializer.currentValue) {
                deserializer.currentValue = input;
                deserializer.hasCurrentValue = true;
            }

            ~value_scope() {
                deserializer.currentValue = previousCurrentValue;
                deserializer.hasCurrentValue = previousHasCurrentValue;
            }

            Deserializer& deserializer;
            bool previousHasCurrentValue;
            json::ValueRef previousCurrentValue;
        };

        value_scope scope(*this, input);
        return serde::deserialize(*this, out);
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

        mark_invalid(error_type::type_mismatch);
        return std::unexpected(current_error());
    }

    template <typename T>
    constexpr static bool variant_candidate_matches(value_kind kind) {
        using U = std::remove_cvref_t<T>;

        if constexpr(serde::annotated_type<U>) {
            using annotated_t = typename U::annotated_type;
            return variant_candidate_matches<annotated_t>(kind);
        } else if constexpr(is_specialization_of<std::optional, U>) {
            if(kind == value_kind::null) {
                return true;
            }
            return variant_candidate_matches<typename U::value_type>(kind);
        } else if constexpr(std::same_as<U, std::nullptr_t>) {
            return kind == value_kind::null;
        } else if constexpr(serde::bool_like<U>) {
            return kind == value_kind::boolean;
        } else if constexpr(serde::int_like<U> || serde::uint_like<U> || serde::floating_like<U>) {
            return kind == value_kind::number;
        } else if constexpr(serde::char_like<U> || std::same_as<U, std::string> ||
                            std::derived_from<U, std::string>) {
            return kind == value_kind::string;
        } else if constexpr(std::same_as<U, std::vector<std::byte>>) {
            return kind == value_kind::array;
        } else if constexpr(is_pair_v<U> || is_tuple_v<U>) {
            return kind == value_kind::array;
        } else if constexpr(std::ranges::input_range<U>) {
            constexpr auto formatKind = format_kind<U>;
            if constexpr(formatKind == range_format::map) {
                return kind == value_kind::object;
            } else if constexpr(formatKind == range_format::sequence ||
                                formatKind == range_format::set) {
                return kind == value_kind::array;
            } else {
                return true;
            }
        } else if constexpr(refl::reflectable_class<U>) {
            return kind == value_kind::object;
        } else {
            return true;
        }
    }

    template <typename Alt, typename... Ts>
    static auto deserialize_variant_candidate(json::ValueRef source, std::variant<Ts...>& value)
        -> status_t {
        Alt candidate{};
        Deserializer probe(source);
        if(!probe.valid()) {
            return std::unexpected(probe.error());
        }

        auto status = serde::deserialize(probe, candidate);
        if(!status) {
            return std::unexpected(status.error());
        }

        auto finished = probe.finish();
        if(!finished) {
            return std::unexpected(finished.error());
        }

        value = std::move(candidate);
        return {};
    }

    result_t<std::vector<typename DeserializeObject::object_entry>>
        collect_object_entries(json::ObjectRef object) {
        std::vector<typename DeserializeObject::object_entry> entries;
        entries.reserve(object.size());

        for(auto entry: object) {
            entries.push_back(typename DeserializeObject::object_entry{
                .key = entry.key,
                .value = entry.value,
            });
        }

        return entries;
    }

    result_t<json::ValueRef> peek_value_ref() {
        if(!isValid) {
            return std::unexpected(current_error());
        }
        if(hasCurrentValue) {
            return currentValue;
        }
        if(rootConsumed || !rootValue.valid()) {
            mark_invalid();
            return std::unexpected(current_error());
        }
        return rootValue;
    }

    result_t<json::ValueRef> consume_value_ref() {
        if(!isValid) {
            return std::unexpected(current_error());
        }
        if(hasCurrentValue) {
            return currentValue;
        }
        if(rootConsumed || !rootValue.valid()) {
            mark_invalid();
            return std::unexpected(current_error());
        }
        rootConsumed = true;
        return rootValue;
    }

    result_t<json::ArrayRef> open_array() {
        auto ref = consume_value_ref();
        if(!ref) {
            return std::unexpected(ref.error());
        }

        auto array = ref->get_array();
        if(!array) {
            mark_invalid(error_type::type_mismatch);
            return std::unexpected(current_error());
        }
        return *array;
    }

    result_t<json::ObjectRef> open_object() {
        auto ref = consume_value_ref();
        if(!ref) {
            return std::unexpected(ref.error());
        }

        auto object = ref->get_object();
        if(!object) {
            mark_invalid(error_type::type_mismatch);
            return std::unexpected(current_error());
        }
        return std::move(*object);
    }

    void mark_invalid(error_type error = error_type::invalid_state) {
        isValid = false;
        if(lastError == error_type::invalid_state || error != error_type::invalid_state) {
            lastError = error;
        }
    }

    [[nodiscard]] error_type current_error() const noexcept {
        return lastError;
    }

private:
    bool isValid = true;
    bool rootConsumed = false;
    error_type lastError = error_type::invalid_state;
    json::ValueRef rootValue{};
    bool hasCurrentValue = false;
    json::ValueRef currentValue{};
};

static_assert(serde::deserializer_like<Deserializer>);

}  // namespace eventide::serde::json::yy
