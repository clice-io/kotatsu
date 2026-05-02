#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "kota/support/config.h"
#include "kota/support/expected_try.h"
#include "kota/support/ranges.h"
#include "kota/support/type_traits.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/meta/struct.h"
#include "kota/codec/detail/apply_behavior.h"
#include "kota/codec/detail/backend.h"
#include "kota/codec/detail/common.h"
#include "kota/codec/detail/config.h"
#include "kota/codec/detail/ser_dispatch.h"
#include "kota/codec/detail/spelling.h"
#include "kota/codec/detail/struct_deserialize.h"
#include "kota/codec/detail/variant_dispatch.h"

namespace kota::codec::detail {

template <typename Config, typename Ctx, typename Attrs, typename V>
auto unified_deserialize(Ctx& ctx, V& v) -> typename Ctx::result_type;

template <typename D>
struct StreamingDeserCtx {
    D& d;
    using error_type = typename D::error_type;
    using result_type = std::expected<void, error_type>;
    constexpr static auto backend_kind_v = backend_kind::streaming;
    constexpr static auto field_mode_v = D::field_mode_v;

    KOTA_ALWAYS_INLINE result_type read_bool(bool& v) {
        return d.deserialize_bool(v);
    }

    template <typename T>
    KOTA_ALWAYS_INLINE result_type read_int(T& v) {
        if constexpr(std::same_as<T, std::int64_t>) {
            return d.deserialize_int(v);
        } else {
            std::int64_t parsed = 0;
            KOTA_EXPECTED_TRY(d.deserialize_int(parsed));
            if(!integral_value_in_range<T>(parsed)) {
                return std::unexpected(error_type::number_out_of_range);
            }
            v = static_cast<T>(parsed);
            return {};
        }
    }

    template <typename T>
    KOTA_ALWAYS_INLINE result_type read_uint(T& v) {
        if constexpr(std::same_as<T, std::uint64_t>) {
            return d.deserialize_uint(v);
        } else {
            std::uint64_t parsed = 0;
            KOTA_EXPECTED_TRY(d.deserialize_uint(parsed));
            if(!integral_value_in_range<T>(parsed)) {
                return std::unexpected(error_type::number_out_of_range);
            }
            v = static_cast<T>(parsed);
            return {};
        }
    }

    template <typename T>
    KOTA_ALWAYS_INLINE result_type read_float(T& v) {
        if constexpr(std::same_as<T, double>) {
            return d.deserialize_float(v);
        } else {
            double parsed = 0;
            KOTA_EXPECTED_TRY(d.deserialize_float(parsed));
            v = static_cast<T>(parsed);
            return {};
        }
    }

    result_type read_char(char& v) {
        return d.deserialize_char(v);
    }

    KOTA_ALWAYS_INLINE result_type read_str(std::string& v) {
        return d.deserialize_str(v);
    }

    result_type read_bytes(std::vector<std::byte>& v) {
        return d.deserialize_bytes(v);
    }

    result_type read_null(bool& is_none) {
        KOTA_EXPECTED_TRY_V(auto r, d.deserialize_none());
        is_none = r;
        return {};
    }

    template <typename V>
    result_type read_optional(V& v) {
        KOTA_EXPECTED_TRY_V(auto is_none, d.deserialize_none());

        if(is_none) {
            v.reset();
            return {};
        }

        using value_t = typename V::value_type;
        if(v.has_value()) {
            return codec::deserialize(d, v.value());
        }

        if constexpr(std::default_initializable<value_t>) {
            v.emplace();
            auto status = codec::deserialize(d, v.value());
            if(!status) {
                v.reset();
                return std::unexpected(status.error());
            }
            return {};
        } else {
            static_assert(dependent_false<V>,
                          "cannot auto deserialize optional<T> without default-constructible T");
        }
    }

    template <typename V>
    result_type read_unique_ptr(V& v) {
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
        KOTA_EXPECTED_TRY(codec::deserialize(d, *value));

        v = std::move(value);
        return {};
    }

    template <typename V>
    result_type read_shared_ptr(V& v) {
        KOTA_EXPECTED_TRY_V(auto is_none, d.deserialize_none());
        if(is_none) {
            v.reset();
            return {};
        }

        using value_t = typename V::element_type;
        static_assert(std::default_initializable<value_t>,
                      "cannot auto deserialize shared_ptr<T> without default-constructible T");

        auto value = std::make_shared<value_t>();
        KOTA_EXPECTED_TRY(codec::deserialize(d, *value));

        v = std::move(value);
        return {};
    }

    template <typename V>
    result_type read_variant(V& v) {
        return d.deserialize_variant(v);
    }

    template <typename Config, typename V>
    KOTA_ALWAYS_INLINE result_type read_tuple(V& v) {
        using E = error_type;
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
                auto result = codec::deserialize(d, element);
                if(!result) {
                    auto err = std::move(result).error();
                    err.prepend_index(tuple_index);
                    element_result = std::unexpected(std::move(err));
                    return false;
                }
                ++tuple_index;
                return true;
            };
            std::apply([&](auto&... elements) { (void)(read_element(elements) && ...); }, v);
            if(!element_result) {
                return std::unexpected(element_result.error());
            }

