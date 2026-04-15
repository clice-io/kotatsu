#pragma once

#include <array>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include "config.h"
#include "traits.h"
#include "eventide/common/expected_try.h"
#include "eventide/common/ranges.h"
#include "eventide/reflection/annotation.h"
#include "eventide/reflection/attrs.h"
#include "eventide/reflection/enum.h"
#include "eventide/reflection/struct.h"
#include "eventide/serde/serde/utils/apply_behavior.h"
#include "eventide/serde/serde/utils/common.h"
#include "eventide/serde/serde/utils/field_dispatch.h"
#include "eventide/serde/serde/utils/fwd.h"
#include "eventide/serde/serde/utils/reflectable.h"
#include "eventide/serde/serde/utils/tagged.h"

namespace eventide::serde {

template <serializer_like S, typename V, typename T, typename E>
constexpr auto serialize(S& s, const V& v) -> std::expected<T, E> {
    using Serde = serialize_traits<S, V>;

    if constexpr(requires { Serde::serialize(s, v); }) {
        return Serde::serialize(s, v);
    } else if constexpr(refl::annotated_type<V>) {
        using attrs_t = typename std::remove_cvref_t<V>::attrs;
        auto&& value = refl::annotated_value(v);
        using value_t = std::remove_cvref_t<decltype(value)>;

        // Field-only attrs at value level are errors
        static_assert(!tuple_has_v<attrs_t, refl::attrs::skip>,
                      "schema::skip is only valid for struct fields");
        static_assert(!tuple_has_v<attrs_t, refl::attrs::flatten>,
                      "schema::flatten is only valid for struct fields");

        // Tagged variant dispatch
        if constexpr(is_specialization_of<std::variant, value_t> &&
                     tuple_any_of_v<attrs_t, refl::is_tagged_attr>) {
            using tag_attr = tuple_find_t<attrs_t, refl::is_tagged_attr>;
            constexpr auto strategy = refl::tagged_strategy_of<tag_attr>;
            if constexpr(strategy == refl::tagged_strategy::external) {
                return detail::serialize_externally_tagged<E>(s, value, tag_attr{});
            } else if constexpr(strategy == refl::tagged_strategy::internal) {
                return detail::serialize_internally_tagged<E>(s, value, tag_attr{});
            } else {
                return detail::serialize_adjacently_tagged<E>(s, value, tag_attr{});
            }
        }
        // Behavior: with/as/enum_string — delegate to apply_serialize_behavior
        else if constexpr(tuple_count_of_v<attrs_t, refl::is_behavior_provider> > 0) {
            return *detail::apply_serialize_behavior<attrs_t, value_t, E>(
                value,
                [&](const auto& v) { return serialize(s, v); },
                [&](auto tag, const auto& v) {
                    using Adapter = typename decltype(tag)::type;
                    return Adapter::serialize(s, v);
                });
        }
        // Struct-level schema attrs for annotated structs
        else if constexpr(refl::reflectable_class<value_t> &&
                          (tuple_has_spec_v<attrs_t, refl::attrs::rename_all> ||
                           tuple_has_v<attrs_t, refl::attrs::deny_unknown_fields>)) {
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
    } else if constexpr(null_like<V>) {
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
        ETD_EXPECTED_TRY_V(auto s_tuple,
                           s.serialize_tuple(std::tuple_size_v<std::remove_cvref_t<V>>));

        std::expected<void, E> element_result;
        auto for_each = [&](const auto& element) -> bool {
            auto result = s_tuple.serialize_element(element);
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

        return s_tuple.end();
    } else if constexpr(std::ranges::input_range<V>) {
        constexpr auto kind = format_kind<V>;
        if constexpr(kind == range_format::sequence || kind == range_format::set) {
            std::optional<std::size_t> len = std::nullopt;
            if constexpr(std::ranges::sized_range<V>) {
                len = static_cast<std::size_t>(std::ranges::size(v));
            }

            ETD_EXPECTED_TRY_V(auto s_seq, s.serialize_seq(len));

            for(auto&& e: v) {
                ETD_EXPECTED_TRY(s_seq.serialize_element(e));
            }

            return s_seq.end();
        } else if constexpr(kind == range_format::map) {
            std::optional<std::size_t> len = std::nullopt;
            if constexpr(std::ranges::sized_range<V>) {
                len = static_cast<std::size_t>(std::ranges::size(v));
            }

            ETD_EXPECTED_TRY_V(auto s_map, s.serialize_map(len));

            for(auto&& [key, value]: v) {
                ETD_EXPECTED_TRY(s_map.serialize_entry(key, value));
            }

            return s_map.end();
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
    } else if constexpr(refl::annotated_type<V>) {
        using attrs_t = typename std::remove_cvref_t<V>::attrs;
        auto&& value = refl::annotated_value(v);
        using value_t = std::remove_cvref_t<decltype(value)>;

        // Field-only attrs at value level are errors
        static_assert(!tuple_has_v<attrs_t, refl::attrs::skip>,
                      "schema::skip is only valid for struct fields");
        static_assert(!tuple_has_v<attrs_t, refl::attrs::flatten>,
                      "schema::flatten is only valid for struct fields");

        // Tagged variant dispatch
        if constexpr(is_specialization_of<std::variant, value_t> &&
                     tuple_any_of_v<attrs_t, refl::is_tagged_attr>) {
            using tag_attr = tuple_find_t<attrs_t, refl::is_tagged_attr>;
            constexpr auto strategy = refl::tagged_strategy_of<tag_attr>;
            if constexpr(strategy == refl::tagged_strategy::external) {
                return detail::deserialize_externally_tagged<E>(d, value, tag_attr{});
            } else if constexpr(strategy == refl::tagged_strategy::internal) {
                return detail::deserialize_internally_tagged<E>(d, value, tag_attr{});
            } else {
                return detail::deserialize_adjacently_tagged<E>(d, value, tag_attr{});
            }
        }
        // Behavior: with/as/enum_string — delegate to apply_deserialize_behavior
        else if constexpr(tuple_count_of_v<attrs_t, refl::is_behavior_provider> > 0) {
            return *detail::apply_deserialize_behavior<attrs_t, value_t, E>(
                value,
                [&](auto& v) { return deserialize(d, v); },
                [&](auto tag, auto& v) -> std::expected<void, E> {
                    using Adapter = typename decltype(tag)::type;
                    return Adapter::deserialize(d, v);
                });
        }
        // Struct-level schema attrs for annotated structs
        else if constexpr(refl::reflectable_class<value_t> &&
                          (tuple_has_spec_v<attrs_t, refl::attrs::rename_all> ||
                           tuple_has_v<attrs_t, refl::attrs::deny_unknown_fields>)) {
            using base_config_t = config::config_of<D>;
            using struct_config_t = detail::annotated_struct_config_t<base_config_t, attrs_t>;
            constexpr bool deny_unknown = tuple_has_v<attrs_t, refl::attrs::deny_unknown_fields>;
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
            ETD_EXPECTED_TRY(d.deserialize_int(parsed));
            if(!detail::integral_value_in_range<underlying_t>(parsed)) {
                return std::unexpected(E::number_out_of_range);
            }
            v = static_cast<V>(static_cast<underlying_t>(parsed));
            return {};
        } else {
            std::uint64_t parsed = 0;
            ETD_EXPECTED_TRY(d.deserialize_uint(parsed));
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
        ETD_EXPECTED_TRY_V(auto captured, d.capture_dom_value());
        v = std::move(captured);
        return {};
    } else if constexpr(null_like<V>) {
        ETD_EXPECTED_TRY_V(auto is_none, d.deserialize_none());
        if(is_none) {
            v = V{};
            return {};
        }
        return std::unexpected(E::type_mismatch);
    } else if constexpr(is_specialization_of<std::optional, V>) {
        ETD_EXPECTED_TRY_V(auto is_none, d.deserialize_none());

        if(is_none) {
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
        ETD_EXPECTED_TRY_V(auto is_none, d.deserialize_none());
        if(is_none) {
            v.reset();
            return {};
        }

        using value_t = typename V::element_type;
        static_assert(std::default_initializable<value_t>,
                      "cannot auto deserialize unique_ptr<T> without default-constructible T");
        static_assert(std::same_as<typename V::deleter_type, std::default_delete<value_t>>,
                      "cannot auto deserialize unique_ptr<T, D> with custom deleter");

        auto value = std::make_unique<value_t>();
        ETD_EXPECTED_TRY(deserialize(d, *value));

        v = std::move(value);
        return {};
    } else if constexpr(is_specialization_of<std::shared_ptr, V>) {
        ETD_EXPECTED_TRY_V(auto is_none, d.deserialize_none());
        if(is_none) {
            v.reset();
            return {};
        }

        using value_t = typename V::element_type;
        static_assert(std::default_initializable<value_t>,
                      "cannot auto deserialize shared_ptr<T> without default-constructible T");

        auto value = std::make_shared<value_t>();
        ETD_EXPECTED_TRY(deserialize(d, *value));

        v = std::move(value);
        return {};
    } else if constexpr(is_specialization_of<std::variant, V>) {
        return d.deserialize_variant(v);
    } else if constexpr(tuple_like<V>) {
        ETD_EXPECTED_TRY_V(auto d_tuple,
                           d.deserialize_tuple(std::tuple_size_v<std::remove_cvref_t<V>>));

        std::expected<void, E> element_result;
        std::size_t tuple_index = 0;
        auto read_element = [&](auto& element) -> bool {
            auto result = d_tuple.deserialize_element(element);
            if(!result) {
                auto err = std::move(result).error();
                err.prepend_index(tuple_index);
                element_result = std::unexpected(std::move(err));
                return false;
            }
            ++tuple_index;
            return true;
        };
        std::apply([&](auto&... elements) { return (read_element(elements) && ...); }, v);
        if(!element_result) {
            return std::unexpected(element_result.error());
        }

        return d_tuple.end();
    } else if constexpr(std::ranges::input_range<V>) {
        constexpr auto kind = format_kind<V>;
        if constexpr(kind == range_format::sequence || kind == range_format::set) {
            ETD_EXPECTED_TRY_V(auto d_seq, d.deserialize_seq(std::nullopt));

            if constexpr(requires { v.clear(); }) {
                v.clear();
            }

            using element_t = std::ranges::range_value_t<V>;
            static_assert(
                std::default_initializable<element_t>,
                "auto deserialization for ranges requires default-constructible elements");
            static_assert(eventide::detail::sequence_insertable<V, element_t>,
                          "cannot auto deserialize range: container does not support insertion");

            std::size_t seq_index = 0;
            while(true) {
                ETD_EXPECTED_TRY_V(auto has_next, d_seq.has_next());
                if(!has_next) {
                    break;
                }

                element_t element{};
                auto elem_status = d_seq.deserialize_element(element);
                if(!elem_status) {
                    auto err = std::move(elem_status).error();
                    err.prepend_index(seq_index);
                    return std::unexpected(std::move(err));
                }

                eventide::detail::append_sequence_element(v, std::move(element));
                ++seq_index;
            }

            return d_seq.end();
        } else if constexpr(kind == range_format::map) {
            using key_t = typename V::key_type;
            using mapped_t = typename V::mapped_type;

            ETD_EXPECTED_TRY_V(auto d_map, d.deserialize_map(std::nullopt));

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
                ETD_EXPECTED_TRY_V(auto key, d_map.next_key());
                if(!key.has_value()) {
                    break;
                }

                auto parsed_key = serde::spelling::parse_map_key<key_t>(*key);
                if(!parsed_key) {
                    if constexpr(requires { d_map.invalid_key(*key); }) {
                        ETD_EXPECTED_TRY(d_map.invalid_key(*key));
                        continue;
                    } else {
                        static_assert(
                            dependent_false<key_t>,
                            "key parse failed and deserializer does not provide invalid_key");
                    }
                }

                mapped_t mapped{};
                auto map_val_status = d_map.deserialize_value(mapped);
                if(!map_val_status) {
                    auto err = std::move(map_val_status).error();
                    err.prepend_field(*key);
                    return std::unexpected(std::move(err));
                }

                eventide::detail::insert_map_entry(v, std::move(*parsed_key), std::move(mapped));
            }

            return d_map.end();
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
