#pragma once

#include <expected>
#include <format>
#include <optional>
#include <string>
#include <type_traits>

#include "eventide/reflection/attrs.h"
#include "eventide/serde/serde/spelling.h"

namespace eventide::serde::detail {

/// Serialize-side behavior attribute dispatch.
///
/// Checks attrs_t for `with`/`as`/`enum_string` and handles them:
///   - with:        calls with_fn(type_identity<Adapter>{}, value)
///   - as:          converts value to Target, then calls emit(converted)
///   - enum_string: maps enum to string, then calls emit(string)
///
/// Returns std::nullopt if no behavior attribute matched (caller should use default path).
template <typename attrs_t, typename value_t, typename E, typename Emitter, typename WithFn>
constexpr auto apply_serialize_behavior(const value_t& value, Emitter&& emit, WithFn&& with_fn)
    -> std::optional<decltype(emit(value))> {
    if constexpr(tuple_has_spec_v<attrs_t, refl::behavior::with>) {
        using Adapter = typename tuple_find_spec_t<attrs_t, refl::behavior::with>::adapter;
        return with_fn(std::type_identity<Adapter>{}, value);
    } else if constexpr(tuple_has_spec_v<attrs_t, refl::behavior::as>) {
        using Target = typename tuple_find_spec_t<attrs_t, refl::behavior::as>::target;
        static_assert(
            std::is_constructible_v<Target, const value_t&>,
            "behavior::as<Target> requires Target to be constructible from the value type");
        Target converted(value);
        return emit(converted);
    } else if constexpr(tuple_has_spec_v<attrs_t, refl::behavior::enum_string>) {
        using Policy = typename tuple_find_spec_t<attrs_t, refl::behavior::enum_string>::policy;
        static_assert(std::is_enum_v<value_t>, "behavior::enum_string requires an enum type");
        auto enum_text = spelling::map_enum_to_string<value_t, Policy>(value);
        return emit(enum_text);
    } else {
        return std::nullopt;
    }
}

/// Deserialize-side behavior attribute dispatch.
///
/// Checks attrs_t for `with`/`as`/`enum_string` and handles them:
///   - with:        calls with_fn(type_identity<Adapter>{}, value)
///   - as:          deserializes into Target via read(temp), then converts back
///   - enum_string: reads string via read(str), then maps to enum
///
/// Returns std::nullopt if no behavior attribute matched (caller should use default path).
template <typename attrs_t, typename value_t, typename E, typename Reader, typename WithFn>
constexpr auto apply_deserialize_behavior(value_t& value, Reader&& read, WithFn&& with_fn)
    -> std::optional<std::expected<void, E>> {
    if constexpr(tuple_has_spec_v<attrs_t, refl::behavior::with>) {
        using Adapter = typename tuple_find_spec_t<attrs_t, refl::behavior::with>::adapter;
        return with_fn(std::type_identity<Adapter>{}, value);
    } else if constexpr(tuple_has_spec_v<attrs_t, refl::behavior::as>) {
        using Target = typename tuple_find_spec_t<attrs_t, refl::behavior::as>::target;
        static_assert(
            std::is_constructible_v<value_t, Target&&>,
            "behavior::as<Target> requires the value type to be constructible from Target");
        Target temp{};
        auto status = read(temp);
        if(!status) {
            return std::expected<void, E>(std::unexpected(status.error()));
        }
        value = value_t(std::move(temp));
        return std::expected<void, E>{};
    } else if constexpr(tuple_has_spec_v<attrs_t, refl::behavior::enum_string>) {
        using Policy = typename tuple_find_spec_t<attrs_t, refl::behavior::enum_string>::policy;
        static_assert(std::is_enum_v<value_t>, "behavior::enum_string requires an enum type");
        std::string enum_text;
        auto status = read(enum_text);
        if(!status) {
            return std::expected<void, E>(std::unexpected(status.error()));
        }
        auto parsed = spelling::map_string_to_enum<value_t, Policy>(enum_text);
        if(parsed.has_value()) {
            value = *parsed;
            return std::expected<void, E>{};
        } else {
            return std::expected<void, E>(std::unexpected(
                E::custom(std::format("unknown enum string value '{}'", enum_text))));
        }
    } else {
        return std::nullopt;
    }
}

}  // namespace eventide::serde::detail
