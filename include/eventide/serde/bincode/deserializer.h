#pragma once

#include <bit>
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
#include <variant>
#include <vector>

#include "eventide/common/expected_try.h"
#include "eventide/common/ranges.h"
#include "eventide/serde/bincode/error.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/serde.h"
#include "eventide/serde/serde/utils/narrow.h"

namespace eventide::serde::bincode {

template <typename Config = config::default_config>
class Deserializer {
public:
    using config_type = Config;
    using error_type = bincode::error;

    template <typename T>
    using result_t = std::expected<T, error_type>;

    using status_t = result_t<void>;

    class DeserializeSeq {
    public:
        DeserializeSeq(Deserializer& deserializer, std::size_t expected_count) noexcept :
            deserializer(deserializer), expected_count(expected_count) {}

        result_t<bool> has_next() {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.last_error);
            }
            return read_count < expected_count;
        }

        template <typename T>
        status_t deserialize_element(T& value) {
            ETD_EXPECTED_TRY_V(auto has_next_value, has_next());
            if(!has_next_value) {
                return deserializer.mark_invalid(error_type::invalid_state);
            }

            ETD_EXPECTED_TRY(serde::deserialize(deserializer, value));

            ++read_count;
            return {};
        }

        status_t skip_element() {
            return deserializer.mark_invalid(error_kind::unsupported_operation);
        }

