#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/expected_try.h"
#include "kota/support/ranges.h"
#include "kota/support/type_traits.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/meta/struct.h"
#include "kota/codec/backend.h"
#include "kota/codec/config.h"
#include "kota/codec/detail/apply_behavior.h"
#include "kota/codec/detail/common.h"
#include "kota/codec/detail/fwd.h"
#include "kota/codec/detail/struct_serialize.h"
#include "kota/codec/spelling.h"
#include "kota/codec/traits.h"

namespace kota::codec::detail {

// annotated_struct_config — compute config type from struct-level schema attrs.
template <typename BaseConfig,
          typename Attrs,
          bool HasRenameAll = tuple_has_spec_v<Attrs, meta::attrs::rename_all>,
          bool HasDenyUnknown = tuple_has_v<Attrs, meta::attrs::deny_unknown_fields>>
struct annotated_struct_config {
    using type = BaseConfig;
};

template <typename BaseConfig, typename Attrs>
struct annotated_struct_config<BaseConfig, Attrs, true, false> {
    struct type {
        using field_rename = typename tuple_find_spec_t<Attrs, meta::attrs::rename_all>::policy;
    };
};

template <typename BaseConfig, typename Attrs>
struct annotated_struct_config<BaseConfig, Attrs, false, true> {
    struct type {
        static constexpr bool deny_unknown_fields = true;
    };
};

template <typename BaseConfig, typename Attrs>
struct annotated_struct_config<BaseConfig, Attrs, true, true> {
    struct type {
        using field_rename = typename tuple_find_spec_t<Attrs, meta::attrs::rename_all>::policy;
        static constexpr bool deny_unknown_fields = true;
    };
};

template <typename BaseConfig, typename Attrs>
using annotated_struct_config_t = typename annotated_struct_config<BaseConfig, Attrs>::type;

// Forward declare the unified dispatch.
template <typename Config, typename Ctx, typename Attrs, typename V>
auto unified_serialize(Ctx& ctx, const V& v) -> typename Ctx::result_type;

// ─────────────────────────────────────────────────────────────────────────────
// StreamingCtx — wraps a streaming serializer for the unified dispatch.
// ─────────────────────────────────────────────────────────────────────────────

template <typename S>
struct StreamingCtx {
    S& s;
    using value_type = typename S::value_type;
    using error_type = typename S::error_type;
    using result_type = std::expected<value_type, error_type>;
    static constexpr auto backend_kind_v = backend_kind::streaming;
    static constexpr auto field_mode_v = S::field_mode_v;

    // Leaf emitters — integral types pass through (implicit widening to int64/uint64)
    template <typename T>
    result_type emit_integral(T v) {
        if constexpr(std::is_signed_v<T>) {
            return s.serialize_int(v);
        } else {
            return s.serialize_uint(v);
        }
    }

    result_type emit_bool(bool v) { return s.serialize_bool(v); }
    result_type emit_float(double v) { return s.serialize_float(v); }
    result_type emit_char(char v) { return s.serialize_char(v); }
    result_type emit_str(std::string_view v) { return s.serialize_str(v); }
    result_type emit_bytes(std::span<const std::byte> v) { return s.serialize_bytes(v); }
    result_type emit_null() { return s.serialize_null(); }

    // Nullable: recurse via public codec::serialize
    template <typename V>
    result_type emit_some(const V& v) { return s.serialize_some(v); }

    // Variant: delegate to serializer
    template <typename Config, typename V>
    result_type emit_variant(const V& v) { return s.serialize_variant(v); }

