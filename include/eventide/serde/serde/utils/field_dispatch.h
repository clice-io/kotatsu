#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "eventide/common/expected_try.h"
#include "eventide/reflection/struct.h"
#include "eventide/serde/serde/annotation.h"
#include "eventide/serde/serde/attrs.h"
#include "eventide/serde/serde/attrs/behavior.h"
#include "eventide/serde/serde/attrs/schema.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/spelling.h"
#include "eventide/serde/serde/utils/common.h"
#include "eventide/serde/serde/utils/fwd.h"

namespace eventide::serde::detail {

template <typename Config, typename E, typename SerializeStruct, typename Field>
constexpr auto serialize_struct_field(SerializeStruct& s_struct, Field field)
    -> std::expected<void, E> {
    using field_t = typename std::remove_cvref_t<decltype(field)>::type;

    if constexpr(!annotated_type<field_t>) {
        std::string scratch;
        auto mapped_name = config::apply_field_rename<Config>(true, field.name(), scratch);
        return s_struct.serialize_field(mapped_name, field.value());
    } else {
        using attrs_t = typename std::remove_cvref_t<field_t>::attrs;
        auto&& value = annotated_value(field.value());
        using value_t = std::remove_cvref_t<decltype(value)>;

        // Schema: skip — exclude field entirely
        if constexpr(detail::tuple_has_v<attrs_t, schema::skip>) {
            return std::expected<void, E>{};
        }
        // Schema: flatten — inline nested struct fields
        else if constexpr(detail::tuple_has_v<attrs_t, schema::flatten>) {
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
            if constexpr(detail::tuple_any_of_v<attrs_t, is_rename_attr>) {
                using rename_attr = detail::tuple_find_t<attrs_t, is_rename_attr>;
                effective_name = rename_attr::name;
            } else {
                effective_name = config::apply_field_rename<Config>(true, field.name(), scratch);
            }

            // Behavior: skip_if — conditionally skip
            if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::skip_if>) {
                using Pred =
                    typename detail::tuple_find_spec_t<attrs_t, behavior::skip_if>::predicate;
                if(evaluate_skip_predicate<Pred>(value, true)) {
                    return std::expected<void, E>{};
                }
            }

            // Behavior: with<Adapter> — adapter-based serialization
            if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::with>) {
                using Adapter =
                    typename detail::tuple_find_spec_t<attrs_t, behavior::with>::adapter;
                return Adapter::serialize_field(s_struct, effective_name, value);
            }
            // Behavior: as<Target> — type conversion before serialization
            else if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::as>) {
                using Target = typename detail::tuple_find_spec_t<attrs_t, behavior::as>::target;
                static_assert(
                    std::is_constructible_v<Target, const value_t&>,
                    "behavior::as<Target> requires Target to be constructible from the field type");
                Target converted(value);
                return s_struct.serialize_field(effective_name, converted);
            }
            // Behavior: enum_string — serialize enum as string
            else if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::enum_string>) {
                using Policy =
                    typename detail::tuple_find_spec_t<attrs_t, behavior::enum_string>::policy;
                static_assert(std::is_enum_v<value_t>,
                              "behavior::enum_string requires an enum field type");
                auto enum_text = spelling::map_enum_to_string<value_t, Policy>(value);
                return s_struct.serialize_field(effective_name, enum_text);
            }
            // Default: serialize field with its value
            else {
                // For tagged variants, preserve annotation so serialize() sees tagging attrs
                if constexpr(is_specialization_of<std::variant, value_t> &&
                             detail::tuple_any_of_v<attrs_t, is_tagged_attr>) {
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

    if constexpr(!annotated_type<field_t>) {
        std::string scratch;
        auto mapped_name = config::apply_field_rename<Config>(true, field.name(), scratch);
        if(mapped_name != key_name) {
            return false;
        }
        ET_EXPECTED_TRY(d_struct.deserialize_value(field.value()));
        return true;
    } else {
        using attrs_t = typename std::remove_cvref_t<field_t>::attrs;
        auto&& value = annotated_value(field.value());
        using value_t = std::remove_cvref_t<decltype(value)>;

        // Schema: skip — never match
        if constexpr(detail::tuple_has_v<attrs_t, schema::skip>) {
            return false;
        }
        // Schema: flatten — recurse into nested struct fields
        else if constexpr(detail::tuple_has_v<attrs_t, schema::flatten>) {
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
            if constexpr(detail::tuple_any_of_v<attrs_t, is_rename_attr>) {
                using rename_attr = detail::tuple_find_t<attrs_t, is_rename_attr>;
                effective_name = rename_attr::name;
            } else {
                effective_name = config::apply_field_rename<Config>(true, field.name(), scratch);
            }

            // Check name match: canonical name + aliases
            bool name_matched = (key_name == effective_name);
            if constexpr(detail::tuple_any_of_v<attrs_t, is_alias_attr>) {
                if(!name_matched) {
                    using alias_attr = detail::tuple_find_t<attrs_t, is_alias_attr>;
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
            if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::skip_if>) {
                using Pred =
                    typename detail::tuple_find_spec_t<attrs_t, behavior::skip_if>::predicate;
                if(evaluate_skip_predicate<Pred>(value, false)) {
                    ET_EXPECTED_TRY(d_struct.skip_value());
                    return true;
                }
            }

            // Behavior: with<Adapter> — adapter-based deserialization
            if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::with>) {
                using Adapter =
                    typename detail::tuple_find_spec_t<attrs_t, behavior::with>::adapter;
                ET_EXPECTED_TRY(Adapter::deserialize_field(d_struct, value));
                return true;
            }
            // Behavior: as<Target> — deserialize as Target, then convert back
            else if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::as>) {
                using Target = typename detail::tuple_find_spec_t<attrs_t, behavior::as>::target;
                static_assert(
                    std::is_constructible_v<value_t, Target&&>,
                    "behavior::as<Target> requires the field type to be constructible from Target");
                Target temp{};
                ET_EXPECTED_TRY(d_struct.deserialize_value(temp));
                value = value_t(std::move(temp));
                return true;
            }
            // Behavior: enum_string — deserialize string then map to enum
            else if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::enum_string>) {
                using Policy =
                    typename detail::tuple_find_spec_t<attrs_t, behavior::enum_string>::policy;
                static_assert(std::is_enum_v<value_t>,
                              "behavior::enum_string requires an enum field type");
                std::string enum_text;
                ET_EXPECTED_TRY(d_struct.deserialize_value(enum_text));
                auto parsed = spelling::map_string_to_enum<value_t, Policy>(enum_text);
                if(parsed.has_value()) {
                    value = *parsed;
                    return true;
                } else {
                    return std::unexpected(E::type_mismatch);
                }
            }
            // Default: deserialize value directly
            else {
                // For tagged variants, preserve annotation so deserialize() sees tagging attrs
                if constexpr(is_specialization_of<std::variant, value_t> &&
                             detail::tuple_any_of_v<attrs_t, is_tagged_attr>) {
                    ET_EXPECTED_TRY(d_struct.deserialize_value(field.value()));
                    return true;
                } else {
                    ET_EXPECTED_TRY(d_struct.deserialize_value(value));
                    return true;
                }
            }
        }
    }
}

}  // namespace eventide::serde::detail