            // Verify no trailing elements beyond the expected tuple size
            KOTA_EXPECTED_TRY_V(auto trailing, d.next_element());
            if(trailing) {
                return std::unexpected(E::type_mismatch);
            }

            return d.end_array();
        } else {
            // by_position: just deserialize elements directly
            std::expected<void, E> element_result;
            std::size_t tuple_index = 0;
            auto read_element = [&](auto& element) -> bool {
                auto result = codec::deserialize(d, element);
                if(!result) {
                    auto err = std::move(result).error();
                    err.prepend_index(tuple_index);
                    element_result = std::unexpected(std::move(err));
                    return false;
                }
                ++tuple_index;
                return true;
            };
            std::apply([&](auto&... elements) { (void)(read_element(elements) && ...); }, v);
            if(!element_result) {
                return std::unexpected(element_result.error());
            }
            return {};
        }
    }

    template <typename Config, typename V>
    KOTA_ALWAYS_INLINE result_type read_sequence(V& v) {
        using element_t = std::ranges::range_value_t<V>;
        static_assert(std::default_initializable<element_t>,
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
            auto elem_status = codec::deserialize(d, element);
            if(!elem_status) {
                auto err = std::move(elem_status).error();
                err.prepend_index(seq_index);
                return std::unexpected(std::move(err));
            }

            kota::detail::append_sequence_element(v, std::move(element));
            ++seq_index;
        }

        return d.end_array();
    }

    template <typename Config, typename V>
    result_type read_map(V& v) {
        using E = error_type;
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

                // Copy the key before nested deserialization — d.next_field()
                // returns a view that may be invalidated by recursive reads.
                std::string owned_key(*key);

                mapped_t mapped{};
                auto map_val_status = codec::deserialize(d, mapped);
                if(!map_val_status) {
                    auto err = std::move(map_val_status).error();
                    err.prepend_field(owned_key);
                    return std::unexpected(std::move(err));
                }

                kota::detail::insert_map_entry(v, std::move(*parsed_key), std::move(mapped));
            }

            return d.end_object();
        } else {
            // by_position: length-prefixed sequence of key-value pairs
            static_assert(
                std::default_initializable<key_t>,
                "by_position map deserialization requires default-constructible key_type");

            KOTA_EXPECTED_TRY(d.begin_array());

            std::size_t pair_index = 0;
            while(true) {
                KOTA_EXPECTED_TRY_V(auto has_next, d.next_element());
                if(!has_next) {
                    break;
                }

                key_t key{};
                auto key_status = codec::deserialize(d, key);
                if(!key_status) {
                    auto err = std::move(key_status).error();
                    err.prepend_index(pair_index);
                    return std::unexpected(std::move(err));
                }

                mapped_t mapped{};
                auto val_status = codec::deserialize(d, mapped);
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
    }

    template <typename Config, typename V>
    KOTA_ALWAYS_INLINE result_type read_struct(V& v) {
        return struct_deserialize<Config, error_type>(d, v);
    }
};

