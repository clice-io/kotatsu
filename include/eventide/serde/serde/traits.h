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
#include "eventide/reflection/type_kind.h"

namespace eventide::serde {

// Type-classification concepts — canonical definitions live in eventide::refl
// (type_kind.h).  These aliases keep existing serde code compiling unchanged.

template <typename T>
concept null_like = refl::null_like<T>;

template <typename T>
concept bool_like = refl::bool_like<T>;

template <typename T>
concept int_like = refl::int_like<T>;

template <typename T>
concept uint_like = refl::uint_like<T>;

template <typename T>
concept floating_like = refl::floating_like<T>;

template <typename T>
concept char_like = refl::char_like<T>;

template <typename T>
concept str_like = refl::str_like<T>;

template <typename T>
concept bytes_like = refl::bytes_like<T>;

template <typename T>
constexpr inline bool is_pair_v = refl::is_pair_v<T>;

template <typename T>
constexpr inline bool is_tuple_v = refl::is_tuple_v<T>;

template <typename T>
concept tuple_like = refl::tuple_like<T>;

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
