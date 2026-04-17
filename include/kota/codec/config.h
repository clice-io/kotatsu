#pragma once

#include <string>
#include <string_view>

#include "spelling.h"
#include "kota/meta/type_info.h"

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

}  // namespace kota::codec::config
