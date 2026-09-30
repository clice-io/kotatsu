#pragma once

#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kota/support/naming.h"
#include "kota/meta/enum.h"

/// Enumerators spelled through a rename policy (naming::rename_policy): the names a CLI or a
/// protocol shows for an enum, and the enumerator a spelling names.
namespace kota::meta {

/// `value` renamed by `Policy`, whose call operator takes whether the name is being
/// serialized and the name, and returns a std::string or anything convertible to a
/// std::string_view.
template <typename Policy>
    requires requires(Policy policy, bool is_serialize, std::string_view value) {
        { policy(is_serialize, value) } -> std::convertible_to<std::string_view>;
    }
std::string apply_rename_policy(bool is_serialize, std::string_view value) {
    return std::string(Policy{}(is_serialize, value));
}

/// The name of enumerator `value`, renamed by `Policy`.
template <enum_type E, typename Policy = naming::rename_policy::lower_camel>
std::string map_enum_to_string(E value) {
    return apply_rename_policy<Policy>(true, enum_name(value));
}

/// The renamed names of E's enumerators, in declaration order.
template <enum_type E, typename Policy = naming::rename_policy::lower_camel>
auto enum_strings() -> const std::vector<std::string>& {
    const static auto names = [] {
        std::vector<std::string> values;
        values.reserve(reflection<E>::member_values.size());
        for(const auto value: reflection<E>::member_values) {
            values.push_back(map_enum_to_string<E, Policy>(value));
        }
        return values;
    }();
    return names;
}

/// The enumerator `value` names: renamed back by `Policy`, then as camel case, each also with
/// the trailing `_` of a keyword-like enumerator (`Delete_`) or, for one that starts with a
/// digit, the leading `_` or `V` that makes it an identifier (`_2d`, `V3`); failing those, the
/// enumerator whose renamed name it is (`HTTPServer` for `httpServer`).
template <enum_type E, typename Policy = naming::rename_policy::lower_camel>
std::optional<E> map_string_to_enum(std::string_view value) {
    auto try_parse = [](std::string_view candidate) -> std::optional<E> {
        if(auto parsed = enum_value<E>(candidate)) {
            return parsed;
        }
        if(auto parsed = enum_value<E>(std::string(candidate) + '_')) {
            return parsed;
        }
        if(!candidate.empty() && naming::is_digit(candidate.front())) {
            if(auto parsed = enum_value<E>('_' + std::string(candidate))) {
                return parsed;
            }
            if(auto parsed = enum_value<E>('V' + std::string(candidate))) {
                return parsed;
            }
        }
        return std::nullopt;
    };

    const auto mapped = apply_rename_policy<Policy>(false, value);
    if(auto parsed = try_parse(mapped)) {
        return parsed;
    }
    if(auto parsed = try_parse(naming::snake_to_camel(mapped, false))) {
        return parsed;
    }
    if(auto parsed = try_parse(naming::snake_to_camel(mapped, true))) {
        return parsed;
    }
    const auto& names = enum_strings<E, Policy>();
    for(std::size_t i = 0; i < names.size(); ++i) {
        if(names[i] == value) {
            return reflection<E>::member_values[i];
        }
    }
    return std::nullopt;
}

}  // namespace kota::meta
