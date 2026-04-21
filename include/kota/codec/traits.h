#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "kota/support/ranges.h"
#include "kota/support/type_traits.h"
#include "kota/meta/type_kind.h"

namespace kota::codec {

// Type-classification concepts — canonical definitions live in kota::meta
// (type_kind.h).  These aliases keep existing serde code compiling unchanged.

template <typename T>
concept null_like = meta::null_like<T>;

template <typename T>
concept bool_like = meta::bool_like<T>;

template <typename T>
concept int_like = meta::int_like<T>;

template <typename T>
concept uint_like = meta::uint_like<T>;

template <typename T>
concept floating_like = meta::floating_like<T>;

template <typename T>
concept char_like = meta::char_like<T>;

template <typename T>
concept str_like = meta::str_like<T>;

template <typename T>
concept bytes_like = meta::bytes_like<T>;

template <typename T>
constexpr inline bool is_pair_v = meta::is_pair_v<T>;

template <typename T>
constexpr inline bool is_tuple_v = meta::is_tuple_v<T>;

template <typename T>
concept tuple_like = meta::tuple_like<T>;

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

template <typename S, typename T = typename S::value_type, typename E = typename S::error_type>
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
                                    std::size_t count,
                                    const std::variant<int, std::string>& variant_value) {
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

        { s.begin_array(len) } -> result_as<void, E>;
        { s.end_array() } -> result_as<T, E>;

        { s.begin_object(count) } -> result_as<void, E>;
        { s.field(text) } -> result_as<void, E>;
        { s.end_object() } -> result_as<T, E>;
    };

template <typename D, typename E = typename D::error_type>
concept deserializer_like =
    serde_error_like<E> && requires(D& d,
                                    bool& b,
                                    char& c,
                                    std::int64_t& i64,
                                    std::uint64_t& u64,
                                    double& f64,
                                    std::string& text,
                                    std::vector<std::byte>& bytes,
                                    std::variant<int, std::string>& variant_value) {
        { d.deserialize_bool(b) } -> result_as<void, E>;
        { d.deserialize_int(i64) } -> result_as<void, E>;
        { d.deserialize_uint(u64) } -> result_as<void, E>;
        { d.deserialize_float(f64) } -> result_as<void, E>;
        { d.deserialize_char(c) } -> result_as<void, E>;
        { d.deserialize_str(text) } -> result_as<void, E>;
        { d.deserialize_bytes(bytes) } -> result_as<void, E>;

        { d.deserialize_none() } -> result_as<bool, E>;
        { d.deserialize_variant(variant_value) } -> result_as<void, E>;

        // Streaming object interface
        { d.begin_object() } -> result_as<void, E>;
        { d.end_object() } -> result_as<void, E>;
        { d.next_field() } -> result_as<std::optional<std::string_view>, E>;
        { d.skip_field_value() } -> result_as<void, E>;

        // Streaming array interface
        { d.begin_array() } -> result_as<void, E>;
        { d.next_element() } -> result_as<bool, E>;
        { d.end_array() } -> result_as<void, E>;
    };

}  // namespace kota::codec
