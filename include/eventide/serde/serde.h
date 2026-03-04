#pragma once

#include <limits>

#include "annotation.h"
#include "attrs.h"
#include "config.h"
#include "traits.h"
#include "eventide/common/ranges.h"
#include "eventide/reflection/enum.h"
#include "eventide/reflection/struct.h"

namespace eventide::serde {

template <typename S, typename T>
struct serialize_traits;

template <typename D, typename T>
struct deserialize_traits;

namespace detail {

template <typename Result>
constexpr Result unexpected_number_out_of_range() {
    using error_t = typename Result::error_type;
    if constexpr(requires { error_t::number_out_of_range; }) {
        return std::unexpected(error_t::number_out_of_range);
    } else if constexpr(requires { error_t::invalid_type; }) {
        return std::unexpected(error_t::invalid_type);
    } else if constexpr(std::is_enum_v<error_t>) {
        return std::unexpected(static_cast<error_t>(1));
    } else {
        return std::unexpected(error_t{});
    }
}

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

template <typename E, typename SerializeStruct, typename Field>
constexpr auto serialize_struct_field(SerializeStruct& s_struct, Field field)
    -> std::expected<void, E> {
    using field_t = typename std::remove_cvref_t<decltype(field)>::type;
    std::string scratch;
    auto mapped_name = config::apply_field_rename(true, field.name(), scratch);

    if constexpr(!annotated_type<field_t>) {
        return s_struct.serialize_field(mapped_name, field.value());
    } else {
        using attrs_t = typename std::remove_cvref_t<field_t>::attrs;
        auto&& value = annotated_value(field.value());
        using value_t = std::remove_cvref_t<decltype(value)>;

        auto terminal = []<typename Ctx>(Ctx ctx) -> std::expected<void, E> {
            return ctx.s.serialize_field(ctx.name, ctx.value);
        };
        serialize_field_ctx<SerializeStruct, value_t> ctx{
            .s = s_struct,
            .name = mapped_name,
            .value = value,
        };
        return run_attrs_hook<attrs_t>(ctx, terminal);
    }
}

template <typename E, typename DeserializeStruct, typename Field>
constexpr auto deserialize_struct_field(DeserializeStruct& d_struct,
                                        std::string_view key_name,
                                        Field field) -> std::expected<bool, E> {
    using field_t = typename std::remove_cvref_t<decltype(field)>::type;
    std::string scratch;
    // We compare against incoming serialized keys, so we must map the reflected
    // internal field name to its serialized form (same direction as serialize).
    auto mapped_name = config::apply_field_rename(true, field.name(), scratch);

    if constexpr(!annotated_type<field_t>) {
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

        auto probe_terminal =
            []<typename Ctx>(Ctx ctx) -> std::expected<deserialize_field_probe_decision, E> {
            if(ctx.alias_matched || ctx.key_name == ctx.field_name) {
                return deserialize_field_probe_decision::match_deferred;
            }
            return deserialize_field_probe_decision::no_match;
        };
        deserialize_field_probe_ctx<DeserializeStruct, value_t> probe_ctx{
            .d = d_struct,
            .key_name = key_name,
            .field_name = mapped_name,
            .value = value,
        };
        auto probe_result = run_attrs_hook<attrs_t>(probe_ctx, probe_terminal);
        if(!probe_result) {
            return std::unexpected(probe_result.error());
        }
        if(*probe_result == deserialize_field_probe_decision::no_match) {
            return false;
        }
        if(*probe_result == deserialize_field_probe_decision::match_consumed) {
            return true;
        }

        auto consume_terminal = []<typename Ctx>(Ctx ctx) -> std::expected<void, E> {
            return ctx.d.deserialize_value(ctx.value);
        };
        deserialize_field_consume_ctx<DeserializeStruct, value_t> consume_ctx{
            .d = d_struct,
            .key_name = key_name,
            .field_name = mapped_name,
            .value = value,
        };
        auto consume_result = run_attrs_hook<attrs_t>(consume_ctx, consume_terminal);
        if(!consume_result) {
            return std::unexpected(consume_result.error());
        }
        return true;
    }
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
        serialize_value_ctx<S, typename V::annotated_type> ctx{.s = s, .value = annotated_value(v)};
        return run_attrs_hook<typename V::attrs>(ctx, [](auto ctx) {
            return serialize(ctx.s, ctx.value);
        });
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
        return s.serialize_none();
    } else if constexpr(is_specialization_of<std::optional, V>) {
        if(v.has_value()) {
            return s.serialize_some(v.value());
        } else {
            return s.serialize_none();
        }
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
        auto s_struct = s.serialize_struct(refl::type_name<V>(), refl::field_count<V>());
        if(!s_struct) {
            return std::unexpected(s_struct.error());
        }

        std::expected<void, E> field_result;
        refl::for_each(v, [&](auto field) {
            auto result = detail::serialize_struct_field<E>(*s_struct, field);
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

        auto terminal = []<typename Ctx>(Ctx ctx) -> std::expected<void, E> {
            return deserialize(ctx.d, ctx.value);
        };
        deserialize_value_ctx<D, value_t> ctx{
            .d = d,
            .value = value,
        };
        return run_attrs_hook<attrs_t>(ctx, terminal);
    } else if constexpr(std::is_enum_v<V>) {
        using underlying_t = std::underlying_type_t<V>;
        if constexpr(std::is_signed_v<underlying_t>) {
            std::int64_t parsed = 0;
            auto status = d.deserialize_int(parsed);
            if(!status) {
                return std::unexpected(status.error());
            }
            if(!detail::integral_value_in_range<underlying_t>(parsed)) {
                return detail::unexpected_number_out_of_range<std::expected<void, E>>();
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
                return detail::unexpected_number_out_of_range<std::expected<void, E>>();
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
        return std::unexpected(E{});
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
            return d.deserialize_some(v.value());
        }

        if constexpr(std::default_initializable<value_t>) {
            v.emplace();
            auto status = d.deserialize_some(v.value());
            if(!status) {
                v.reset();
                return std::unexpected(status.error());
            }
            return {};
        } else {
            static_assert(dependent_false<V>,
                          "cannot auto deserialize optional<T> without default-constructible T");
        }
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
                    } else if constexpr(std::default_initializable<E>) {
                        return std::unexpected(E{});
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
                auto status = detail::deserialize_struct_field<E>(*d_struct, key_name, field);
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
