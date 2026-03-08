#pragma once

#include <array>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include "annotation.h"
#include "attrs.h"
#include "config.h"
#include "traits.h"
#include "eventide/common/ranges.h"
#include "eventide/reflection/enum.h"
#include "eventide/reflection/struct.h"
#include "eventide/serde/serde/attrs/behavior.h"
#include "eventide/serde/serde/attrs/schema.h"

namespace eventide::serde {

template <typename S, typename T>
struct serialize_traits;

template <typename D, typename T>
struct deserialize_traits;

template <serializer_like S,
          typename V,
          typename T = typename S::value_type,
          typename E = S::error_type>
constexpr auto serialize(S& s, const V& v) -> std::expected<T, E>;

template <deserializer_like D, typename V, typename E = typename D::error_type>
constexpr auto deserialize(D& d, V& v) -> std::expected<void, E>;

}  // namespace eventide::serde

#include "eventide/serde/serde/utils/common.h"
#include "eventide/serde/serde/utils/field_dispatch.h"
#include "eventide/serde/serde/utils/reflectable.h"
#include "eventide/serde/serde/utils/tagged.h"

namespace eventide::serde {

template <serializer_like S, typename V, typename T, typename E>
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
                     detail::tuple_any_of_v<attrs_t, is_tagged_attr>) {
            using tag_attr = detail::tuple_find_t<attrs_t, is_tagged_attr>;
            constexpr auto strategy = tagged_strategy_of<tag_attr>;
            if constexpr(strategy == tagged_strategy::external) {
                return detail::serialize_externally_tagged<E>(s, value, tag_attr{});
            } else if constexpr(strategy == tagged_strategy::internal) {
                return detail::serialize_internally_tagged<E>(s, value, tag_attr{});
            } else {
                return detail::serialize_adjacently_tagged<E>(s, value, tag_attr{});
            }
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
        // Behavior: as<Target> — type conversion before serialization
        else if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::as>) {
            using Target = typename detail::tuple_find_spec_t<attrs_t, behavior::as>::target;
            static_assert(
                std::is_constructible_v<Target, const value_t&>,
                "behavior::as<Target> requires Target to be constructible from the value type");
            Target converted(value);
            return serialize(s, converted);
        }
        // Struct-level schema attrs for annotated structs
        else if constexpr(refl::reflectable_class<value_t> &&
                          (detail::tuple_has_spec_v<attrs_t, schema::rename_all> ||
                           detail::tuple_has_v<attrs_t, schema::deny_unknown_fields>)) {
            using base_config_t = config::config_of<S>;
            using struct_config_t = detail::annotated_struct_config_t<base_config_t, attrs_t>;
            return detail::serialize_reflectable<struct_config_t, E>(s, value);
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
    } else if constexpr(tuple_like<V>) {
        auto s_tuple = s.serialize_tuple(std::tuple_size_v<std::remove_cvref_t<V>>);
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
    } else if constexpr(refl::reflectable_class<V>) {
        using config_t = config::config_of<S>;
        return detail::serialize_reflectable<config_t, E>(s, v);
    } else {
        static_assert(dependent_false<V>,
                      "cannot auto serialize the value, try to specialize for it");
    }
}

template <deserializer_like D, typename V, typename E>
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
                     detail::tuple_any_of_v<attrs_t, is_tagged_attr>) {
            using tag_attr = detail::tuple_find_t<attrs_t, is_tagged_attr>;
            constexpr auto strategy = tagged_strategy_of<tag_attr>;
            if constexpr(strategy == tagged_strategy::external) {
                return detail::deserialize_externally_tagged<E>(d, value, tag_attr{});
            } else if constexpr(strategy == tagged_strategy::internal) {
                return detail::deserialize_internally_tagged<E>(d, value, tag_attr{});
            } else {
                return detail::deserialize_adjacently_tagged<E>(d, value, tag_attr{});
            }
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
        // Behavior: as<Target> — deserialize as Target, then convert back
        else if constexpr(detail::tuple_has_spec_v<attrs_t, behavior::as>) {
            using Target = typename detail::tuple_find_spec_t<attrs_t, behavior::as>::target;
            static_assert(
                std::is_constructible_v<value_t, Target&&>,
                "behavior::as<Target> requires the value type to be constructible from Target");
            Target temp{};
            auto status = deserialize(d, temp);
            if(!status) {
                return std::unexpected(status.error());
            }
            value = value_t(std::move(temp));
            return {};
        }
        // Struct-level schema attrs for annotated structs
        else if constexpr(refl::reflectable_class<value_t> &&
                          (detail::tuple_has_spec_v<attrs_t, schema::rename_all> ||
                           detail::tuple_has_v<attrs_t, schema::deny_unknown_fields>)) {
            using base_config_t = config::config_of<D>;
            using struct_config_t = detail::annotated_struct_config_t<base_config_t, attrs_t>;
            constexpr bool deny_unknown = detail::tuple_has_v<attrs_t, schema::deny_unknown_fields>;
            return detail::deserialize_reflectable<struct_config_t, E, deny_unknown>(d, value);
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
    } else if constexpr(detail::is_captured_dom_value_v<D, V>) {
        auto captured = d.capture_dom_value();
        if(!captured) {
            return std::unexpected(captured.error());
        }
        v = std::move(*captured);
        return {};
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
    } else if constexpr(tuple_like<V>) {
        auto d_tuple = d.deserialize_tuple(std::tuple_size_v<std::remove_cvref_t<V>>);
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
    } else if constexpr(refl::reflectable_class<V>) {
        using config_t = config::config_of<D>;
        return detail::deserialize_reflectable<config_t, E, false>(d, v);
    } else {
        static_assert(dependent_false<V>,
                      "cannot auto deserialize the value, try to specialize for it");
    }
}

}  // namespace eventide::serde