    // Tuple: begin_array + recurse per element + end_array (by_name) or direct (by_position)
    template <typename Config, typename V>
    result_type emit_tuple(const V& v) {
        using E = error_type;
        constexpr std::size_t N = std::tuple_size_v<std::remove_cvref_t<V>>;
        if constexpr(S::field_mode_v == field_mode::by_name) {
            KOTA_EXPECTED_TRY(s.begin_array(N));
            std::expected<void, E> element_result;
            auto for_each = [&](const auto& element) -> bool {
                auto r = emit_element_value<S, E>(s, codec::serialize(s, element));
                if(!r) {
                    element_result = std::unexpected(r.error());
                    return false;
                }
                return true;
            };
            std::apply([&](const auto&... elements) { (for_each(elements) && ...); }, v);
            if(!element_result) {
                return std::unexpected(element_result.error());
            }
            return s.end_array();
        } else {
            // by_position: serialize elements directly without framing
            std::expected<void, E> element_result;
            auto for_each = [&](const auto& element) -> bool {
                auto r = codec::serialize(s, element);
                if(!r) {
                    element_result = std::unexpected(r.error());
                    return false;
                }
                return true;
            };
            std::apply([&](const auto&... elements) { (for_each(elements) && ...); }, v);
            if(!element_result) {
                return std::unexpected(element_result.error());
            }
            return {};
        }
    }

    // Sequence: begin_array + recurse per element + end_array
    template <typename Config, typename V>
    result_type emit_sequence(const V& v) {
        using E = error_type;
        std::optional<std::size_t> len = std::nullopt;
        if constexpr(std::ranges::sized_range<V>) {
            len = static_cast<std::size_t>(std::ranges::size(v));
        }

        KOTA_EXPECTED_TRY(s.begin_array(len));

        for(auto&& e: v) {
            { auto _r = emit_element_value<S, E>(s, codec::serialize(s, e)); if(!_r) return std::unexpected(_r.error()); }
        }

        return s.end_array();
    }

    // Map: begin_object + field + recurse (by_name) or begin_array + recurse pairs (by_position)
    template <typename Config, typename V>
    result_type emit_map(const V& v) {
        using E = error_type;
        std::optional<std::size_t> len = std::nullopt;
        if constexpr(std::ranges::sized_range<V>) {
            len = static_cast<std::size_t>(std::ranges::size(v));
        }

        if constexpr(S::field_mode_v == field_mode::by_name) {
            KOTA_EXPECTED_TRY(s.begin_object(len.value_or(0)));
            for(auto&& [key, value]: v) {
                KOTA_EXPECTED_TRY(s.field(codec::spelling::map_key_to_string(key)));
                { auto _r = emit_field_value<S, E>(s, codec::serialize(s, value)); if(!_r) return std::unexpected(_r.error()); }
            }
            return s.end_object();
        } else {
            // by_position: write length + key-value pairs
            KOTA_EXPECTED_TRY(s.begin_array(len));
            for(auto&& [key, value]: v) {
                KOTA_EXPECTED_TRY(codec::serialize(s, key));
                KOTA_EXPECTED_TRY(codec::serialize(s, value));
            }
            return s.end_array();
        }
    }

    // Struct: delegate to existing struct_serialize
    template <typename Config, typename V>
    result_type emit_struct(const V& v) {
        return detail::struct_serialize<Config, error_type>(s, v);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ArenaFieldCtx — wraps an arena backend + table builder + slot for field-level dispatch.
// ─────────────────────────────────────────────────────────────────────────────

}  // namespace kota::codec::detail

// Forward declarations for arena helper functions (defined in arena_encode.h).
namespace kota::codec::arena::detail {

template <typename Config, typename B, typename T>
auto encode_table(B& b, const T& value)
    -> std::expected<typename B::table_ref, typename B::error_type>;

template <typename Config, typename B, typename T>
auto encode_tuple_like(B& b, const T& value)
    -> std::expected<typename B::table_ref, typename B::error_type>;

template <typename Config, typename B, typename T>
auto encode_variant(B& b, const T& value)
    -> std::expected<typename B::table_ref, typename B::error_type>;

template <typename Config, typename B, typename T>
auto encode_sequence(B& b, const T& range)
    -> std::expected<typename B::vector_ref, typename B::error_type>;

template <typename Config, typename B, typename T>
auto encode_map(B& b, const T& map)
    -> std::expected<typename B::vector_ref, typename B::error_type>;

}  // namespace kota::codec::arena::detail

namespace kota::codec::detail {

template <typename B>
struct ArenaFieldCtx {
    B& backend;
    typename B::TableBuilder& tb;
    typename B::slot_id sid;
    using error_type = typename B::error_type;
    using result_type = std::expected<void, error_type>;
    static constexpr auto backend_kind_v = backend_kind::arena;

