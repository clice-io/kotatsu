#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "eventide/serde/config.h"
#include "eventide/serde/detail/narrow.h"
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
class Deserializer {
public:
    using config_type = Config;
    using error_type = error_code;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    class DeserializeArray {
    public:
        result_t<bool> has_next() {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.current_error());
            }
            return consumed_count < vector.size();
        }

        template <typename T>
        status_t deserialize_element(T& value) {
            auto has_next_result = has_next();
            if(!has_next_result) {
                return std::unexpected(has_next_result.error());
            }
            if(!*has_next_result) {
                deserializer.mark_invalid();
                return std::unexpected(deserializer.current_error());
            }

            auto parsed = deserializer.deserialize_from_reference(vector[consumed_count], value);
            if(!parsed) {
                return std::unexpected(parsed.error());
            }

            ++consumed_count;
            return {};
        }

        status_t skip_element() {
            auto has_next_result = has_next();
            if(!has_next_result) {
                return std::unexpected(has_next_result.error());
            }
            if(!*has_next_result) {
                deserializer.mark_invalid();
                return std::unexpected(deserializer.current_error());
            }

            ++consumed_count;
            return {};
        }

        status_t end() {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.current_error());
            }

            if(is_strict_length) {
                if(consumed_count != expected_length || consumed_count != vector.size()) {
                    deserializer.mark_invalid(error_code::type_mismatch);
                    return std::unexpected(deserializer.current_error());
                }
                return {};
            }

            consumed_count = vector.size();
            return {};
        }

    private:
        friend class Deserializer<Config>;

        DeserializeArray(Deserializer<Config>& deserializer,
                         ::flexbuffers::Vector vector,
                         std::size_t expected_length,
                         bool is_strict_length) :
            deserializer(deserializer), vector(vector), expected_length(expected_length),
            is_strict_length(is_strict_length) {}

        Deserializer<Config>& deserializer;
        ::flexbuffers::Vector vector;
        std::size_t expected_length = 0;
        std::size_t consumed_count = 0;
        bool is_strict_length = false;
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
            if(index >= keys.size()) {
                return std::optional<std::string_view>{};
            }

            pending_key.assign(keys[index].AsKey());
            pending_value = values[index];
            has_pending_value = true;
            return std::optional<std::string_view>{std::string_view(pending_key)};
        }

        status_t invalid_key(std::string_view /*key_name*/) {
            deserializer.mark_invalid(error_code::invalid_key);
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

            auto parsed = deserializer.deserialize_from_reference(pending_value, value);
            if(!parsed) {
                return std::unexpected(parsed.error());
            }

            ++index;
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

            ++index;
            has_pending_value = false;
            return {};
        }

        status_t end() {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.current_error());
            }

            if(has_pending_value) {
                ++index;
                has_pending_value = false;
            }
            index = keys.size();
            return {};
        }

    private:
        friend class Deserializer<Config>;

        DeserializeObject(Deserializer<Config>& deserializer, ::flexbuffers::Map map) :
            deserializer(deserializer), map(map), keys(this->map.Keys()),
            values(this->map.Values()) {
            if(keys.size() != values.size()) {
                deserializer.mark_invalid(error_code::invalid_buffer);
            }
        }

        Deserializer<Config>& deserializer;
        ::flexbuffers::Map map;
        ::flexbuffers::TypedVector keys;
        ::flexbuffers::Vector values;
        std::size_t index = 0;
        ::flexbuffers::Reference pending_value{};
        std::string pending_key;
        bool has_pending_value = false;
    };

    using DeserializeSeq = DeserializeArray;
    using DeserializeTuple = DeserializeArray;
    using DeserializeMap = DeserializeObject;
    using DeserializeStruct = DeserializeObject;

    explicit Deserializer(std::span<const std::uint8_t> bytes) :
        buffer_storage(bytes.begin(), bytes.end()) {
        initialize();
    }

    explicit Deserializer(std::span<const std::byte> bytes) {
        if(!bytes.empty()) {
            const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
            buffer_storage.assign(data, data + bytes.size());
        }
        initialize();
    }

    explicit Deserializer(const std::vector<std::uint8_t>& bytes) : buffer_storage(bytes) {
        initialize();
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

    status_t finish() {
        if(!is_valid) {
            return std::unexpected(current_error());
        }
        if(!root_consumed) {
            mark_invalid(error_code::root_not_consumed);
            return std::unexpected(current_error());
        }
        return {};
    }

    result_t<bool> deserialize_none() {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }

        const bool is_none = reference->IsNull();
        if(is_none) {
            consume_root_if_needed();
        }
        return is_none;
    }

    template <typename... Ts>
    status_t deserialize_variant(std::variant<Ts...>& value) {
        static_assert((std::default_initializable<Ts> && ...),
                      "variant deserialization requires default-constructible alternatives");

        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }

        auto matches = [&]<typename Alt>() -> bool {
            using clean_alt_t = std::remove_cvref_t<Alt>;
            if constexpr(std::same_as<clean_alt_t, std::monostate>) {
                return reference->IsNull();
            } else if constexpr(std::same_as<clean_alt_t, std::nullptr_t>) {
                return reference->IsNull();
            } else if constexpr(serde::bool_like<clean_alt_t>) {
                return reference->IsBool();
            } else if constexpr(serde::int_like<clean_alt_t>) {
                return reference->IsInt() || reference->IsUInt();
            } else if constexpr(serde::uint_like<clean_alt_t>) {
                return reference->IsUInt() || reference->IsInt();
            } else if constexpr(serde::floating_like<clean_alt_t>) {
                return reference->IsNumeric();
            } else if constexpr(serde::char_like<clean_alt_t>) {
                return reference->IsString() || reference->IsKey();
            } else if constexpr(serde::str_like<clean_alt_t>) {
                return reference->IsString() || reference->IsKey();
            } else if constexpr(serde::bytes_like<clean_alt_t>) {
                return reference->IsBlob() || (reference->IsVector() && !reference->IsMap()) ||
                       reference->IsString();
            } else if constexpr(is_pair_v<clean_alt_t> || is_tuple_v<clean_alt_t>) {
                return reference->IsVector() && !reference->IsMap();
            } else if constexpr(std::ranges::input_range<clean_alt_t>) {
                constexpr auto kind = eventide::format_kind<clean_alt_t>;
                if constexpr(kind == eventide::range_format::map) {
                    return reference->IsMap();
                } else {
                    return reference->IsVector() && !reference->IsMap();
                }
            } else if constexpr(refl::reflectable_class<clean_alt_t>) {
                return reference->IsMap();
            } else {
                return true;
            }
        };

        bool matched = false;
        std::expected<void, error_type> status = std::unexpected(error_code::type_mismatch);

        auto try_alternative = [&]<typename Alt>() {
            if(matched || !matches.template operator()<Alt>()) {
                return;
            }

            matched = true;
            Alt candidate{};
            auto candidate_status = deserialize_from_reference(*reference, candidate);
            if(!candidate_status) {
                status = std::unexpected(candidate_status.error());
                return;
            }

            value = std::move(candidate);
            status = {};
        };

        (try_alternative.template operator()<Ts>(), ...);

        if(!matched) {
            mark_invalid(error_code::type_mismatch);
            return std::unexpected(current_error());
        }

        if(!status) {
            return std::unexpected(status.error());
        }

        consume_root_if_needed();
        return {};
    }

    status_t deserialize_bool(bool& value) {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }
        if(!reference->IsBool()) {
            mark_invalid(error_code::type_mismatch);
            return std::unexpected(current_error());
        }

        value = reference->AsBool();
        consume_root_if_needed();
        return {};
    }

    template <serde::int_like T>
    status_t deserialize_int(T& value) {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }

        std::int64_t parsed = 0;
        if(reference->IsInt()) {
            parsed = reference->AsInt64();
        } else if(reference->IsUInt()) {
            const std::uint64_t raw = reference->AsUInt64();
            if(!std::in_range<std::int64_t>(raw)) {
                mark_invalid(error_code::number_out_of_range);
                return std::unexpected(current_error());
            }
            parsed = static_cast<std::int64_t>(raw);
        } else {
            mark_invalid(error_code::type_mismatch);
            return std::unexpected(current_error());
        }

        auto narrowed = serde::detail::narrow_int<T>(parsed, error_code::number_out_of_range);
        if(!narrowed) {
            mark_invalid(narrowed.error());
            return std::unexpected(current_error());
        }

        value = *narrowed;
        consume_root_if_needed();
        return {};
    }

    template <serde::uint_like T>
    status_t deserialize_uint(T& value) {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }

        std::uint64_t parsed = 0;
        if(reference->IsUInt()) {
            parsed = reference->AsUInt64();
        } else if(reference->IsInt()) {
            const std::int64_t raw = reference->AsInt64();
            if(raw < 0) {
                mark_invalid(error_code::number_out_of_range);
                return std::unexpected(current_error());
            }
            parsed = static_cast<std::uint64_t>(raw);
        } else {
            mark_invalid(error_code::type_mismatch);
            return std::unexpected(current_error());
        }

        auto narrowed = serde::detail::narrow_uint<T>(parsed, error_code::number_out_of_range);
        if(!narrowed) {
            mark_invalid(narrowed.error());
            return std::unexpected(current_error());
        }

        value = *narrowed;
        consume_root_if_needed();
        return {};
    }

    template <serde::floating_like T>
    status_t deserialize_float(T& value) {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }
        if(!reference->IsNumeric()) {
            mark_invalid(error_code::type_mismatch);
            return std::unexpected(current_error());
        }

        const double parsed = reference->AsDouble();
        auto narrowed = serde::detail::narrow_float<T>(parsed, error_code::number_out_of_range);
        if(!narrowed) {
            mark_invalid(narrowed.error());
            return std::unexpected(current_error());
        }

        value = *narrowed;
        consume_root_if_needed();
        return {};
    }

    status_t deserialize_char(char& value) {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }
        if(!reference->IsString() && !reference->IsKey()) {
            mark_invalid(error_code::type_mismatch);
            return std::unexpected(current_error());
        }

        auto text = reference->AsString();
        if(text.length() != 1) {
            mark_invalid(error_code::invalid_char);
            return std::unexpected(current_error());
        }

        value = text.c_str()[0];
        consume_root_if_needed();
        return {};
    }

    status_t deserialize_str(std::string& value) {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }
        if(!reference->IsString() && !reference->IsKey()) {
            mark_invalid(error_code::type_mismatch);
            return std::unexpected(current_error());
        }

        auto text = reference->AsString();
        value.assign(text.c_str(), text.length());
        consume_root_if_needed();
        return {};
    }

    status_t deserialize_bytes(std::vector<std::byte>& value) {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }

        value.clear();
        if(reference->IsBlob() || reference->IsString()) {
            auto blob = reference->AsBlob();
            value.reserve(blob.size());
            for(std::size_t i = 0; i < blob.size(); ++i) {
                value.push_back(static_cast<std::byte>(blob.data()[i]));
            }

            consume_root_if_needed();
            return {};
        }

        if(reference->IsMap() || !reference->IsVector()) {
            mark_invalid(error_code::type_mismatch);
            return std::unexpected(current_error());
        }

        auto vec = reference->AsVector();
        value.reserve(vec.size());
        for(std::size_t i = 0; i < vec.size(); ++i) {
            const auto element = vec[i];
            std::uint64_t parsed = 0;
            if(element.IsUInt()) {
                parsed = element.AsUInt64();
            } else if(element.IsInt()) {
                const auto signed_value = element.AsInt64();
                if(signed_value < 0) {
                    mark_invalid(error_code::number_out_of_range);
                    return std::unexpected(current_error());
                }
                parsed = static_cast<std::uint64_t>(signed_value);
            } else {
                mark_invalid(error_code::type_mismatch);
                return std::unexpected(current_error());
            }

            if(parsed > static_cast<std::uint64_t>(std::numeric_limits<std::uint8_t>::max())) {
                mark_invalid(error_code::number_out_of_range);
                return std::unexpected(current_error());
            }
            value.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(parsed)));
        }

        consume_root_if_needed();
        return {};
    }

    result_t<DeserializeSeq> deserialize_seq(std::optional<std::size_t> len) {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }
        if(reference->IsMap() || !reference->IsVector()) {
            mark_invalid(error_code::type_mismatch);
            return std::unexpected(current_error());
        }

        consume_root_if_needed();
        return DeserializeSeq(*this, reference->AsVector(), len.value_or(0), false);
    }

    result_t<DeserializeTuple> deserialize_tuple(std::size_t len) {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }
        if(reference->IsMap() || !reference->IsVector()) {
            mark_invalid(error_code::type_mismatch);
            return std::unexpected(current_error());
        }

        consume_root_if_needed();
        return DeserializeTuple(*this, reference->AsVector(), len, true);
    }

    result_t<DeserializeMap> deserialize_map(std::optional<std::size_t> /*len*/) {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }
        if(!reference->IsMap()) {
            mark_invalid(error_code::type_mismatch);
            return std::unexpected(current_error());
        }

        consume_root_if_needed();
        DeserializeMap out(*this, reference->AsMap());
        if(!is_valid) {
            return std::unexpected(current_error());
        }
        return out;
    }

    result_t<DeserializeStruct> deserialize_struct(std::string_view /*name*/, std::size_t /*len*/) {
        auto reference = active_reference();
        if(!reference) {
            return std::unexpected(reference.error());
        }
        if(!reference->IsMap()) {
            mark_invalid(error_code::type_mismatch);
            return std::unexpected(current_error());
        }

        consume_root_if_needed();
        DeserializeStruct out(*this, reference->AsMap());
        if(!is_valid) {
            return std::unexpected(current_error());
        }
        return out;
    }