        status_t end() {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.last_error);
            }
            if(read_count != expected_count) {
                return deserializer.mark_invalid(error_type::invalid_state);
            }
            return {};
        }

    private:
        Deserializer& deserializer;
        std::size_t expected_count = 0;
        std::size_t read_count = 0;
    };

    class DeserializeTuple {
    public:
        DeserializeTuple(Deserializer& deserializer, std::size_t expected_count) noexcept :
            deserializer(deserializer), expected_count(expected_count) {}

        template <typename T>
        status_t deserialize_element(T& value) {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.last_error);
            }
            if(read_count >= expected_count) {
                return deserializer.mark_invalid(error_type::invalid_state);
            }

            ETD_EXPECTED_TRY(serde::deserialize(deserializer, value));

            ++read_count;
            return {};
        }

        status_t skip_element() {
            return deserializer.mark_invalid(error_kind::unsupported_operation);
        }

        status_t end() {
            if(!deserializer.is_valid) {
                return std::unexpected(deserializer.last_error);
            }
            if(read_count != expected_count) {
                return deserializer.mark_invalid(error_type::invalid_state);
            }
            return {};
        }

    private:
        Deserializer& deserializer;
        std::size_t expected_count = 0;
        std::size_t read_count = 0;
    };

    class DeserializeUnsupported {
    public:
        explicit DeserializeUnsupported(Deserializer& deserializer) noexcept :
            deserializer(deserializer) {}

        result_t<std::optional<std::string_view>> next_key() {
            return deserializer.mark_invalid(error_kind::unsupported_operation);
        }

        status_t invalid_key(std::string_view /*key_name*/) {
            return deserializer.mark_invalid(error_kind::unsupported_operation);
        }

        template <typename T>
        status_t deserialize_value(T& /*value*/) {
            return deserializer.mark_invalid(error_kind::unsupported_operation);
        }

        status_t skip_value() {
            return deserializer.mark_invalid(error_kind::unsupported_operation);
        }

        status_t end() {
            return deserializer.mark_invalid(error_kind::unsupported_operation);
        }

    private:
        Deserializer& deserializer;
    };

    using DeserializeMap = DeserializeUnsupported;
    using DeserializeStruct = DeserializeUnsupported;

    explicit Deserializer(std::span<const std::byte> bytes) : bytes(bytes) {}

    explicit Deserializer(std::span<const std::uint8_t> bytes) :
        bytes(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()) {}

    explicit Deserializer(const std::vector<std::byte>& bytes) :
        Deserializer(std::span<const std::byte>(bytes.data(), bytes.size())) {}

    explicit Deserializer(const std::vector<std::uint8_t>& bytes) :
        Deserializer(std::span<const std::uint8_t>(bytes.data(), bytes.size())) {}

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
        if(offset != bytes.size()) {
            return mark_invalid(error_kind::trailing_bytes);
        }
        return {};
    }

    status_t deserialize_bool(bool& value) {
        ETD_EXPECTED_TRY_V(auto parsed, read_u8());

        if(parsed > 1U) {
            return mark_invalid(error_type::type_mismatch);
        }
        value = parsed == 1U;
        return {};
    }

    template <serde::int_like T>
    status_t deserialize_int(T& value) {
        ETD_EXPECTED_TRY_V(auto parsed, read_integral<std::int64_t>());

        auto narrowed = serde::detail::narrow_int<T>(parsed, error_type::number_out_of_range);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }
        value = *narrowed;
        return {};
    }

    template <serde::uint_like T>
    status_t deserialize_uint(T& value) {
        ETD_EXPECTED_TRY_V(auto parsed, read_integral<std::uint64_t>());

        auto narrowed = serde::detail::narrow_uint<T>(parsed, error_type::number_out_of_range);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }
        value = *narrowed;
        return {};
    }

    template <serde::floating_like T>
    status_t deserialize_float(T& value) {
        ETD_EXPECTED_TRY_V(auto raw, read_integral<std::uint64_t>());

        const double parsed = std::bit_cast<double>(raw);
        auto narrowed = serde::detail::narrow_float<T>(parsed, error_type::number_out_of_range);
        if(!narrowed) {
            return mark_invalid(narrowed.error());
        }
        value = *narrowed;
        return {};
    }

    status_t deserialize_char(char& value) {
        ETD_EXPECTED_TRY_V(auto parsed, read_u8());
        value = static_cast<char>(parsed);
        return {};
    }

    status_t deserialize_str(std::string& value) {
        ETD_EXPECTED_TRY_V(auto length, read_length());

        if(offset + length > bytes.size()) {
            return mark_invalid(error_kind::unexpected_eof);
        }

        if(length == 0) {
            value.clear();
            return {};
        }

        const auto* begin = reinterpret_cast<const char*>(bytes.data() + offset);
        value.assign(begin, begin + length);
        offset += length;
        return {};
    }

    status_t deserialize_bytes(std::vector<std::byte>& value) {
        ETD_EXPECTED_TRY_V(auto length, read_length());

        if(offset + length > bytes.size()) {
            return mark_invalid(error_kind::unexpected_eof);
        }

        if(length == 0) {
            value.clear();
            return {};
        }

        value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                     bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
        return {};
    }

    result_t<bool> deserialize_none() {
        ETD_EXPECTED_TRY_V(auto tag, read_u8());

        if(tag == 0U) {
            return true;
        }
        if(tag == 1U) {
            return false;
        }
        return mark_invalid(error_type::type_mismatch);
    }

    template <typename... Ts>
    status_t deserialize_variant(std::variant<Ts...>& value) {
        ETD_EXPECTED_TRY_V(auto index, read_integral<std::uint32_t>());

        constexpr std::size_t variant_size = sizeof...(Ts);
        if(index >= variant_size) {
            return mark_invalid(error_kind::invalid_variant_index);
        }

        std::expected<void, error_type> status{};
        bool matched = false;

        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&] {
                 if(index != I) {
                     return;
                 }

                 matched = true;
                 status = deserialize_variant_alternative<I>(value);
             }()),
             ...);
        }(std::make_index_sequence<variant_size>{});

        if(!matched) {
            return mark_invalid(error_kind::invalid_variant_index);
        }
        if(!status) {
            return std::unexpected(status.error());
        }
        return {};
    }

    result_t<DeserializeSeq> deserialize_seq(std::optional<std::size_t> len) {
        ETD_EXPECTED_TRY_V(auto parsed, read_length());

        if(len.has_value() && *len != parsed) {
            return std::unexpected(error_type::invalid_state);
        }

        return DeserializeSeq(*this, parsed);
    }

    result_t<DeserializeTuple> deserialize_tuple(std::size_t len) {
        return DeserializeTuple(*this, len);
    }

    result_t<DeserializeMap> deserialize_map(std::optional<std::size_t> /*len*/) {
        return DeserializeUnsupported(*this);
    }

    result_t<DeserializeStruct> deserialize_struct(std::string_view /*name*/, std::size_t /*len*/) {
        return DeserializeUnsupported(*this);
    }

    result_t<std::size_t> read_length_prefix() {
        return read_length();
    }

