#pragma once

#include <array>
#include <limits>
#include <memory>
#include <variant>

#include "annotation.h"
#include "attrs.h"
#include "config.h"
#include "traits.h"
#include "detail/backend_utils.h"
#include "eventide/common/ranges.h"
#include "eventide/reflection/enum.h"
#include "eventide/reflection/struct.h"
#include "eventide/serde/attrs/behavior.h"
#include "eventide/serde/attrs/schema.h"

namespace eventide::serde {

template <typename S, typename T>
struct serialize_traits;

template <typename D, typename T>
struct deserialize_traits;

template <serializer_like S, typename V, typename T, typename E>
constexpr auto serialize(S& s, const V& v) -> std::expected<T, E>;

template <deserializer_like D, typename V, typename E>
constexpr auto deserialize(D& d, V& v) -> std::expected<void, E>;

template <typename S, typename T, std::size_t N>
struct serialize_traits<S, std::array<T, N>> {
    using value_type = typename S::value_type;
    using error_type = typename S::error_type;

    static auto serialize(S& serializer, const std::array<T, N>& value)
        -> std::expected<value_type, error_type> {
        auto tuple = serializer.serialize_tuple(N);
        if(!tuple) {
            return std::unexpected(tuple.error());
        }

        for(const auto& element: value) {
            auto status = tuple->serialize_element(element);
            if(!status) {
                return std::unexpected(status.error());
            }
        }

        return tuple->end();
    }
};

template <typename D, typename T, std::size_t N>
struct deserialize_traits<D, std::array<T, N>> {
    using error_type = typename D::error_type;

    static auto deserialize(D& deserializer, std::array<T, N>& value)
        -> std::expected<void, error_type> {
        auto tuple = deserializer.deserialize_tuple(N);
        if(!tuple) {
            return std::unexpected(tuple.error());
        }

        for(auto& element: value) {
            auto status = tuple->deserialize_element(element);
            if(!status) {
                return std::unexpected(status.error());
            }
        }

        return tuple->end();
    }
};

namespace detail {

template <typename To, typename From>
constexpr bool integral_value_in_range(From value) {
    static_assert(std::is_integral_v<To>);
    static_assert(std::is_integral_v<From>);

    if constexpr(std::is_signed_v<To> == std::is_signed_v<From>) {
        using compare_t = std::conditional_t<(sizeof(From) > sizeof(To)), From, To>;
        return static_cast<compare_t>(value) >=
                   static_cast<compare_t>((std::numeric_limits<To>::lowest)()) &&
               static_cast<compare_t>(value) <=
                   static_cast<compare_t>((std::numeric_limits<To>::max)());
    } else if constexpr(std::is_signed_v<From>) {
        if(value < 0) {
            return false;
        }

        using from_unsigned_t = std::make_unsigned_t<From>;
        using to_unsigned_t = std::make_unsigned_t<To>;
        using compare_t = std::common_type_t<from_unsigned_t, to_unsigned_t>;
        return static_cast<compare_t>(static_cast<from_unsigned_t>(value)) <=
               static_cast<compare_t>((std::numeric_limits<To>::max)());
    } else {
        using from_unsigned_t = std::make_unsigned_t<From>;
        using to_unsigned_t = std::make_unsigned_t<To>;
        using compare_t = std::common_type_t<from_unsigned_t, to_unsigned_t>;
        return static_cast<compare_t>(static_cast<from_unsigned_t>(value)) <=
               static_cast<compare_t>((std::numeric_limits<To>::max)());
    }
}

// ── Direct dispatch for struct field serialization ─────────────────

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
                             (detail::tuple_any_of_v<attrs_t, is_externally_tagged_attr> ||
                              detail::tuple_any_of_v<attrs_t, is_adjacently_tagged_attr>)) {
                    return s_struct.serialize_field(effective_name, field.value());
                } else {
                    return s_struct.serialize_field(effective_name, value);
                }
            }
        }
    }
}

