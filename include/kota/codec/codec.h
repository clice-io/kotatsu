#pragma once

#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include "backend.h"
#include "config.h"
#include "traits.h"
#include "kota/support/expected_try.h"
#include "kota/support/ranges.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/meta/enum.h"
#include "kota/meta/struct.h"
#include "kota/codec/detail/apply_behavior.h"
#include "kota/codec/detail/common.h"
#include "kota/codec/detail/dispatch.h"
#include "kota/codec/detail/fwd.h"
#include "kota/codec/detail/struct_serialize.h"
#include "kota/codec/detail/struct_deserialize.h"
#include "kota/codec/detail/tagged.h"
#include "kota/codec/spelling.h"

namespace kota::codec {

template <serializer_like S, typename V, typename T, typename E>
constexpr auto serialize(S& s, const V& v) -> std::expected<T, E> {
    using Serde = serialize_traits<S, V>;

    if constexpr(requires { Serde::serialize(s, v); }) {
        return Serde::serialize(s, v);
    } else {
        detail::StreamingCtx<S> ctx{s};
        return detail::unified_serialize<config::config_of<S>, detail::StreamingCtx<S>, std::tuple<>>(ctx, v);
    }
}

template <deserializer_like D, typename V, typename E>
constexpr auto deserialize(D& d, V& v) -> std::expected<void, E> {
    using Deserde = deserialize_traits<D, V>;

    if constexpr(requires { Deserde::deserialize(d, v); }) {
        return Deserde::deserialize(d, v);
    } else if constexpr(meta::annotated_type<V>) {
        using attrs_t = typename std::remove_cvref_t<V>::attrs;
        auto&& value = meta::annotated_value(v);
        using value_t = std::remove_cvref_t<decltype(value)>;

        // Field-only attrs at value level are errors
        static_assert(!tuple_has_v<attrs_t, meta::attrs::skip>,
                      "schema::skip is only valid for struct fields");
        static_assert(!tuple_has_v<attrs_t, meta::attrs::flatten>,
                      "schema::flatten is only valid for struct fields");

        // Tagged variant dispatch
        if constexpr(is_specialization_of<std::variant, value_t> &&
                     tuple_any_of_v<attrs_t, meta::is_tagged_attr>) {
            using tag_attr = tuple_find_t<attrs_t, meta::is_tagged_attr>;
            constexpr auto strategy = meta::tagged_strategy_of<tag_attr>;
            if constexpr(strategy == meta::tagged_strategy::external) {
                return detail::deserialize_externally_tagged<E>(d, value, tag_attr{});
            } else if constexpr(strategy == meta::tagged_strategy::internal) {
                return detail::deserialize_internally_tagged<E>(d, value, tag_attr{});
            } else {
                return detail::deserialize_adjacently_tagged<E>(d, value, tag_attr{});
            }
        }
        // Behavior: with/as/enum_string — delegate to apply_deserialize_behavior
        else if constexpr(tuple_count_of_v<attrs_t, meta::is_behavior_provider> > 0) {
            return *detail::apply_deserialize_behavior<attrs_t, value_t, E>(
                value,
                [&](auto& v) { return deserialize(d, v); },
                [&](auto tag, auto& v) -> std::expected<void, E> {
                    using Adapter = typename decltype(tag)::type;
                    return Adapter::deserialize(d, v);
                });
        }
        // Struct-level schema attrs for annotated structs
        else if constexpr(meta::reflectable_class<value_t> &&
                          (tuple_has_spec_v<attrs_t, meta::attrs::rename_all> ||
                           tuple_has_v<attrs_t, meta::attrs::deny_unknown_fields>)) {
            using base_config_t = config::config_of<D>;
            using struct_config_t = detail::annotated_struct_config_t<base_config_t, attrs_t>;
            return detail::struct_deserialize<struct_config_t, E>(d, value);
        }
        // Default: deserialize the underlying value
        else {
            return deserialize(d, value);
        }
    } else if constexpr(std::is_enum_v<V>) {
        using underlying_t = std::underlying_type_t<V>;
        if constexpr(std::is_signed_v<underlying_t>) {
            std::int64_t parsed = 0;
            KOTA_EXPECTED_TRY(d.deserialize_int(parsed));
            if(!detail::integral_value_in_range<underlying_t>(parsed)) {
                return std::unexpected(E::number_out_of_range);
            }
            v = static_cast<V>(static_cast<underlying_t>(parsed));
            return {};
        } else {
            std::uint64_t parsed = 0;
            KOTA_EXPECTED_TRY(d.deserialize_uint(parsed));
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
        KOTA_EXPECTED_TRY_V(auto captured, d.capture_dom_value());
        v = std::move(captured);
        return {};
    } else if constexpr(null_like<V>) {
        KOTA_EXPECTED_TRY_V(auto is_none, d.deserialize_none());
        if(is_none) {
            v = V{};
            return {};
        }
        return std::unexpected(E::type_mismatch);
    } else if constexpr(is_specialization_of<std::optional, V>) {
        KOTA_EXPECTED_TRY_V(auto is_none, d.deserialize_none());

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
        KOTA_EXPECTED_TRY_V(auto is_none, d.deserialize_none());
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
        KOTA_EXPECTED_TRY(deserialize(d, *value));

        v = std::move(value);
        return {};
    } else if constexpr(is_specialization_of<std::shared_ptr, V>) {
        KOTA_EXPECTED_TRY_V(auto is_none, d.deserialize_none());
        if(is_none) {
            v.reset();
            return {};
        }

        using value_t = typename V::element_type;
        static_assert(std::default_initializable<value_t>,
                      "cannot auto deserialize shared_ptr<T> without default-constructible T");

        auto value = std::make_shared<value_t>();
        KOTA_EXPECTED_TRY(deserialize(d, *value));

        v = std::move(value);
        return {};
    } else if constexpr(is_specialization_of<std::variant, V>) {
        return d.deserialize_variant(v);
    } else if constexpr(tuple_like<V>) {
        if constexpr(D::field_mode_v == field_mode::by_name) {
            KOTA_EXPECTED_TRY(d.begin_array());

            std::expected<void, E> element_result;
            std::size_t tuple_index = 0;
            auto read_element = [&](auto& element) -> bool {
                auto has = d.next_element();
                if(!has || !*has) {
                    element_result = std::unexpected(E::type_mismatch);
                    return false;
                }
                auto result = deserialize(d, element);
                if(!result) {
                    auto err = std::move(result).error();
                    err.prepend_index(tuple_index);
                    element_result = std::unexpected(std::move(err));
                    return false;
                }
                ++tuple_index;
                return true;
            };
            std::apply([&](auto&... elements) { (read_element(elements) && ...); }, v);
            if(!element_result) {
                return std::unexpected(element_result.error());
            }

            return d.end_array();
        } else {
            // by_position: just deserialize elements directly
            std::expected<void, E> element_result;
            std::size_t tuple_index = 0;
            auto read_element = [&](auto& element) -> bool {
                auto result = deserialize(d, element);
                if(!result) {
                    auto err = std::move(result).error();
                    err.prepend_index(tuple_index);
                    element_result = std::unexpected(std::move(err));
                    return false;
                }
                ++tuple_index;
                return true;
            };
            std::apply([&](auto&... elements) { (read_element(elements) && ...); }, v);
            if(!element_result) {
                return std::unexpected(element_result.error());
            }
            return {};
        }
    } else if constexpr(std::ranges::input_range<V>) {
        constexpr auto kind = format_kind<V>;
        if constexpr(kind == range_format::sequence || kind == range_format::set) {
            using element_t = std::ranges::range_value_t<V>;
            static_assert(
                std::default_initializable<element_t>,
                "auto deserialization for ranges requires default-constructible elements");
            static_assert(kota::detail::sequence_insertable<V, element_t>,
                          "cannot auto deserialize range: container does not support insertion");

            KOTA_EXPECTED_TRY(d.begin_array());

            if constexpr(requires { v.clear(); }) {
                v.clear();
            }

            std::size_t seq_index = 0;
            while(true) {
                KOTA_EXPECTED_TRY_V(auto has_next, d.next_element());
                if(!has_next) {
                    break;
                }

                element_t element{};
                auto elem_status = deserialize(d, element);
                if(!elem_status) {
                    auto err = std::move(elem_status).error();
                    err.prepend_index(seq_index);
                    return std::unexpected(std::move(err));
                }

                kota::detail::append_sequence_element(v, std::move(element));
                ++seq_index;
            }

            return d.end_array();
        } else if constexpr(kind == range_format::map) {
            using key_t = typename V::key_type;
            using mapped_t = typename V::mapped_type;

            static_assert(std::default_initializable<mapped_t>,
                          "auto map deserialization requires default-constructible mapped_type");
            static_assert(kota::detail::map_insertable<V, key_t, mapped_t>,
                          "cannot auto deserialize map: container does not support map insertion");

            if constexpr(requires { v.clear(); }) {
                v.clear();
            }

            if constexpr(D::field_mode_v == field_mode::by_name) {
                static_assert(
                    codec::spelling::parseable_map_key<key_t>,
                    "by_name map deserialization requires key_type parseable from string keys");

                KOTA_EXPECTED_TRY(d.begin_object());

                while(true) {
                    KOTA_EXPECTED_TRY_V(auto key, d.next_field());
                    if(!key.has_value()) {
                        break;
                    }

                    auto parsed_key = codec::spelling::parse_map_key<key_t>(*key);
                    if(!parsed_key) {
                        return std::unexpected(E::custom("invalid map key"));
                    }

                    mapped_t mapped{};
                    auto map_val_status = deserialize(d, mapped);
                    if(!map_val_status) {
                        auto err = std::move(map_val_status).error();
                        err.prepend_field(*key);
                        return std::unexpected(std::move(err));
                    }

                    kota::detail::insert_map_entry(v, std::move(*parsed_key), std::move(mapped));
                }

                return d.end_object();
            } else {
                // by_position: length-prefixed sequence of key-value pairs
                static_assert(std::default_initializable<key_t>,
                              "by_position map deserialization requires default-constructible key_type");

                KOTA_EXPECTED_TRY(d.begin_array());

                std::size_t pair_index = 0;
                while(true) {
                    KOTA_EXPECTED_TRY_V(auto has_next, d.next_element());
                    if(!has_next) {
                        break;
                    }

                    key_t key{};
                    auto key_status = deserialize(d, key);
                    if(!key_status) {
                        auto err = std::move(key_status).error();
                        err.prepend_index(pair_index);
                        return std::unexpected(std::move(err));
                    }

                    mapped_t mapped{};
                    auto val_status = deserialize(d, mapped);
                    if(!val_status) {
                        auto err = std::move(val_status).error();
                        err.prepend_index(pair_index);
                        return std::unexpected(std::move(err));
                    }

                    kota::detail::insert_map_entry(v, std::move(key), std::move(mapped));
                    ++pair_index;
                }

                return d.end_array();
            }
        } else {
            static_assert(dependent_false<V>, "cannot auto deserialize the input range");
        }
    } else if constexpr(meta::reflectable_class<V>) {
        return detail::struct_deserialize<config::config_of<D>, E>(d, v);
    } else {
        static_assert(dependent_false<V>,
                      "cannot auto deserialize the value, try to specialize for it");
    }
}

}  // namespace kota::codec