private:
    struct value_scope {
        value_scope(Deserializer<Config>& deserializer, const ::flexbuffers::Reference& reference) :
            deserializer(deserializer), previous(deserializer.current_value), value(reference) {
            deserializer.current_value = &value;
        }

        ~value_scope() {
            deserializer.current_value = previous;
        }

        Deserializer<Config>& deserializer;
        const ::flexbuffers::Reference* previous;
        ::flexbuffers::Reference value;
    };

    template <typename T>
    status_t deserialize_from_reference(const ::flexbuffers::Reference& reference, T& out) {
        value_scope scope(*this, reference);
        return serde::deserialize(*this, out);
    }

    result_t<::flexbuffers::Reference> active_reference() {
        if(!is_valid) {
            return std::unexpected(current_error());
        }
        if(current_value != nullptr) {
            return *current_value;
        }
        if(root_consumed) {
            mark_invalid();
            return std::unexpected(current_error());
        }
        return root_reference;
    }

    void consume_root_if_needed() {
        if(current_value == nullptr) {
            root_consumed = true;
        }
    }

    void initialize() {
        if(buffer_storage.empty()) {
            mark_invalid(error_code::invalid_buffer);
            return;
        }
        if(!::flexbuffers::VerifyBuffer(buffer_storage.data(), buffer_storage.size())) {
            mark_invalid(error_code::invalid_buffer);
            return;
        }

        root_reference = ::flexbuffers::GetRoot(buffer_storage.data(), buffer_storage.size());
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

private:
    bool is_valid = true;
    bool root_consumed = false;
    error_type last_error = error_code::none;
    const ::flexbuffers::Reference* current_value = nullptr;

    std::vector<std::uint8_t> buffer_storage;
    ::flexbuffers::Reference root_reference{};
};