// ── Direct dispatch for struct field deserialization ────────────────

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
        auto result = d_struct.deserialize_value(field.value());
        if(!result) {
            return std::unexpected(result.error());
        }
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
                    auto skip_result = d_struct.skip_value();
                    if(!skip_result) {
                        return std::unexpected(skip_result.error());
                    }
                    return true;
                }
            }

            // Behavior: with<Adapter> — adapter-based deserialization
            if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::with>) {
                using Adapter =
                    typename detail::tuple_find_spec_t<attrs_t, behavior::with>::adapter;
                auto result = Adapter::deserialize_field(d_struct, value);
                if(!result) {
                    return std::unexpected(result.error());
                }
                return true;
            }
            // Behavior: enum_string — deserialize string then map to enum
            else if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::enum_string>) {
                using Policy =
                    typename detail::tuple_find_spec_t<attrs_t, behavior::enum_string>::policy;
                static_assert(std::is_enum_v<value_t>,
                              "behavior::enum_string requires an enum field type");
                std::string enum_text;
                auto result = d_struct.deserialize_value(enum_text);
                if(!result) {
                    return std::unexpected(result.error());
                }
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
                             (detail::tuple_any_of_v<attrs_t, is_externally_tagged_attr> ||
                              detail::tuple_any_of_v<attrs_t, is_adjacently_tagged_attr>)) {
                    auto result = d_struct.deserialize_value(field.value());
                    if(!result) {
                        return std::unexpected(result.error());
                    }
                    return true;
                } else {
                    auto result = d_struct.deserialize_value(value);
                    if(!result) {
                        return std::unexpected(result.error());
                    }
                    return true;
                }
            }
        }
    }
}

// ── Tagged variant serialization helpers ───────────────────────────

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_externally_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E> {
    static_assert(TagAttr::names.size() == sizeof...(Ts),
                  "externally_tagged: number of names must match variant alternatives");

    auto s_struct = s.serialize_struct("", 1);
    if(!s_struct) {
        return std::unexpected(s_struct.error());
    }

    auto name = TagAttr::names[value.index()];
    std::expected<void, E> field_result{};
    std::visit([&](const auto& item) { field_result = s_struct->serialize_field(name, item); },
               value);

    if(!field_result) {
        return std::unexpected(field_result.error());
    }
    return s_struct->end();
}

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_adjacently_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E> {
    static_assert(TagAttr::names.size() == sizeof...(Ts),
                  "adjacently_tagged: number of names must match variant alternatives");

    auto s_struct = s.serialize_struct("", 2);
    if(!s_struct) {
        return std::unexpected(s_struct.error());
    }

    auto name = TagAttr::names[value.index()];
    auto tag_result = s_struct->serialize_field(TagAttr::tag, name);
    if(!tag_result) {
        return std::unexpected(tag_result.error());
    }

    std::expected<void, E> content_result{};
    std::visit(
        [&](const auto& item) {
            content_result = s_struct->serialize_field(TagAttr::content, item);
        },
        value);

    if(!content_result) {
        return std::unexpected(content_result.error());
    }
    return s_struct->end();
}

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_externally_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E> {
    static_assert(TagAttr::names.size() == sizeof...(Ts),
                  "externally_tagged: number of names must match variant alternatives");

    auto d_struct = d.deserialize_struct("", 1);
    if(!d_struct) {
        return std::unexpected(d_struct.error());
    }

    auto key = d_struct->next_key();
    if(!key) {
        return std::unexpected(key.error());
    }
    if(!key->has_value()) {
        return std::unexpected(E::type_mismatch);
    }

    std::string_view key_name = **key;
    bool matched = false;
    std::expected<void, E> status{};

    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (([&] {
             if(matched) {
                 return;
             }
             if(TagAttr::names[I] != key_name) {
                 return;
             }
             matched = true;

             using alt_t = std::variant_alternative_t<I, std::variant<Ts...>>;
             if constexpr(std::same_as<alt_t, std::monostate>) {
                 std::monostate alt{};
                 auto result = d_struct->deserialize_value(alt);
                 if(!result) {
                     status = std::unexpected(result.error());
                 } else {
                     value = std::monostate{};
                 }
             } else if constexpr(std::default_initializable<alt_t>) {
                 alt_t alt{};
                 auto result = d_struct->deserialize_value(alt);
                 if(!result) {
                     status = std::unexpected(result.error());
                 } else {
                     value = std::move(alt);
                 }
             } else {
                 status = std::unexpected(E::invalid_state);
             }
         }()),
         ...);
    }(std::make_index_sequence<sizeof...(Ts)>{});

    if(!matched) {
        return std::unexpected(E::type_mismatch);
    }
    if(!status) {
        return std::unexpected(status.error());
    }
    return d_struct->end();
}

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_adjacently_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E> {
    static_assert(TagAttr::names.size() == sizeof...(Ts),
                  "adjacently_tagged: number of names must match variant alternatives");

    auto d_struct = d.deserialize_struct("", 2);
    if(!d_struct) {
        return std::unexpected(d_struct.error());
    }

    // Read tag field (must come first)
    auto tag_key = d_struct->next_key();
    if(!tag_key) {
        return std::unexpected(tag_key.error());
    }
    if(!tag_key->has_value() || **tag_key != TagAttr::tag) {
        return std::unexpected(E::type_mismatch);
    }

    std::string tag_value;
    auto tag_status = d_struct->deserialize_value(tag_value);
    if(!tag_status) {
        return std::unexpected(tag_status.error());
    }

    // Read content field
    auto content_key = d_struct->next_key();
    if(!content_key) {
        return std::unexpected(content_key.error());
    }
    if(!content_key->has_value() || **content_key != TagAttr::content) {
        return std::unexpected(E::type_mismatch);
    }

    // Dispatch by tag value
    bool matched = false;
    std::expected<void, E> status{};

    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (([&] {
             if(matched) {
                 return;
             }
             if(TagAttr::names[I] != tag_value) {
                 return;
             }
             matched = true;

             using alt_t = std::variant_alternative_t<I, std::variant<Ts...>>;
             if constexpr(std::same_as<alt_t, std::monostate>) {
                 std::monostate alt{};
                 auto result = d_struct->deserialize_value(alt);
                 if(!result) {
                     status = std::unexpected(result.error());
                 } else {
                     value = std::monostate{};
                 }
             } else if constexpr(std::default_initializable<alt_t>) {
                 alt_t alt{};
                 auto result = d_struct->deserialize_value(alt);
                 if(!result) {
                     status = std::unexpected(result.error());
                 } else {
                     value = std::move(alt);
                 }
             } else {
                 status = std::unexpected(E::invalid_state);
             }
         }()),
         ...);
    }(std::make_index_sequence<sizeof...(Ts)>{});

    if(!matched) {
        return std::unexpected(E::type_mismatch);
    }
    if(!status) {
        return std::unexpected(status.error());
    }
    return d_struct->end();
}

}  // namespace detail

