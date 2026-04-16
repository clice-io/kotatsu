#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "eventide/common/expected_try.h"
#include "eventide/reflection/annotation.h"
#include "eventide/reflection/attrs.h"
#include "eventide/reflection/struct.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/spelling.h"
#include "eventide/serde/serde/utils/apply_behavior.h"
#include "eventide/serde/serde/utils/common.h"
#include "eventide/serde/serde/utils/fwd.h"

namespace eventide::serde::detail {

template <typename Config, typename E, typename SerializeStruct, typename Field>
constexpr auto serialize_struct_field(SerializeStruct& s_struct, Field field)
    -> std::expected<void, E> {
    using field_t = typename std::remove_cvref_t<decltype(field)>::type;

    if constexpr(!refl::annotated_type<field_t>) {
        std::string scratch;
        auto mapped_name = config::apply_field_rename<Config>(true, field.name(), scratch);
        return s_struct.serialize_field(mapped_name, field.value());
    } else {
        using attrs_t = typename std::remove_cvref_t<field_t>::attrs;
        auto&& value = refl::annotated_value(field.value());
        using value_t = std::remove_cvref_t<decltype(value)>;

        // Schema: skip — exclude field entirely
        if constexpr(tuple_has_v<attrs_t, refl::attrs::skip>) {
            return std::expected<void, E>{};
        }
        // Schema: flatten — inline nested struct fields
        else if constexpr(tuple_has_v<attrs_t, refl::attrs::flatten>) {
            static_assert(refl::reflectable_class<value_t>,
                          "schema::flatten requires a reflectable class field type");
            std::expected<void, E> nested_result;
            refl::for_each(value, [&](auto nested_field) {
                auto status = serialize_struct_field<Config, E>(s_struct, nested_field);
                if(!status) {
                    nested_result = std::unexpected(status.error());
                    return false;
                }
                return true;
            });
            return nested_result;
        } else {
            // Resolve effective field name
            std::string scratch;
            std::string_view effective_name;
            if constexpr(tuple_any_of_v<attrs_t, refl::is_rename_attr>) {
                using rename_attr = tuple_find_t<attrs_t, refl::is_rename_attr>;
                effective_name = rename_attr::name;
            } else {
                effective_name = config::apply_field_rename<Config>(true, field.name(), scratch);
            }

            // Behavior: skip_if — conditionally skip
            if constexpr(tuple_has_spec_v<attrs_t, refl::behavior::skip_if>) {
                using Pred =
                    typename tuple_find_spec_t<attrs_t, refl::behavior::skip_if>::predicate;
                if(refl::evaluate_skip_predicate<Pred>(value, true)) {
                    return std::expected<void, E>{};
                }
            }

            // Behavior: with/as/enum_string — delegate to apply_serialize_behavior
            if constexpr(tuple_count_of_v<attrs_t, refl::is_behavior_provider> > 0) {
                return *detail::apply_serialize_behavior<attrs_t, value_t, E>(
                    value,
                    [&](const auto& v) { return s_struct.serialize_field(effective_name, v); },
                    [&](auto tag, const auto& v) {
                        using Adapter = typename decltype(tag)::type;
                        return Adapter::serialize_field(s_struct, effective_name, v);
                    });
            }
            // Default: serialize field with its value
            else {
                // For tagged variants, preserve annotation so serialize() sees tagging attrs
                if constexpr(is_specialization_of<std::variant, value_t> &&
                             tuple_any_of_v<attrs_t, refl::is_tagged_attr>) {
                    return s_struct.serialize_field(effective_name, field.value());
                } else {
                    return s_struct.serialize_field(effective_name, value);
                }
            }
        }
    }
}

template <typename Config, typename E, typename DeserializeStruct, typename Field>
constexpr auto deserialize_struct_field(DeserializeStruct& d_struct,
                                        std::string_view key_name,
                                        Field field) -> std::expected<bool, E> {
    using field_t = typename std::remove_cvref_t<decltype(field)>::type;

    if constexpr(!refl::annotated_type<field_t>) {
        std::string scratch;
        auto mapped_name = config::apply_field_rename<Config>(true, field.name(), scratch);
        if(mapped_name != key_name) {
            return false;
        }
        ETD_EXPECTED_TRY(d_struct.deserialize_value(field.value()));
        return true;
    } else {
        using attrs_t = typename std::remove_cvref_t<field_t>::attrs;
        auto&& value = refl::annotated_value(field.value());
        using value_t = std::remove_cvref_t<decltype(value)>;

        // Schema: skip — never match
        if constexpr(tuple_has_v<attrs_t, refl::attrs::skip>) {
            return false;
        }
        // Schema: flatten — recurse into nested struct fields
        else if constexpr(tuple_has_v<attrs_t, refl::attrs::flatten>) {
            static_assert(refl::reflectable_class<value_t>,
                          "schema::flatten requires a reflectable class field type");
            bool matched = false;
            std::expected<void, E> nested_error;
            refl::for_each(value, [&](auto nested_field) {
                auto status = deserialize_struct_field<Config, E>(d_struct, key_name, nested_field);
                if(!status) {
                    nested_error = std::unexpected(status.error());
                    return false;
                }
                if(*status) {
                    matched = true;
                    return false;
                }
                return true;
            });
            if(!nested_error) {
                return std::unexpected(nested_error.error());
            }
            return matched;
        } else {
            // Resolve effective field name
            std::string scratch;
            std::string_view effective_name;
            if constexpr(tuple_any_of_v<attrs_t, refl::is_rename_attr>) {
                using rename_attr = tuple_find_t<attrs_t, refl::is_rename_attr>;
                effective_name = rename_attr::name;
            } else {
                effective_name = config::apply_field_rename<Config>(true, field.name(), scratch);
            }

            // Check name match: canonical name + aliases
            bool name_matched = (key_name == effective_name);
            if constexpr(tuple_any_of_v<attrs_t, refl::is_alias_attr>) {
                if(!name_matched) {
                    using alias_attr = tuple_find_t<attrs_t, refl::is_alias_attr>;
                    for(auto alias_name: alias_attr::names) {
                        if(alias_name == key_name) {
                            name_matched = true;
                            break;
                        }
                    }
                }
            }

            if(!name_matched) {
                return false;
            }

            // Behavior: skip_if — conditionally skip deserialization
            if constexpr(tuple_has_spec_v<attrs_t, refl::behavior::skip_if>) {
                using Pred =
                    typename tuple_find_spec_t<attrs_t, refl::behavior::skip_if>::predicate;
                if(refl::evaluate_skip_predicate<Pred>(value, false)) {
                    ETD_EXPECTED_TRY(d_struct.skip_value());
                    return true;
                }
            }

            // Behavior: with/as/enum_string — delegate to apply_deserialize_behavior
            if constexpr(tuple_count_of_v<attrs_t, refl::is_behavior_provider> > 0) {
                ETD_EXPECTED_TRY((*detail::apply_deserialize_behavior<attrs_t, value_t, E>(
                    value,
                    [&](auto& v) { return d_struct.deserialize_value(v); },
                    [&](auto tag, auto& v) -> std::expected<void, E> {
                        using Adapter = typename decltype(tag)::type;
                        return Adapter::deserialize_field(d_struct, v);
                    })));
                return true;
            }
            // Default: deserialize value directly
            else {
                // For tagged variants, preserve annotation so deserialize() sees tagging attrs
                if constexpr(is_specialization_of<std::variant, value_t> &&
                             tuple_any_of_v<attrs_t, refl::is_tagged_attr>) {
                    ETD_EXPECTED_TRY(d_struct.deserialize_value(field.value()));
                    return true;
                } else {
                    ETD_EXPECTED_TRY(d_struct.deserialize_value(value));
                    return true;
                }
            }
        }
    }
}

}  // namespace eventide::serde::detail