template <typename Config = config::default_config, typename T>
auto from_flatbuffer(std::span<const std::uint8_t> bytes, T& value)
    -> std::expected<void, error_code> {
    Deserializer<Config> deserializer(bytes);
    if(!deserializer.valid()) {
        return std::unexpected(deserializer.error());
    }

    auto result = serde::deserialize(deserializer, value);
    if(!result) {
        return std::unexpected(result.error());
    }

    return deserializer.finish();
}

template <typename Config = config::default_config, typename T>
auto from_flatbuffer(std::span<const std::byte> bytes, T& value)
    -> std::expected<void, error_code> {
    if(bytes.empty()) {
        return from_flatbuffer<Config>(std::span<const std::uint8_t>{}, value);
    }
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    return from_flatbuffer<Config>(std::span<const std::uint8_t>(data, bytes.size()), value);
}

template <typename Config = config::default_config, typename T>
auto from_flatbuffer(const std::vector<std::uint8_t>& bytes, T& value)
    -> std::expected<void, error_code> {
    return from_flatbuffer<Config>(std::span<const std::uint8_t>(bytes.data(), bytes.size()),
                                   value);
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_flatbuffer(std::span<const std::uint8_t> bytes) -> std::expected<T, error_code> {
    T value{};
    auto status = from_flatbuffer<Config>(bytes, value);
    if(!status) {
        return std::unexpected(status.error());
    }
    return value;
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_flatbuffer(std::span<const std::byte> bytes) -> std::expected<T, error_code> {
    if(bytes.empty()) {
        return from_flatbuffer<T, Config>(std::span<const std::uint8_t>{});
    }
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    return from_flatbuffer<T, Config>(std::span<const std::uint8_t>(data, bytes.size()));
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_flatbuffer(const std::vector<std::uint8_t>& bytes) -> std::expected<T, error_code> {
    return from_flatbuffer<T, Config>(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}

static_assert(serde::deserializer_like<Deserializer<>>);

}  // namespace eventide::serde::flex