template <serializer_like S,
          typename V,
          typename T = typename S::value_type,
          typename E = S::error_type>
constexpr auto serialize(S& s, const V& v) -> std::expected<T, E> {
    using Serde = serialize_traits<S, V>;

    if constexpr(requires { Serde::serialize(s, v); }) {
        return Serde::serialize(s, v);
    } else if constexpr(annotated_type<V>) {
        using attrs_t = typename std::remove_cvref_t<V>::attrs;
        auto&& value = annotated_value(v);
        using value_t = std::remove_cvref_t<decltype(value)>;

        // Field-only attrs at value level are errors
        static_assert(!detail::tuple_has_v<attrs_t, schema::skip>,
                      "schema::skip is only valid for struct fields");
        static_assert(!detail::tuple_has_v<attrs_t, schema::flatten>,
                      "schema::flatten is only valid for struct fields");

        // Tagged variant dispatch
        if constexpr(is_specialization_of<std::variant, value_t> &&
                     detail::tuple_any_of_v<attrs_t, is_externally_tagged_attr>) {
            using tag_attr = detail::tuple_find_t<attrs_t, is_externally_tagged_attr>;
            return detail::serialize_externally_tagged<E>(s, value, tag_attr{});
        } else if constexpr(is_specialization_of<std::variant, value_t> &&
                            detail::tuple_any_of_v<attrs_t, is_adjacently_tagged_attr>) {
            using tag_attr = detail::tuple_find_t<attrs_t, is_adjacently_tagged_attr>;
            return detail::serialize_adjacently_tagged<E>(s, value, tag_attr{});
        }
        // Behavior: enum_string — serialize enum as string
        else if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::enum_string>) {
            using Policy =
                typename detail::tuple_find_spec_t<attrs_t, behavior::enum_string>::policy;
            static_assert(std::is_enum_v<value_t>, "behavior::enum_string requires an enum type");
            auto enum_text = spelling::map_enum_to_string<value_t, Policy>(value);
            return s.serialize_str(enum_text);
        }
        // Behavior: with<Adapter> — adapter-based serialization
        else if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::with>) {
            using Adapter = typename detail::tuple_find_spec_t<attrs_t, behavior::with>::adapter;
            return Adapter::serialize(s, value);
        }
        // Default: serialize the underlying value
        else {
            return serialize(s, value);
        }
    } else if constexpr(std::is_enum_v<V>) {
        using underlying_t = std::underlying_type_t<V>;
        if constexpr(std::is_signed_v<underlying_t>) {
            return s.serialize_int(static_cast<std::int64_t>(static_cast<underlying_t>(v)));
        } else {
            return s.serialize_uint(static_cast<std::uint64_t>(static_cast<underlying_t>(v)));
        }
    } else if constexpr(bool_like<V>) {
        return s.serialize_bool(v);
    } else if constexpr(int_like<V>) {
        return s.serialize_int(v);
    } else if constexpr(uint_like<V>) {
        return s.serialize_uint(v);
    } else if constexpr(floating_like<V>) {
        return s.serialize_float(static_cast<double>(v));
    } else if constexpr(char_like<V>) {
        return s.serialize_char(v);
    } else if constexpr(str_like<V>) {
        return s.serialize_str(v);
    } else if constexpr(bytes_like<V>) {
        return s.serialize_bytes(v);
    } else if constexpr(std::same_as<V, std::nullptr_t>) {
        return s.serialize_null();
    } else if constexpr(std::same_as<V, std::monostate>) {
        return s.serialize_null();
    } else if constexpr(is_specialization_of<std::optional, V>) {
        if(v.has_value()) {
            return s.serialize_some(v.value());
        } else {
            return s.serialize_null();
        }
    } else if constexpr(is_specialization_of<std::unique_ptr, V> ||
                        is_specialization_of<std::shared_ptr, V>) {
        if(v) {
            return s.serialize_some(*v);
        }
        return s.serialize_null();
    } else if constexpr(is_specialization_of<std::variant, V>) {
        return s.serialize_variant(v);
    } else if constexpr(std::ranges::input_range<V>) {
        constexpr auto kind = format_kind<V>;
        if constexpr(kind == range_format::sequence || kind == range_format::set) {
            std::optional<std::size_t> len = std::nullopt;
            if constexpr(std::ranges::sized_range<V>) {
                len = static_cast<std::size_t>(std::ranges::size(v));
            }

            auto s_seq = s.serialize_seq(len);
            if(!s_seq) {
                return std::unexpected(s_seq.error());
            }

            for(auto&& e: v) {
                auto element = s_seq->serialize_element(e);
                if(!element) {
                    return std::unexpected(element.error());
                }
            }

            return s_seq->end();
        } else if constexpr(kind == range_format::map) {
            std::optional<std::size_t> len = std::nullopt;
            if constexpr(std::ranges::sized_range<V>) {
                len = static_cast<std::size_t>(std::ranges::size(v));
            }

            auto s_map = s.serialize_map(len);
            if(!s_map) {
                return std::unexpected(s_map.error());
            }

            for(auto&& [key, value]: v) {
                auto entry = s_map->serialize_entry(key, value);
                if(!entry) {
                    return std::unexpected(entry.error());
                }
            }

            return s_map->end();
        } else {
            static_assert(dependent_false<V>, "cannot auto serialize the input range");
        }
    } else if constexpr(is_pair_v<V> || is_tuple_v<V>) {
        auto s_tuple = s.serialize_tuple(std::tuple_size_v<V>);
        if(!s_tuple) {
            return std::unexpected(s_tuple.error());
        }

        std::expected<void, E> element_result;
        auto for_each = [&](const auto& element) -> bool {
            auto result = s_tuple->serialize_element(element);
            if(!result) {
                element_result = std::unexpected(result.error());
                return false;
            }
            return true;
        };
        std::apply([&](const auto&... elements) { return (for_each(elements) && ...); }, v);
        if(!element_result) {
            return std::unexpected(element_result.error());
        }

        return s_tuple->end();
    } else if constexpr(refl::reflectable_class<V>) {
        using config_t = config::config_of<S>;
        auto s_struct = s.serialize_struct(refl::type_name<V>(), refl::field_count<V>());
        if(!s_struct) {
            return std::unexpected(s_struct.error());
        }

        std::expected<void, E> field_result;
        refl::for_each(v, [&](auto field) {
            auto result = detail::serialize_struct_field<config_t, E>(*s_struct, field);
            if(!result) {
                field_result = std::unexpected(result.error());
                return false;
            }
            return true;
        });
        if(!field_result) {
            return std::unexpected(field_result.error());
        }

        return s_struct->end();
    } else {
        static_assert(dependent_false<V>,
                      "cannot auto serialize the value, try to specialize for it");
    }
}

