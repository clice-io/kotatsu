#pragma once

#include <concepts>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

#include "spelling.h"
#include "kota/meta/type_info.h"
#include "kota/codec/detail/error_context.h"

namespace kota::codec::config {

using default_config = meta::default_config;

/// Extract the config type from a serializer/deserializer.
/// Falls back to default_config if S::config_type is not defined.
template <typename S>
struct config_of_impl {
    using type = default_config;
};

template <typename S>
    requires requires { typename S::config_type; }
struct config_of_impl<S> {
    using type = typename S::config_type;
};

template <typename S>
using config_of = typename config_of_impl<S>::type;

/// Apply field rename policy from Config.
/// If Config::field_rename exists, uses it; otherwise returns value unchanged.
template <typename Config>
inline std::string_view apply_field_rename(bool is_serialize,
                                           std::string_view value,
                                           std::string& scratch) {
    if constexpr(requires { typename Config::field_rename; }) {
        scratch = spelling::apply_rename_policy<typename Config::field_rename>(is_serialize, value);
        return scratch;
    } else {
        return value;
    }
}

/// Apply enum rename policy from Config.
/// If Config::enum_rename exists, uses it; otherwise returns value unchanged.
template <typename Config>
inline std::string_view apply_enum_rename(bool is_serialize,
                                          std::string_view value,
                                          std::string& scratch) {
    if constexpr(requires { typename Config::enum_rename; }) {
        scratch = spelling::apply_rename_policy<typename Config::enum_rename>(is_serialize, value);
        return scratch;
    } else {
        return value;
    }
}

/// Compile-time trait to detect whether error path collection is enabled.
/// Defaults to true if Config::collect_error_path is not defined.
template <typename Config>
constexpr bool config_collect_error_path_v = [] {
    if constexpr(requires {
                     { Config::collect_error_path } -> std::convertible_to<bool>;
                 }) {
        return Config::collect_error_path;
    } else {
        return true;
    }
}();

/// Convenience functions for visitors to report errors via the common context.
/// Each checks the config before writing to the thread-local error context.

template <typename Config>
inline void error_set_missing_field(std::string_view field_name) {
    if constexpr(config_collect_error_path_v<Config>) {
        detail::thread_error_context().set_message(
            std::format("missing required field '{}'", field_name));
    }
}

template <typename Config>
inline void error_set_unknown_field(std::string_view field_name) {
    if constexpr(config_collect_error_path_v<Config>) {
        detail::thread_error_context().set_message(std::format("unknown field '{}'", field_name));
    }
}

template <typename Config>
inline void error_set_unknown_enum(std::string_view value) {
    if constexpr(config_collect_error_path_v<Config>) {
        detail::thread_error_context().set_message(
            std::format("unknown enum string value '{}'", value));
    }
}

template <typename Config>
inline void error_prepend_field(std::string_view name) {
    if constexpr(config_collect_error_path_v<Config>) {
        detail::thread_error_context().prepend_field(name);
    }
}

template <typename Config>
inline void error_prepend_index(std::size_t index) {
    if constexpr(config_collect_error_path_v<Config>) {
        detail::thread_error_context().prepend_index(index);
    }
}

}  // namespace kota::codec::config