    // Scalars: add directly to table builder, preserving the original type.
    template <typename T>
    result_type emit_integral(T v) {
        tb.add_scalar(sid, v);
        return {};
    }

    result_type emit_bool(bool v) {
        tb.add_scalar(sid, v);
        return {};
    }

    result_type emit_float(double v) {
        // Preserve float vs double when possible
        tb.add_scalar(sid, v);
        return {};
    }

    // Overload for original float type preservation
    template <typename T>
        requires(std::same_as<T, float> || std::same_as<T, double>)
    result_type emit_float_typed(T v) {
        tb.add_scalar(sid, v);
        return {};
    }

    result_type emit_char(char v) {
        tb.add_scalar(sid, static_cast<std::int8_t>(v));
        return {};
    }

    result_type emit_str(std::string_view v) {
        KOTA_EXPECTED_TRY_V(auto r, backend.alloc_string(v));
        tb.add_offset(sid, r);
        return {};
    }

    result_type emit_bytes(std::span<const std::byte> v) {
        KOTA_EXPECTED_TRY_V(auto r, backend.alloc_bytes(v));
        tb.add_offset(sid, r);
        return {};
    }

    result_type emit_null() {
        // optional-absent: just don't write
        return {};
    }

    // Nullable: recurse with same ctx
    template <typename V>
    result_type emit_some(const V& v) {
        // Recurse through unified_serialize with same ctx (same slot)
        return unified_serialize<config::default_config, ArenaFieldCtx<B>, std::tuple<>>(*this, v);
    }

    // Variant: encode_variant + add_offset
    template <typename Config, typename V>
    result_type emit_variant(const V& v) {
        KOTA_EXPECTED_TRY_V(auto r, kota::codec::arena::detail::encode_variant<Config>(backend, v));
        tb.add_offset(sid, r);
        return {};
    }

    // Tuple: encode_tuple_like + add_offset
    template <typename Config, typename V>
    result_type emit_tuple(const V& v) {
        KOTA_EXPECTED_TRY_V(auto r, kota::codec::arena::detail::encode_tuple_like<Config>(backend, v));
        tb.add_offset(sid, r);
        return {};
    }

    // Sequence: encode_sequence + add_offset
    template <typename Config, typename V>
    result_type emit_sequence(const V& v) {
        KOTA_EXPECTED_TRY_V(auto r, kota::codec::arena::detail::encode_sequence<Config>(backend, v));
        tb.add_offset(sid, r);
        return {};
    }

    // Map: encode_map + add_offset
    template <typename Config, typename V>
    result_type emit_map(const V& v) {
        KOTA_EXPECTED_TRY_V(auto r, kota::codec::arena::detail::encode_map<Config>(backend, v));
        tb.add_offset(sid, r);
        return {};
    }