template <deserializer_like D, typename V, typename E = typename D::error_type>
constexpr auto deserialize(D& d, V& v) -> std::expected<void, E> {
    using Deserde = deserialize_traits<D, V>;

    if constexpr(requires { Deserde::deserialize(d, v); }) {
        return Deserde::deserialize(d, v);
    } else if constexpr(annotated_type<V>) {
        using attrs_t = typename std::remove_cvref_t<V>::attrs;
        auto&& value = annotated_value(v);
        using value_t = std::remove_cvref_t<decltype(value)>;

        // Field-only attrs at value level are errors
        static_assert(!detail::tuple_has_v<attrs_t, schema::skip>,
                      "schema::skip is only valid for struct fields");
        static_assert(!detail::tuple_has_v<attrs_t, schema::flatten>,
                      "schema::flatten is only valid for struct fields");

        // Tagged variant dispatch
        if constexpr(is_specialization_of<std::variant, value_t> &&
                     detail::tuple_any_of_v<attrs_t, is_externally_tagged_attr>) {
            using tag_attr = detail::tuple_find_t<attrs_t, is_externally_tagged_attr>;
            return detail::deserialize_externally_tagged<E>(d, value, tag_attr{});
        } else if constexpr(is_specialization_of<std::variant, value_t> &&
                            detail::tuple_any_of_v<attrs_t, is_adjacently_tagged_attr>) {
            using tag_attr = detail::tuple_find_t<attrs_t, is_adjacently_tagged_attr>;
            return detail::deserialize_adjacently_tagged<E>(d, value, tag_attr{});
        }
        // Behavior: enum_string — deserialize string then map to enum
        else if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::enum_string>) {
            using Policy =
                typename detail::tuple_find_spec_t<attrs_t, behavior::enum_string>::policy;
            static_assert(std::is_enum_v<value_t>, "behavior::enum_string requires an enum type");
            std::string enum_text;
            auto parsed = d.deserialize_str(enum_text);
            if(!parsed) {
                return std::unexpected(parsed.error());
            }
            auto mapped = spelling::map_string_to_enum<value_t, Policy>(enum_text);
            if(mapped.has_value()) {
                value = *mapped;
                return {};
            } else {
                return std::unexpected(E::type_mismatch);
            }
        }
        // Behavior: with<Adapter> — adapter-based deserialization
        else if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::with>) {
            using Adapter = typename detail::tuple_find_spec_t<attrs_t, behavior::with>::adapter;
            return Adapter::deserialize(d, value);
        }
        // Default: deserialize the underlying value
        else {
            return deserialize(d, value);
        }
    } else if constexpr(std::is_enum_v<V>) {
        using underlying_t = std::underlying_type_t<V>;
        if constexpr(std::is_signed_v<underlying_t>) {
            std::int64_t parsed = 0;
            auto status = d.deserialize_int(parsed);
            if(!status) {
                return std::unexpected(status.error());
            }
            if(!detail::integral_value_in_range<underlying_t>(parsed)) {
                return std::unexpected(E::number_out_of_range);
            }
            v = static_cast<V>(static_cast<underlying_t>(parsed));
            return {};
        } else {
            std::uint64_t parsed = 0;
            auto status = d.deserialize_uint(parsed);
            if(!status) {
                return std::unexpected(status.error());
            }
            if(!detail::integral_value_in_range<underlying_t>(parsed)) {
                return std::unexpected(E::number_out_of_range);
            }
            v = static_cast<V>(static_cast<underlying_t>(parsed));
            return {};
        }
    } else if constexpr(bool_like<V>) {
        return d.deserialize_bool(v);
    } else if constexpr(int_like<V>) {
        return d.deserialize_int(v);
    } else if constexpr(uint_like<V>) {
        return d.deserialize_uint(v);
    } else if constexpr(floating_like<V>) {
        return d.deserialize_float(v);
    } else if constexpr(char_like<V>) {
        return d.deserialize_char(v);
    } else if constexpr(std::same_as<V, std::string> || std::derived_from<V, std::string>) {
        return d.deserialize_str(static_cast<std::string&>(v));
    } else if constexpr(std::same_as<V, std::vector<std::byte>>) {
        return d.deserialize_bytes(v);
    } else if constexpr(std::same_as<V, std::nullptr_t>) {
        auto is_none = d.deserialize_none();
        if(!is_none) {
            return std::unexpected(is_none.error());
        }
        if(*is_none) {
            v = nullptr;
            return {};
        }
        return std::unexpected(E::type_mismatch);
    } else if constexpr(std::same_as<V, std::monostate>) {
        auto is_none = d.deserialize_none();
        if(!is_none) {
            return std::unexpected(is_none.error());
        }
        if(*is_none) {
            v = std::monostate{};
            return {};
        }
        return std::unexpected(E::type_mismatch);
    } else if constexpr(is_specialization_of<std::optional, V>) {
        auto is_none = d.deserialize_none();
        if(!is_none) {
            return std::unexpected(is_none.error());
        }

        if(*is_none) {
            v.reset();
            return {};
        }

        using value_t = typename V::value_type;
        if(v.has_value()) {
            return deserialize(d, v.value());
        }

        if constexpr(std::default_initializable<value_t>) {
            v.emplace();
            auto status = deserialize(d, v.value());
            if(!status) {
                v.reset();
                return std::unexpected(status.error());
            }
            return {};
        } else {
            static_assert(dependent_false<V>,
                          "cannot auto deserialize optional<T> without default-constructible T");
        }
    } else if constexpr(is_specialization_of<std::unique_ptr, V>) {
        auto is_none = d.deserialize_none();
        if(!is_none) {
            return std::unexpected(is_none.error());
        }
        if(*is_none) {
            v.reset();
            return {};
        }

        using value_t = typename V::element_type;
        static_assert(std::default_initializable<value_t>,
                      "cannot auto deserialize unique_ptr<T> without default-constructible T");
        static_assert(std::same_as<typename V::deleter_type, std::default_delete<value_t>>,
                      "cannot auto deserialize unique_ptr<T, D> with custom deleter");

        auto value = std::make_unique<value_t>();
        auto status = deserialize(d, *value);
        if(!status) {
            return std::unexpected(status.error());
        }

        v = std::move(value);
        return {};
    } else if constexpr(is_specialization_of<std::shared_ptr, V>) {
        auto is_none = d.deserialize_none();
        if(!is_none) {
            return std::unexpected(is_none.error());
        }
        if(*is_none) {
            v.reset();
            return {};
        }

        using value_t = typename V::element_type;
        static_assert(std::default_initializable<value_t>,
                      "cannot auto deserialize shared_ptr<T> without default-constructible T");

        auto value = std::make_shared<value_t>();
        auto status = deserialize(d, *value);
        if(!status) {
            return std::unexpected(status.error());
        }

        v = std::move(value);
        return {};
    } else if constexpr(is_specialization_of<std::variant, V>) {
        return d.deserialize_variant(v);
    } else if constexpr(std::ranges::input_range<V>) {
        constexpr auto kind = format_kind<V>;
        if constexpr(kind == range_format::sequence || kind == range_format::set) {
            auto d_seq = d.deserialize_seq(std::nullopt);
            if(!d_seq) {
                return std::unexpected(d_seq.error());
            }

            if constexpr(requires { v.clear(); }) {
                v.clear();
            }

            using element_t = std::ranges::range_value_t<V>;
            static_assert(
                std::default_initializable<element_t>,
                "auto deserialization for ranges requires default-constructible elements");
            static_assert(eventide::detail::sequence_insertable<V, element_t>,
                          "cannot auto deserialize range: container does not support insertion");

            while(true) {
                auto has_next = d_seq->has_next();
                if(!has_next) {
                    return std::unexpected(has_next.error());
                }
                if(!*has_next) {
                    break;
                }

                element_t element{};
                auto element_status = d_seq->deserialize_element(element);
                if(!element_status) {
                    return std::unexpected(element_status.error());
                }

                eventide::detail::append_sequence_element(v, std::move(element));
            }

            return d_seq->end();
        } else if constexpr(kind == range_format::map) {
            using key_t = typename V::key_type;
            using mapped_t = typename V::mapped_type;

            auto d_map = d.deserialize_map(std::nullopt);
            if(!d_map) {
                return std::unexpected(d_map.error());
            }

            if constexpr(requires { v.clear(); }) {
                v.clear();
            }

            static_assert(
                serde::spelling::parseable_map_key<key_t>,
                "auto map deserialization requires key_type parseable from JSON object keys");
            static_assert(std::default_initializable<mapped_t>,
                          "auto map deserialization requires default-constructible mapped_type");
            static_assert(eventide::detail::map_insertable<V, key_t, mapped_t>,
                          "cannot auto deserialize map: container does not support map insertion");

            while(true) {
                auto key = d_map->next_key();
                if(!key) {
                    return std::unexpected(key.error());
                }
                if(!key->has_value()) {
                    break;
                }

                auto parsed_key = serde::spelling::parse_map_key<key_t>(**key);
                if(!parsed_key) {
                    if constexpr(requires { d_map->invalid_key(**key); }) {
                        auto invalid = d_map->invalid_key(**key);
                        if(!invalid) {
                            return std::unexpected(invalid.error());
                        }
                        continue;
                        return std::unexpected(E::type_mismatch);
                    } else {
                        static_assert(
                            dependent_false<key_t>,
                            "key parse failed and deserializer does not provide invalid_key");
                    }
                }

                mapped_t mapped{};
                auto mapped_status = d_map->deserialize_value(mapped);
                if(!mapped_status) {
                    return std::unexpected(mapped_status.error());
                }

                eventide::detail::insert_map_entry(v, std::move(*parsed_key), std::move(mapped));
            }

            return d_map->end();
        } else {
            static_assert(dependent_false<V>, "cannot auto deserialize the input range");
        }
    } else if constexpr(is_pair_v<V> || is_tuple_v<V>) {
        auto d_tuple = d.deserialize_tuple(std::tuple_size_v<V>);
        if(!d_tuple) {
            return std::unexpected(d_tuple.error());
        }

        std::expected<void, E> element_result;
        auto read_element = [&](auto& element) -> bool {
            auto result = d_tuple->deserialize_element(element);
            if(!result) {
                element_result = std::unexpected(result.error());
                return false;
            }
            return true;
        };
        std::apply([&](auto&... elements) { return (read_element(elements) && ...); }, v);
        if(!element_result) {
            return std::unexpected(element_result.error());
        }

        return d_tuple->end();
    } else if constexpr(refl::reflectable_class<V>) {
        using config_t = config::config_of<D>;
        auto d_struct = d.deserialize_struct(refl::type_name<V>(), refl::field_count<V>());
        if(!d_struct) {
            return std::unexpected(d_struct.error());
        }

        while(true) {
            auto key = d_struct->next_key();
            if(!key) {
                return std::unexpected(key.error());
            }
            if(!key->has_value()) {
                break;
            }

            std::string_view key_name = **key;
            bool matched = false;
            std::expected<void, E> field_result;

            refl::for_each(v, [&](auto field) {
                auto status =
                    detail::deserialize_struct_field<config_t, E>(*d_struct, key_name, field);
                if(!status) {
                    field_result = std::unexpected(status.error());
                    return false;
                }
                if(*status) {
                    matched = true;
                }
                return !matched;
            });

            if(!field_result) {
                return std::unexpected(field_result.error());
            }

            if(!matched) {
                auto skipped = d_struct->skip_value();
                if(!skipped) {
                    return std::unexpected(skipped.error());
                }
            }
        }

        return d_struct->end();
    } else {
        static_assert(dependent_false<V>,
                      "cannot auto deserialize the value, try to specialize for it");
    }
}

}  // namespace eventide::serde
