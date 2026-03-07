#pragma once

#include <cstdint>
#include <expected>
#include <type_traits>

#include "eventide/reflection/struct.h"
#include "eventide/serde/serde/annotation.h"
#include "eventide/serde/serde/serde.h"
#include "eventide/serde/serde/traits.h"

namespace eventide::serde {

/// Bitmask of data-model type categories.
/// Backends map their format-specific "kind" enums to these bits;
/// the shared `expected_type_hints<T>()` maps C++ types to them.
enum class type_hint : std::uint8_t {
    null_like = 1 << 0,
    boolean = 1 << 1,
    integer = 1 << 2,
    floating = 1 << 3,
    string = 1 << 4,
    array = 1 << 5,
    object = 1 << 6,
    any = 0x7F,
};

constexpr type_hint operator|(type_hint a, type_hint b) noexcept {
    return static_cast<type_hint>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

constexpr bool has_any(type_hint set, type_hint flags) noexcept {
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(flags)) != 0;
}

/// Map a C++ type `T` to the set of data-model categories it can deserialize from.
template <typename T>
constexpr type_hint expected_type_hints() {
    using U = std::remove_cvref_t<T>;

    if constexpr(annotated_type<U>) {
        return expected_type_hints<typename U::annotated_type>();
    } else if constexpr(is_specialization_of<std::optional, U>) {
        return type_hint::null_like | expected_type_hints<typename U::value_type>();
    } else if constexpr(std::same_as<U, std::nullptr_t>) {
        return type_hint::null_like;
    } else if constexpr(std::same_as<U, std::monostate>) {
        return type_hint::null_like;
    } else if constexpr(bool_like<U>) {
        return type_hint::boolean;
    } else if constexpr(int_like<U> || uint_like<U>) {
        return type_hint::integer;
    } else if constexpr(floating_like<U>) {
        // floats accept both integer and floating sources
        return type_hint::integer | type_hint::floating;
    } else if constexpr(char_like<U> || std::same_as<U, std::string> ||
                        std::derived_from<U, std::string>) {
        return type_hint::string;
    } else if constexpr(std::same_as<U, std::vector<std::byte>>) {
        return type_hint::array;
    } else if constexpr(is_pair_v<U> || is_tuple_v<U>) {
        return type_hint::array;
    } else if constexpr(std::ranges::input_range<U>) {
        constexpr auto kind = format_kind<U>;
        if constexpr(kind == range_format::map) {
            return type_hint::object;
        } else if constexpr(kind == range_format::sequence || kind == range_format::set) {
            return type_hint::array;
        } else {
            return type_hint::any;
        }
    } else if constexpr(refl::reflectable_class<U>) {
        return type_hint::object;
    } else {
        return type_hint::any;
    }
}

/// Shared implementation of the probe-deserialize-finish pattern for variant candidates.
/// `D` must be constructible from `Source`.
template <typename D, typename Alt, typename Source, typename... Ts>
auto try_deserialize_variant_candidate(Source&& source, std::variant<Ts...>& value)
    -> std::expected<void, typename D::error_type> {
    Alt candidate{};
    D probe(std::forward<Source>(source));
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

/// Shared variant dispatch for DOM-like deserializers.
///
/// Given a source value and a type_hint describing its data-model category,
/// iterate all variant alternatives, check if the hint matches, and attempt
/// deserialization via the probe-deserialize-finish pattern.
///
/// D: the Deserializer type (must be constructible from Source)
/// Source: the captured value (e.g., json::ValueRef, const toml::node*)
/// hint: the serde::type_hint for the current value
/// mismatch_error: the error value to use when no alternative matches
template <typename D, typename Source, typename... Ts>
auto try_variant_dispatch(Source&& source,
                          type_hint hint,
                          std::variant<Ts...>& value,
                          typename D::error_type mismatch_error)
    -> std::expected<void, typename D::error_type> {
    static_assert((std::default_initializable<Ts> && ...),
                  "variant deserialization requires default-constructible alternatives");

    using error_type = typename D::error_type;

    bool matched = false;
    bool considered = false;
    error_type last_error = mismatch_error;

    auto try_alternative = [&](auto type_tag) {
        if(matched) {
            return;
        }

        using alt_t = typename decltype(type_tag)::type;
        if(!has_any(expected_type_hints<alt_t>(), hint)) {
            return;
        }

        considered = true;
        auto status =
            try_deserialize_variant_candidate<D, alt_t>(std::forward<Source>(source), value);
        if(status) {
            matched = true;
        } else {
            last_error = status.error();
        }
    };

    (try_alternative(std::type_identity<Ts>{}), ...);

    if(!matched) {
        return std::unexpected(considered ? last_error : mismatch_error);
    }
    return {};
}

}  // namespace eventide::serde
