#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "eventide/common/meta.h"
#include "eventide/common/ranges.h"

namespace eventide::serde {

template <typename T>
concept null_like = is_one_of<T, std::nullptr_t, std::nullopt_t, std::monostate>;

template <typename T>
concept bool_like = std::same_as<T, bool>;

template <typename T>
concept int_like = is_one_of<T,
                             signed char,
                             short,
                             int,
                             long,
                             long long,
                             std::int8_t,
                             std::int16_t,
                             std::int32_t,
                             std::int64_t>;

template <typename T>
concept uint_like = is_one_of<T,
                              unsigned char,
                              unsigned short,
                              unsigned int,
                              unsigned long,
                              unsigned long long,
                              std::uint8_t,
                              std::uint16_t,
                              std::uint32_t,
                              std::uint64_t>;

template <typename T>
concept floating_like = is_one_of<T, float, double, long double>;

template <typename T>
concept char_like = std::same_as<T, char>;

template <typename T>
concept str_like = std::convertible_to<T, std::string_view>;

template <typename T>
concept bytes_like = std::convertible_to<T, std::span<const std::byte>>;

template <typename T>
constexpr inline bool is_pair_v = is_specialization_of<std::pair, T>;

template <typename T>
constexpr inline bool is_tuple_v = is_specialization_of<std::tuple, T>;

namespace detail {

template <typename T, std::size_t... Is>
consteval bool tuple_gettable_impl(std::index_sequence<Is...>) {
    return (requires(T& value) { std::get<Is>(value); } && ...);
}

template <typename T>
consteval bool tuple_gettable() {
    return tuple_gettable_impl<T>(std::make_index_sequence<std::tuple_size_v<T>>{});
}

}  // namespace detail

template <typename T>
concept tuple_like = requires { typename std::tuple_size<std::remove_cvref_t<T>>::type; } &&
                     detail::tuple_gettable<std::remove_cvref_t<T>>();

template <typename A, typename T, typename E>
concept result_as = std::same_as<A, std::expected<T, E>>;

/// Error concept: all serde error types must provide these named enumerators.
/// Modeled after Rust serde's `de::Error` / `ser::Error` traits — backends keep
/// their own concrete types but the core framework can construct common errors
/// without if-constexpr probing.
template <typename E>
concept serde_error_like = requires {
    { E::type_mismatch } -> std::convertible_to<E>;
    { E::number_out_of_range } -> std::convertible_to<E>;
    { E::invalid_state } -> std::convertible_to<E>;
};

template <typename S,
          typename T = typename S::value_type,
          typename E = typename S::error_type,
          typename SerializeSeq = typename S::SerializeSeq,
          typename SerializeTuple = typename S::SerializeTuple,
          typename SerializeMap = typename S::SerializeMap,
          typename SerializeStruct = typename S::SerializeStruct>
concept serializer_like =
    serde_error_like<E> && requires(S& s,
                                    bool b,
                                    char c,
                                    std::int64_t i,
                                    std::uint64_t u,
                                    double f,
                                    std::string_view text,
                                    std::span<const std::byte> bytes,
                                    std::optional<std::size_t> len,
                                    std::size_t tuple_len,
                                    const std::variant<int, std::string>& variant_value,
                                    const int& key,
                                    const int& value) {
        { s.serialize_bool(b) } -> result_as<T, E>;
        { s.serialize_int(i) } -> result_as<T, E>;
        { s.serialize_uint(u) } -> result_as<T, E>;
        { s.serialize_float(f) } -> result_as<T, E>;
        { s.serialize_char(c) } -> result_as<T, E>;
        { s.serialize_str(text) } -> result_as<T, E>;
        { s.serialize_bytes(bytes) } -> result_as<T, E>;

        { s.serialize_null() } -> result_as<T, E>;
        { s.serialize_some(i) } -> result_as<T, E>;
        { s.serialize_variant(variant_value) } -> result_as<T, E>;

        { s.serialize_seq(len) } -> result_as<SerializeSeq, E>;
        requires requires(SerializeSeq& s) {
            { s.serialize_element(value) } -> result_as<void, E>;
            { s.end() } -> result_as<T, E>;
        };

        { s.serialize_tuple(tuple_len) } -> result_as<SerializeTuple, E>;
        requires requires(SerializeTuple& s) {
            { s.serialize_element(value) } -> result_as<void, E>;
            { s.end() } -> result_as<T, E>;
        };

        { s.serialize_map(len) } -> result_as<SerializeMap, E>;
        requires requires(SerializeMap& s) {
            { s.serialize_entry(key, value) } -> result_as<void, E>;
            { s.end() } -> result_as<T, E>;
        };

        { s.serialize_struct(text, tuple_len) } -> result_as<SerializeStruct, E>;
        requires requires(SerializeStruct& s) {
            { s.serialize_field(text, value) } -> result_as<void, E>;
            { s.end() } -> result_as<T, E>;
        };
    };

template <typename D,
          typename E = typename D::error_type,
          typename DeserializeSeq = typename D::DeserializeSeq,
          typename DeserializeTuple = typename D::DeserializeTuple,
          typename DeserializeMap = typename D::DeserializeMap,
          typename DeserializeStruct = typename D::DeserializeStruct>
concept deserializer_like =
    serde_error_like<E> && requires(D& d,
                                    bool& b,
                                    char& c,
                                    std::int64_t& i64,
                                    std::uint64_t& u64,
                                    double& f64,
                                    std::string& text,
                                    std::vector<std::byte>& bytes,
                                    std::optional<std::size_t> len,
                                    std::size_t tuple_len,
                                    std::string_view name,
                                    std::variant<int, std::string>& variant_value,
                                    int& value) {
        { d.deserialize_bool(b) } -> result_as<void, E>;
        { d.deserialize_int(i64) } -> result_as<void, E>;
        { d.deserialize_uint(u64) } -> result_as<void, E>;
        { d.deserialize_float(f64) } -> result_as<void, E>;
        { d.deserialize_char(c) } -> result_as<void, E>;
        { d.deserialize_str(text) } -> result_as<void, E>;
        { d.deserialize_bytes(bytes) } -> result_as<void, E>;

        { d.deserialize_none() } -> result_as<bool, E>;
        { d.deserialize_variant(variant_value) } -> result_as<void, E>;

        { d.deserialize_seq(len) } -> result_as<DeserializeSeq, E>;
        requires requires(DeserializeSeq& s) {
            { s.has_next() } -> result_as<bool, E>;
            { s.deserialize_element(value) } -> result_as<void, E>;
            { s.skip_element() } -> result_as<void, E>;
            { s.end() } -> result_as<void, E>;
        };

        { d.deserialize_tuple(tuple_len) } -> result_as<DeserializeTuple, E>;
        requires requires(DeserializeTuple& s) {
            { s.deserialize_element(value) } -> result_as<void, E>;
            { s.skip_element() } -> result_as<void, E>;
            { s.end() } -> result_as<void, E>;
        };

        { d.deserialize_map(len) } -> result_as<DeserializeMap, E>;
        requires requires(DeserializeMap& s) {
            { s.next_key() } -> result_as<std::optional<std::string_view>, E>;
            { s.deserialize_value(value) } -> result_as<void, E>;
            { s.skip_value() } -> result_as<void, E>;
            { s.end() } -> result_as<void, E>;
        };

        { d.deserialize_struct(name, tuple_len) } -> result_as<DeserializeStruct, E>;
        requires requires(DeserializeStruct& s) {
            { s.next_key() } -> result_as<std::optional<std::string_view>, E>;
            { s.deserialize_value(value) } -> result_as<void, E>;
            { s.skip_value() } -> result_as<void, E>;
            { s.end() } -> result_as<void, E>;
        };
    };

}  // namespace eventide::serde