    // Struct: encode_table + add_offset (or inline_struct if supported)
    template <typename Config, typename V>
    result_type emit_struct(const V& v) {
        using clean = std::remove_cvref_t<V>;
        if constexpr(B::template can_inline_struct_field<clean>) {
            tb.add_inline_struct(sid, static_cast<clean>(v));
            return {};
        } else {
            KOTA_EXPECTED_TRY_V(auto r, kota::codec::arena::detail::encode_table<Config>(backend, v));
            tb.add_offset(sid, r);
            return {};
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// unified_serialize — THE single type cascade for both streaming and arena.
// ─────────────────────────────────────────────────────────────────────────────

template <typename Config, typename Ctx, typename Attrs, typename V>
auto unified_serialize(Ctx& ctx, const V& v) -> typename Ctx::result_type {
    using U = std::remove_cvref_t<V>;
    using E = typename Ctx::error_type;

    // === ANNOTATED TYPE HANDLING ===
    if constexpr(meta::annotated_type<U>) {
        using attrs_t = typename U::attrs;
        auto&& value = meta::annotated_value(v);
        using value_t = std::remove_cvref_t<decltype(value)>;

        // Field-only attrs at value level are errors
        static_assert(!tuple_has_v<attrs_t, meta::attrs::skip>,
                      "schema::skip is only valid for struct fields");
        static_assert(!tuple_has_v<attrs_t, meta::attrs::flatten>,
                      "schema::flatten is only valid for struct fields");

        if constexpr(Ctx::backend_kind_v == backend_kind::arena) {
            // Arena: annotations are transparent — just unwrap and recurse
            // with the outer Attrs (behavior attrs come from struct field schema, not annotation)
            return unified_serialize<Config, Ctx, Attrs>(ctx, value);
        }
        // Tagged variant dispatch — streaming only
        else if constexpr(is_specialization_of<std::variant, value_t> &&
                     tuple_any_of_v<attrs_t, meta::is_tagged_attr>) {
            using tag_attr = tuple_find_t<attrs_t, meta::is_tagged_attr>;
            constexpr auto strategy = meta::tagged_strategy_of<tag_attr>;
            if constexpr(strategy == meta::tagged_strategy::external) {
                return serialize_externally_tagged<E>(ctx.s, value, tag_attr{});
            } else if constexpr(strategy == meta::tagged_strategy::internal) {
                return serialize_internally_tagged<E>(ctx.s, value, tag_attr{});
            } else {
                return serialize_adjacently_tagged<E>(ctx.s, value, tag_attr{});
            }
        }
        // Behavior: with/as/enum_string — delegate to apply_serialize_behavior
        else if constexpr(tuple_count_of_v<attrs_t, meta::is_behavior_provider> > 0) {
            return *apply_serialize_behavior<attrs_t, value_t, E>(
                value,
                [&](const auto& inner) { return codec::serialize(ctx.s, inner); },
                [&](auto tag, const auto& inner) {
                    using Adapter = typename decltype(tag)::type;
                    return Adapter::serialize(ctx.s, inner);
                });
        }
        // Struct-level schema attrs for annotated structs
        else if constexpr(meta::reflectable_class<value_t> &&
                          (tuple_has_spec_v<attrs_t, meta::attrs::rename_all> ||
                           tuple_has_v<attrs_t, meta::attrs::deny_unknown_fields>)) {
            using base_config_t = Config;
            using struct_config_t = annotated_struct_config_t<base_config_t, attrs_t>;
            return ctx.template emit_struct<struct_config_t>(value);
        }
        // Default: serialize the underlying value
        else {
            return unified_serialize<Config, Ctx, Attrs>(ctx, value);
        }
    }
    // === BEHAVIOR ATTRS FROM STRUCT FIELD (passed via Attrs template param) ===
    else if constexpr(tuple_count_of_v<Attrs, meta::is_behavior_provider> > 0) {
        if constexpr(Ctx::backend_kind_v == backend_kind::streaming) {
            // Streaming: behavior attrs are handled at struct_serialize level,
            // this path shouldn't normally be hit, but handle it gracefully
            return *apply_serialize_behavior<Attrs, U, E>(
                v,
                [&](const auto& inner) { return codec::serialize(ctx.s, inner); },
                [&](auto tag, const auto& inner) {
                    using Adapter = typename decltype(tag)::type;
                    return Adapter::serialize(ctx.s, inner);
                });
        } else {
            // Arena behavior dispatch
            if constexpr(tuple_has_spec_v<Attrs, meta::behavior::as>) {
                using target = typename tuple_find_spec_t<Attrs, meta::behavior::as>::target;
                target tmp = static_cast<target>(v);
                return unified_serialize<Config, Ctx, std::tuple<>>(ctx, tmp);
            } else if constexpr(tuple_has_spec_v<Attrs, meta::behavior::enum_string>) {
                static_assert(std::is_enum_v<U>, "enum_string requires an enum type");
                std::string_view name = meta::enum_name(static_cast<U>(v));
                return ctx.emit_str(name);
            } else if constexpr(tuple_has_spec_v<Attrs, meta::behavior::with>) {
                using adapter = typename tuple_find_spec_t<Attrs, meta::behavior::with>::adapter;
                using wire_t = typename adapter::wire_type;
                wire_t wire = adapter::to_wire(v);
                return unified_serialize<Config, Ctx, std::tuple<>>(ctx, wire);
            } else {
                return unified_serialize<Config, Ctx, std::tuple<>>(ctx, v);
            }
        }
    }
    // === TYPE CLASSIFICATION CASCADE ===
    else if constexpr(std::is_enum_v<U>) {
        using underlying_t = std::underlying_type_t<U>;
        if constexpr(Ctx::backend_kind_v == backend_kind::streaming) {
            // Streaming: always widen to int64/uint64 (matching original behavior)
            if constexpr(std::is_signed_v<underlying_t>) {
                return ctx.emit_integral(static_cast<std::int64_t>(static_cast<underlying_t>(v)));
            } else {
                return ctx.emit_integral(static_cast<std::uint64_t>(static_cast<underlying_t>(v)));
            }
        } else {
            // Arena: preserve underlying type
            return unified_serialize<Config, Ctx, std::tuple<>>(ctx, static_cast<underlying_t>(v));
        }
    }
    else if constexpr(bool_like<U>) {
        return ctx.emit_bool(static_cast<bool>(v));
    }
    else if constexpr(int_like<U> || uint_like<U>) {
        return ctx.emit_integral(static_cast<U>(v));
    }
    else if constexpr(floating_like<U>) {
        if constexpr(Ctx::backend_kind_v == backend_kind::arena) {
            if constexpr(std::same_as<U, float> || std::same_as<U, double>) {
                return ctx.emit_float_typed(static_cast<U>(v));
            } else {
                return ctx.emit_float(static_cast<double>(v));
            }
        } else {
            return ctx.emit_float(static_cast<double>(v));
        }
    }
    else if constexpr(char_like<U>) {
        return ctx.emit_char(static_cast<char>(v));
    }
    else if constexpr(str_like<U>) {
        return ctx.emit_str(std::string_view(v));
    }
    else if constexpr(bytes_like<U>) {
        return ctx.emit_bytes(std::span<const std::byte>(v));
    }
    else if constexpr(null_like<U>) {
        if constexpr(Ctx::backend_kind_v == backend_kind::arena) {
            // Arena: only nullptr_t is truly null (matches old behavior).
            // monostate/nullopt_t may be reflectable_class and need struct encoding.
            if constexpr(std::same_as<U, std::nullptr_t>) {
                return ctx.emit_null();
            } else if constexpr(meta::reflectable_class<U>) {
                return ctx.template emit_struct<Config>(v);
            } else {
                return ctx.emit_null();
            }
        } else {
            return ctx.emit_null();
        }
    }
    else if constexpr(is_specialization_of<std::optional, U>) {
        if(v.has_value()) {
            return ctx.emit_some(v.value());
        } else {
            return ctx.emit_null();
        }
    }
    else if constexpr(is_specialization_of<std::unique_ptr, U> ||
                      is_specialization_of<std::shared_ptr, U>) {
        if(v) {
            return ctx.emit_some(*v);
        }
        return ctx.emit_null();
    }
    else if constexpr(is_specialization_of<std::variant, U>) {
        return ctx.template emit_variant<Config>(v);
    }
    else if constexpr(Ctx::backend_kind_v == backend_kind::arena && std::ranges::input_range<U>) {
        // Arena: ranges before tuple_like (std::array is a range, encoded as vector)
        constexpr auto kind = format_kind<U>;
        if constexpr(kind == range_format::map) {
            return ctx.template emit_map<Config>(v);
        } else {
            return ctx.template emit_sequence<Config>(v);
        }
    }
    else if constexpr(tuple_like<U>) {
        return ctx.template emit_tuple<Config>(v);
    }
    else if constexpr(std::ranges::input_range<U>) {
        // Streaming: ranges after tuple_like
        constexpr auto kind = format_kind<U>;
        if constexpr(kind == range_format::map) {
            return ctx.template emit_map<Config>(v);
        } else {
            return ctx.template emit_sequence<Config>(v);
        }
    }
    else if constexpr(meta::reflectable_class<U>) {
        return ctx.template emit_struct<Config>(v);
    }
    else {
        static_assert(dependent_false<V>,
                      "cannot auto serialize the value, try to specialize for it");
    }
}

}  // namespace kota::codec::detail
