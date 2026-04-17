#pragma once

#include <charconv>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "kota/support/naming.h"
#include "kota/support/type_traits.h"
#include "kota/meta/enum.h"

namespace kota::codec {

namespace spelling {

namespace detail {

template <typename Mapped>
std::string to_string_storage(Mapped&& mapped) {
    using mapped_t = std::remove_cvref_t<Mapped>;
    if constexpr(std::same_as<mapped_t, std::string>) {
        return std::forward<Mapped>(mapped);
    } else if constexpr(std::convertible_to<Mapped, std::string_view>) {
        return std::string(static_cast<std::string_view>(mapped));
    } else {
        static_assert(dependent_false<mapped_t>,
                      "rename policy must return std::string or string-like value");
        return {};
    }
}

template <std::signed_integral T>
std::optional<T> parse_signed(std::string_view text) {
    T value{};
    auto [ptr, err] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if(err != std::errc() || ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

template <std::unsigned_integral T>
std::optional<T> parse_unsigned(std::string_view text) {
    T value{};
    auto [ptr, err] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if(err != std::errc() || ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

template <std::floating_point T>
std::optional<T> parse_floating(std::string_view text) {
    T value{};
    auto [ptr, err] = std::from_chars(text.data(), text.data() + text.size(), value);
    if(err != std::errc() || ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

}  // namespace detail

namespace rename_policy {

using identity = naming::rename_policy::identity;
using lower_snake = naming::rename_policy::lower_snake;
using lower_camel = naming::rename_policy::lower_camel;
using upper_camel = naming::rename_policy::upper_camel;
using upper_snake = naming::rename_policy::upper_snake;
using upper_case = naming::rename_policy::upper_case;

}  // namespace rename_policy

template <typename Policy>
std::string apply_rename_policy(bool is_serialize, std::string_view value) {
    if constexpr(requires(Policy policy) { policy(is_serialize, value); }) {
        return detail::to_string_storage(Policy{}(is_serialize, value));
    } else {
        static_assert(dependent_false<Policy>,
                      "rename policy must support operator()(bool, std::string_view)");
    }
}

template <typename E, typename Policy = rename_policy::lower_camel>
std::string map_enum_to_string(E value) {
    static_assert(std::is_enum_v<E>, "map_enum_to_string requires an enum type");
    return apply_rename_policy<Policy>(true, meta::enum_name(value));
}

template <typename E, typename Policy = rename_policy::lower_camel>
auto enum_strings() -> const std::vector<std::string>& {
    static_assert(std::is_enum_v<E>, "enum_strings requires an enum type");
    const static auto names = [] {
        std::vector<std::string> values;
        values.reserve(meta::reflection<E>::member_values.size());
        for(const auto value: meta::reflection<E>::member_values) {
            values.push_back(map_enum_to_string<E, Policy>(value));
        }
        return values;
    }();
    return names;
}

template <typename E, typename Policy = rename_policy::lower_camel>
constexpr std::optional<E> map_string_to_enum(std::string_view value) {
    static_assert(std::is_enum_v<E>, "map_string_to_enum requires an enum type");
    auto mapped = apply_rename_policy<Policy>(false, value);
    auto try_parse = [](std::string_view candidate) -> std::optional<E> {
        if(auto parsed = meta::enum_value<E>(candidate)) {
            return parsed;
        }

        // Keyword-safe fallback for generated enum members like `Delete_`/`Import_`.
        auto keyword_suffixed = std::string(candidate);
        keyword_suffixed.push_back('_');
        if(auto parsed = meta::enum_value<E>(keyword_suffixed)) {
            return parsed;
        }

        if(!candidate.empty() && naming::is_digit(candidate.front())) {
            auto underscored = std::string("_") + std::string(candidate);
            if(auto parsed = meta::enum_value<E>(underscored)) {
                return parsed;
            }

            auto value_prefixed = std::string("V") + std::string(candidate);
            if(auto parsed = meta::enum_value<E>(value_prefixed)) {
                return parsed;
            }
        }

        return std::nullopt;
    };

    if(auto parsed = try_parse(mapped)) {
        return parsed;
    }

    auto lower_camel = naming::snake_to_camel(mapped, false);
    if(auto parsed = try_parse(lower_camel)) {
        return parsed;
    }

    auto upper_camel = naming::snake_to_camel(mapped, true);
    if(auto parsed = try_parse(upper_camel)) {
        return parsed;
    }

    return std::nullopt;
}

template <typename Key>
std::string map_key_to_string(const Key& key) {
    using key_t = std::remove_cvref_t<Key>;

    if constexpr(std::same_as<key_t, char*>) {
        return key == nullptr ? std::string{} : std::string(key);
    } else if constexpr(std::same_as<key_t, const char*>) {
        return key == nullptr ? std::string{} : std::string(key);
    } else if constexpr(std::is_array_v<key_t> &&
                        (std::same_as<std::remove_extent_t<key_t>, char> ||
                         std::same_as<std::remove_extent_t<key_t>, const char>)) {
        return std::string(std::string_view(key));
    } else if constexpr(std::same_as<key_t, std::string>) {
        return key;
    } else if constexpr(std::convertible_to<const key_t&, std::string_view>) {
        return std::string(std::string_view(key));
    } else if constexpr(std::same_as<key_t, char>) {
        return std::string(1, key);
    } else if constexpr(std::same_as<key_t, bool>) {
        return key ? std::string("true") : std::string("false");
    } else if constexpr(std::is_enum_v<key_t>) {
        using underlying_t = std::underlying_type_t<key_t>;
        if constexpr(std::signed_integral<underlying_t>) {
            return std::to_string(static_cast<std::int64_t>(static_cast<underlying_t>(key)));
        } else {
            return std::to_string(static_cast<std::uint64_t>(static_cast<underlying_t>(key)));
        }
    } else if constexpr(std::signed_integral<key_t>) {
        return std::to_string(static_cast<std::int64_t>(key));
    } else if constexpr(std::unsigned_integral<key_t>) {
        return std::to_string(static_cast<std::uint64_t>(key));
    } else if constexpr(std::floating_point<key_t>) {
        return std::to_string(static_cast<double>(key));
    } else {
        static_assert(dependent_false<key_t>,
                      "Unsupported map key type for serializer key mapping");
    }
}

template <typename Key>
concept parseable_map_key =
    std::constructible_from<Key, std::string_view> || std::same_as<Key, bool> ||
    std::same_as<Key, char> || std::is_enum_v<Key> || std::signed_integral<Key> ||
    std::unsigned_integral<Key> || std::floating_point<Key>;

template <typename Key>
std::optional<Key> parse_map_key(std::string_view key_text) {
    using key_t = std::remove_cvref_t<Key>;

    if constexpr(std::constructible_from<key_t, std::string_view>) {
        return key_t(key_text);
    } else if constexpr(std::same_as<key_t, bool>) {
        if(key_text == "true" || key_text == "1") {
            return true;
        }
        if(key_text == "false" || key_text == "0") {
            return false;
        }
        return std::nullopt;
    } else if constexpr(std::same_as<key_t, char>) {
        if(key_text.size() != 1) {
            return std::nullopt;
        }
        return key_text.front();
    } else if constexpr(std::is_enum_v<key_t>) {
        using underlying_t = std::underlying_type_t<key_t>;
        auto parsed = parse_map_key<underlying_t>(key_text);
        if(!parsed) {
            return std::nullopt;
        }
        return static_cast<key_t>(*parsed);
    } else if constexpr(std::signed_integral<key_t>) {
        return detail::parse_signed<key_t>(key_text);
    } else if constexpr(std::unsigned_integral<key_t>) {
        return detail::parse_unsigned<key_t>(key_text);
    } else if constexpr(std::floating_point<key_t>) {
        return detail::parse_floating<key_t>(key_text);
    } else {
        static_assert(dependent_false<key_t>,
                      "Unsupported map key type for deserializer key parsing");
    }
}

}  // namespace spelling

namespace rename_policy {

using identity = spelling::rename_policy::identity;
using lower_snake = spelling::rename_policy::lower_snake;
using lower_camel = spelling::rename_policy::lower_camel;
using upper_camel = spelling::rename_policy::upper_camel;
using upper_snake = spelling::rename_policy::upper_snake;
using upper_case = spelling::rename_policy::upper_case;

}  // namespace rename_policy

}  // namespace kota::codec