private:
    template <std::size_t I, typename Variant>
    status_t deserialize_variant_alternative(Variant& value) {
        using alt_t = std::variant_alternative_t<I, Variant>;
        if constexpr(std::same_as<alt_t, std::monostate>) {
            value = std::monostate{};
            return {};
        } else if constexpr(std::default_initializable<alt_t>) {
            alt_t alt{};
            ETD_EXPECTED_TRY(serde::deserialize(*this, alt));

            value = std::move(alt);
            return {};
        } else {
            return mark_invalid(error_type::invalid_state);
        }
    }

    template <typename T>
        requires std::integral<T>
    result_t<T> read_integral() {
        if(!is_valid) {
            return std::unexpected(last_error);
        }

        using unsigned_t = std::make_unsigned_t<T>;
        if(offset + sizeof(unsigned_t) > bytes.size()) {
            return mark_invalid(error_kind::unexpected_eof);
        }

        unsigned_t raw = 0;
        for(std::size_t i = 0; i < sizeof(unsigned_t); ++i) {
            const auto byte = std::to_integer<std::uint8_t>(bytes[offset + i]);
            raw |= (static_cast<unsigned_t>(byte) << (i * 8));
        }

        offset += sizeof(unsigned_t);
        if constexpr(std::signed_integral<T>) {
            return std::bit_cast<T>(raw);
        } else {
            return static_cast<T>(raw);
        }
    }

    result_t<std::uint8_t> read_u8() {
        return read_integral<std::uint8_t>();
    }

    result_t<std::size_t> read_length() {
        ETD_EXPECTED_TRY_V(auto raw, read_integral<std::uint64_t>());

        if(raw > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
            return mark_invalid(error_type::number_out_of_range);
        }

        return static_cast<std::size_t>(raw);
    }

    std::unexpected<error_type> mark_invalid(error_type error) {
        is_valid = false;
        last_error = error;
        return std::unexpected(last_error);
    }

private:
    std::span<const std::byte> bytes{};
    std::size_t offset = 0;
    bool is_valid = true;
    error_type last_error = error_kind::ok;
};

template <typename Config = config::default_config, typename T>
auto from_bytes(std::span<const std::byte> bytes, T& value) -> std::expected<void, error> {
    Deserializer<Config> deserializer(bytes);
    if(!deserializer.valid()) {
        return std::unexpected(deserializer.error());
    }

    ETD_EXPECTED_TRY(serde::deserialize(deserializer, value));
    ETD_EXPECTED_TRY(deserializer.finish());
    return {};
}

template <typename Config = config::default_config, typename T>
auto from_bytes(std::span<const std::uint8_t> bytes, T& value) -> std::expected<void, error> {
    return from_bytes<Config>(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()),
        value);
}

template <typename Config = config::default_config, typename T>
auto from_bytes(const std::vector<std::byte>& bytes, T& value) -> std::expected<void, error> {
    return from_bytes<Config>(std::span<const std::byte>(bytes.data(), bytes.size()), value);
}