template <typename Config, typename Ctx, typename Attrs, typename V>
KOTA_ALWAYS_INLINE auto unified_deserialize(Ctx& ctx, V& v) -> typename Ctx::result_type {
    using U = std::remove_cvref_t<V>;
    using E = typename Ctx::error_type;

    if constexpr(meta::annotated_type<U>) {
        using attrs_t = typename U::attrs;
        auto&& value = meta::annotated_value(v);
        using value_t = std::remove_cvref_t<decltype(value)>;

        static_assert(!tuple_has_v<attrs_t, meta::attrs::skip>,
                      "schema::skip is only valid for struct fields");
        static_assert(!tuple_has_v<attrs_t, meta::attrs::flatten>,
                      "schema::flatten is only valid for struct fields");

        if constexpr(Ctx::backend_kind_v == backend_kind::arena) {
            // Arena handles annotations at field level — strip and recurse.
            return unified_deserialize<Config, Ctx, Attrs>(ctx, value);
        } else if constexpr(is_specialization_of<std::variant, value_t> &&
                            tuple_any_of_v<attrs_t, meta::is_tagged_attr>) {
            using tag_attr = tuple_find_t<attrs_t, meta::is_tagged_attr>;
            constexpr auto strategy = meta::tagged_strategy_of<tag_attr>;
            if constexpr(strategy == meta::tagged_strategy::external) {
                return deserialize_externally_tagged<E>(ctx.d, value, tag_attr{});
            } else if constexpr(strategy == meta::tagged_strategy::internal) {
                return deserialize_internally_tagged<E>(ctx.d, value, tag_attr{});
            } else {
                return deserialize_adjacently_tagged<E>(ctx.d, value, tag_attr{});
            }
        } else if constexpr(tuple_count_of_v<attrs_t, meta::is_behavior_provider> > 0) {
            if constexpr(Ctx::backend_kind_v == backend_kind::streaming) {
                return *apply_deserialize_behavior<attrs_t, value_t, E>(
                    value,
                    [&](auto& inner) { return codec::deserialize(ctx.d, inner); },
                    [&](auto tag, auto& inner) -> std::expected<void, E> {
                        using Adapter = typename decltype(tag)::type;
                        return Adapter::deserialize(ctx.d, inner);
                    });
            } else {
                // Arena behavior attrs are handled before entering unified_deserialize.
                return unified_deserialize<Config, Ctx, attrs_t>(ctx, value);
            }
        } else if constexpr(meta::reflectable_class<value_t> &&
                            (tuple_has_spec_v<attrs_t, meta::attrs::rename_all> ||
                             tuple_has_v<attrs_t, meta::attrs::deny_unknown_fields>)) {
            using base_config_t = Config;
            using struct_config_t = annotated_struct_config_t<base_config_t, attrs_t>;
            return ctx.template read_struct<struct_config_t>(value);
        } else {
            return unified_deserialize<Config, Ctx, Attrs>(ctx, value);
        }
    } else if constexpr(tuple_count_of_v<Attrs, meta::is_behavior_provider> > 0) {
        // Behavior attrs passed from outer context (e.g. struct field attrs for arena)
        if constexpr(Ctx::backend_kind_v == backend_kind::streaming) {
            return *apply_deserialize_behavior<Attrs, U, E>(
                v,
                [&](auto& inner) { return codec::deserialize(ctx.d, inner); },
                [&](auto tag, auto& inner) -> std::expected<void, E> {
                    using Adapter = typename decltype(tag)::type;
                    if constexpr(requires {
                                     Adapter::from_wire(
                                         std::declval<typename Adapter::wire_type>());
                                 }) {
                        using wire_t = typename Adapter::wire_type;
                        wire_t wire{};
                        KOTA_EXPECTED_TRY(codec::deserialize(ctx.d, wire));
                        inner = Adapter::from_wire(std::move(wire));
                        return {};
                    } else if constexpr(requires { Adapter::deserialize(ctx.d, inner); }) {
                        return Adapter::deserialize(ctx.d, inner);
                    } else {
                        return codec::deserialize(ctx.d, inner);
                    }
                });
        } else {
            // Arena: behavior attrs already handled in decode_value_at before calling us.
            return unified_deserialize<Config, Ctx, std::tuple<>>(ctx, v);
        }
    } else if constexpr(std::is_enum_v<U>) {
        using underlying_t = std::underlying_type_t<U>;
        if constexpr(std::is_signed_v<underlying_t>) {
            std::int64_t parsed = 0;
            KOTA_EXPECTED_TRY(ctx.read_int(parsed));
            if(!integral_value_in_range<underlying_t>(parsed)) {
                return std::unexpected(E::number_out_of_range);
            }
            v = static_cast<U>(static_cast<underlying_t>(parsed));
            return {};
        } else {
            std::uint64_t parsed = 0;
            KOTA_EXPECTED_TRY(ctx.read_uint(parsed));
            if(!integral_value_in_range<underlying_t>(parsed)) {
                return std::unexpected(E::number_out_of_range);
            }
            v = static_cast<U>(static_cast<underlying_t>(parsed));
            return {};
        }
    } else if constexpr(bool_like<U>) {
        return ctx.read_bool(v);
    } else if constexpr(int_like<U>) {
        return ctx.read_int(v);
    } else if constexpr(uint_like<U>) {
        return ctx.read_uint(v);
    } else if constexpr(floating_like<U>) {
        return ctx.read_float(v);
    } else if constexpr(char_like<U>) {
        return ctx.read_char(v);
    } else if constexpr(std::same_as<U, std::string> || std::derived_from<U, std::string>) {
        return ctx.read_str(static_cast<std::string&>(v));
    } else if constexpr(std::same_as<U, std::vector<std::byte>>) {
        return ctx.read_bytes(v);
    } else if constexpr(null_like<U>) {
        bool is_none = false;
        KOTA_EXPECTED_TRY(ctx.read_null(is_none));
        if(is_none) {
            v = U{};
            return {};
        }
        return std::unexpected(E::type_mismatch);
    } else if constexpr(is_specialization_of<std::optional, U>) {
        return ctx.read_optional(v);
    } else if constexpr(is_specialization_of<std::unique_ptr, U>) {
        return ctx.read_unique_ptr(v);
    } else if constexpr(is_specialization_of<std::shared_ptr, U>) {
        return ctx.read_shared_ptr(v);
    } else if constexpr(is_specialization_of<std::variant, U>) {
        return ctx.read_variant(v);
    } else if constexpr(tuple_like<U>) {
        return ctx.template read_tuple<Config>(v);
    } else if constexpr(std::ranges::input_range<U>) {
        constexpr auto kind = format_kind<U>;
        if constexpr(kind == range_format::map) {
            return ctx.template read_map<Config>(v);
        } else {
            return ctx.template read_sequence<Config>(v);
        }
    } else if constexpr(meta::reflectable_class<U>) {
        return ctx.template read_struct<Config>(v);
    } else {
        static_assert(dependent_false<V>,
                      "cannot auto deserialize the value, try to specialize for it");
    }
}

}  // namespace kota::codec::detail