template <typename Config = config::default_config, typename T>
auto from_bytes(const std::vector<std::uint8_t>& bytes, T& value) -> std::expected<void, error> {
    return from_bytes<Config>(std::span<const std::uint8_t>(bytes.data(), bytes.size()), value);
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_bytes(std::span<const std::byte> bytes) -> std::expected<T, error> {
    T value{};
    ETD_EXPECTED_TRY(from_bytes<Config>(bytes, value));
    return value;
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_bytes(std::span<const std::uint8_t> bytes) -> std::expected<T, error> {
    T value{};
    ETD_EXPECTED_TRY(from_bytes<Config>(bytes, value));
    return value;
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_bytes(const std::vector<std::byte>& bytes) -> std::expected<T, error> {
    return from_bytes<T, Config>(std::span<const std::byte>(bytes.data(), bytes.size()));
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_bytes(const std::vector<std::uint8_t>& bytes) -> std::expected<T, error> {
    return from_bytes<T, Config>(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
}

static_assert(serde::deserializer_like<Deserializer<>>);

}  // namespace eventide::serde::bincode

namespace eventide::serde {

namespace detail {

template <typename Config, typename E, typename D, typename Field>
constexpr auto deserialize_sequential_struct_field(D& deserializer, Field field)
    -> std::expected<void, E> {
    using field_t = typename std::remove_cvref_t<decltype(field)>::type;

    if constexpr(!refl::annotated_type<field_t>) {
        ETD_EXPECTED_TRY(serde::deserialize(deserializer, field.value()));
        return {};
    } else {
        using attrs_t = typename std::remove_cvref_t<field_t>::attrs;
        auto&& value = refl::annotated_value(field.value());
        using value_t = std::remove_cvref_t<decltype(value)>;

        // schema::skip excludes the field from the wire format.
        if constexpr(tuple_has_v<attrs_t, refl::attrs::skip>) {
            return {};
        }
        // schema::flatten in bincode is equivalent to inlining nested field sequence.
        else if constexpr(tuple_has_v<attrs_t, refl::attrs::flatten>) {
            static_assert(refl::reflectable_class<value_t>,
                          "schema::flatten requires a reflectable class field type");

            std::expected<void, E> nested_status{};
            refl::for_each(value, [&](auto nested_field) {
                auto status =
                    deserialize_sequential_struct_field<Config, E>(deserializer, nested_field);
                if(!status) {
                    nested_status = std::unexpected(status.error());
                    return false;
                }
                return true;
            });
            return nested_status;
        } else {
            if constexpr(tuple_has_spec_v<attrs_t, refl::behavior::skip_if>) {
                using skip_if_attr = tuple_find_spec_t<attrs_t, refl::behavior::skip_if>;
                using Pred = typename skip_if_attr::predicate;
                if(refl::evaluate_skip_predicate<Pred>(value, false)) {
                    using consume_t = std::remove_cvref_t<decltype(field.value())>;
                    static_assert(std::default_initializable<consume_t>,
                                  "bincode behavior::skip_if requires default-initializable field");
                    consume_t skipped{};
                    ETD_EXPECTED_TRY(serde::deserialize(deserializer, skipped));
                    return {};
                }
            }

            // Keep annotation wrapper so tagged/provider attrs are still honored.
            ETD_EXPECTED_TRY(serde::deserialize(deserializer, field.value()));
            return {};
        }
    }
}

}  // namespace detail

template <typename Config, typename T>
    requires (refl::reflectable_class<std::remove_cvref_t<T>> &&
              !std::ranges::input_range<std::remove_cvref_t<T>>)
struct deserialize_traits<bincode::Deserializer<Config>, T> {
    using deserializer_t = bincode::Deserializer<Config>;
    using error_type = typename deserializer_t::error_type;

    static auto deserialize(deserializer_t& deserializer, T& value)
        -> std::expected<void, error_type> {
        std::expected<void, error_type> field_status{};

        refl::for_each(value, [&](auto field) {
            auto status =
                detail::deserialize_sequential_struct_field<Config, error_type>(deserializer,
                                                                                field);
            if(!status) {
                field_status = std::unexpected(status.error());
                return false;
            }
            return true;
        });

        return field_status;
    }
};

template <typename Config, typename T>
    requires (std::ranges::input_range<std::remove_cvref_t<T>> &&
              format_kind<std::remove_cvref_t<T>> == range_format::map)
struct deserialize_traits<bincode::Deserializer<Config>, T> {
    using deserializer_t = bincode::Deserializer<Config>;
    using error_type = typename deserializer_t::error_type;
    using map_t = std::remove_cvref_t<T>;
    using key_t = typename map_t::key_type;
    using mapped_t = typename map_t::mapped_type;

    static auto deserialize(deserializer_t& deserializer, T& value)
        -> std::expected<void, error_type> {
        static_assert(std::default_initializable<key_t>,
                      "bincode map deserialization requires default-constructible key_type");
        static_assert(std::default_initializable<mapped_t>,
                      "bincode map deserialization requires default-constructible mapped_type");
        static_assert(eventide::detail::map_insertable<map_t, key_t, mapped_t>,
                      "bincode map deserialization requires insertable map container");

        ETD_EXPECTED_TRY_V(auto length, deserializer.read_length_prefix());

        if constexpr(requires { value.clear(); }) {
            value.clear();
        }

        for(std::size_t i = 0; i < length; ++i) {
            key_t key{};
            ETD_EXPECTED_TRY(serde::deserialize(deserializer, key));

            mapped_t mapped{};
            ETD_EXPECTED_TRY(serde::deserialize(deserializer, mapped));

            eventide::detail::insert_map_entry(value, std::move(key), std::move(mapped));
        }

        return {};
    }
};

}  // namespace eventide::serde
