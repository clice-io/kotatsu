#pragma once

#include <concepts>
#include <cstdint>
#include <limits>
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

#include "kota/support/ranges.h"
#include "kota/support/type_traits.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/meta/struct.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/detail/backend.h"
#include "kota/codec/detail/common.h"
#include "kota/codec/detail/config.h"
#include "kota/codec/detail/ser_dispatch.h"
#include "kota/codec/detail/spelling.h"
#include "kota/codec/visitors/seq_visitor.h"
#include "kota/codec/visitors/map_visitor.h"
#include "kota/codec/visitors/struct_visitor.h"
#include "kota/codec/visitors/variant_visitor.h"
#include "kota/codec/visitors/variant_scoring.h"

namespace kota::codec {

/// User extension point: specialize this to provide custom deserialization
template <typename Backend, typename T>
struct custom_deserialize;

/// Helper: resolve the "canonical" backend type for custom_deserialize lookup.
/// If Backend has a base_backend_type, use that; otherwise use Backend itself.
template <typename Backend>
struct canonical_backend {
    using type = Backend;
};

template <typename Backend>
    requires requires { typename Backend::base_backend_type; }
struct canonical_backend<Backend> {
    using type = typename Backend::base_backend_type;
};

template <typename Backend>
using canonical_backend_t = typename canonical_backend<Backend>::type;

template <typename Backend, typename T>
concept has_custom_deserialize =
    requires(typename Backend::value_type& v, T& out) {
        {
            custom_deserialize<canonical_backend_t<Backend>, T>::read(v, out)
        } -> std::same_as<typename Backend::error_type>;
    };

/// Core dispatch: deserialize<Backend>(source, T& out) -> Backend::error_type
template <typename Backend, typename T>
auto deserialize(typename Backend::value_type& src, T& out) -> typename Backend::error_type {
    using U = std::remove_cvref_t<T>;
    using E = typename Backend::error_type;

    // 1. Custom deserialize
    if constexpr(has_custom_deserialize<Backend, U>) {
        return custom_deserialize<canonical_backend_t<Backend>, U>::read(src, out);
    }
    // 1b. Annotated types: unwrap annotation and recurse
    else if constexpr(meta::annotated_type<U>) {
        using attrs_t = typename U::attrs;
        auto&& value = meta::annotated_value(out);
        using value_t = std::remove_cvref_t<decltype(value)>;

        // Tagged variant annotation
        if constexpr(is_specialization_of<std::variant, value_t> &&
                     tuple_any_of_v<attrs_t, meta::is_tagged_attr>) {
            using tag_attr = tuple_find_t<attrs_t, meta::is_tagged_attr>;
            constexpr auto strategy = meta::tagged_strategy_of<tag_attr>;
            if constexpr(strategy == meta::tagged_strategy::external) {
                return deserialize_externally_tagged<Backend, tag_attr>(src, value);
            } else if constexpr(strategy == meta::tagged_strategy::internal) {
                return deserialize_internally_tagged<Backend, tag_attr>(src, value);
            } else {
                return deserialize_adjacently_tagged<Backend, tag_attr>(src, value);
            }
        }
        // Enum string annotation
        else if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::enum_string>) {
            using Policy =
                typename tuple_find_spec_t<attrs_t, meta::behavior::enum_string>::policy;
            static_assert(std::is_enum_v<value_t>,
                          "behavior::enum_string requires an enum type");
            std::string_view text;
            auto err = Backend::read_string(src, text);
            if(err != Backend::success)
                return err;
            auto mapped = spelling::map_string_to_enum<value_t, Policy>(text);
            if(!mapped) {
                if constexpr(requires { Backend::report_unknown_enum(text); }) {
                    Backend::report_unknown_enum(text);
                }
                return Backend::type_mismatch;
            }
            value = *mapped;
            return Backend::success;
        }
        // Struct with struct-level attrs (rename_all, deny_unknown_fields)
        else if constexpr(meta::reflectable_class<value_t> &&
                          (tuple_has_spec_v<attrs_t, meta::attrs::rename_all> ||
                           tuple_has_v<attrs_t, meta::attrs::deny_unknown_fields>)) {
            using base_config = config::config_of<Backend>;
            using struct_config_t =
                detail::annotated_struct_config_t<base_config, attrs_t>;
            return deserialize_struct<Backend, value_t, struct_config_t>(src, value);
        }
        // Default: just recurse on the underlying value
        else {
            return deserialize<Backend>(src, value);
        }
    }
    // 2. Bool
    else if constexpr(std::same_as<U, bool>) {
        return Backend::read_bool(src, out);
    }
    // 3. Signed integers
    else if constexpr(meta::int_like<U>) {
        if constexpr(std::same_as<U, std::int64_t>) {
            return Backend::read_int64(src, out);
        } else {
            std::int64_t v;
            auto err = Backend::read_int64(src, v);
            if(err != Backend::success)
                return err;
            if(!std::in_range<U>(v)) [[unlikely]]
                return Backend::number_out_of_range;
            out = static_cast<U>(v);
            return Backend::success;
        }
    }
    // 4. Unsigned integers
    else if constexpr(meta::uint_like<U>) {
        if constexpr(std::same_as<U, std::uint64_t>) {
            return Backend::read_uint64(src, out);
        } else {
            std::uint64_t v;
            auto err = Backend::read_uint64(src, v);
            if(err != Backend::success)
                return err;
            if(!std::in_range<U>(v)) [[unlikely]]
                return Backend::number_out_of_range;
            out = static_cast<U>(v);
            return Backend::success;
        }
    }
    // 5. Floating point
    else if constexpr(meta::floating_like<U>) {
        double d;
        auto err = Backend::read_double(src, d);
        if(err != Backend::success)
            return err;
        out = static_cast<U>(d);
        return Backend::success;
    }
    // 6. char
    else if constexpr(meta::char_like<U>) {
        std::string_view sv;
        auto err = Backend::read_string(src, sv);
        if(err != Backend::success)
            return err;
        if(sv.size() != 1) [[unlikely]]
            return Backend::type_mismatch;
        out = sv.front();
        return Backend::success;
    }
    // 7. String
    else if constexpr(std::same_as<U, std::string> || std::derived_from<U, std::string>) {
        std::string_view sv;
        auto err = Backend::read_string(src, sv);
        if(err != Backend::success)
            return err;
        static_cast<std::string&>(out).assign(sv.data(), sv.size());
        return Backend::success;
    }
    // 8. null types
    else if constexpr(meta::null_like<U>) {
        bool is_null = false;
        auto err = Backend::read_is_null(src, is_null);
        if(err != Backend::success)
            return err;
        if(!is_null)
            return Backend::type_mismatch;
        out = U{};
        return Backend::success;
    }
    // 9. Optional
    else if constexpr(is_specialization_of<std::optional, U>) {
        bool is_null = false;
        auto err = Backend::read_is_null(src, is_null);
        if(err != Backend::success)
            return err;
        if(is_null) {
            out.reset();
            return Backend::success;
        }
        out.emplace();
        return deserialize<Backend>(src, *out);
    }
    // 10. unique_ptr
    else if constexpr(is_specialization_of<std::unique_ptr, U>) {
        bool is_null = false;
        auto err = Backend::read_is_null(src, is_null);
        if(err != Backend::success)
            return err;
        if(is_null) {
            out.reset();
            return Backend::success;
        }
        using elem_t = typename U::element_type;
        out = std::make_unique<elem_t>();
        return deserialize<Backend>(src, *out);
    }
    // 11. shared_ptr
    else if constexpr(is_specialization_of<std::shared_ptr, U>) {
        bool is_null = false;
        auto err = Backend::read_is_null(src, is_null);
        if(err != Backend::success)
            return err;
        if(is_null) {
            out.reset();
            return Backend::success;
        }
        using elem_t = typename U::element_type;
        out = std::make_shared<elem_t>();
        return deserialize<Backend>(src, *out);
    }
    // 12. bytes (vector<byte>)
    else if constexpr(std::same_as<U, std::vector<std::byte>>) {
        out.clear();
        struct byte_visitor {
            std::vector<std::byte>& out;

            E visit_element(typename Backend::value_type& val) {
                std::uint64_t byte_val = 0;
                auto err = Backend::read_uint64(val, byte_val);
                if(err != Backend::success)
                    return err;
                if(byte_val > 255)
                    return Backend::number_out_of_range;
                out.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(byte_val)));
                return Backend::success;
            }
        };
        byte_visitor vis{out};
        return Backend::visit_array(src, vis);
    }
    // 13. Variant (untagged)
    else if constexpr(is_specialization_of<std::variant, U>) {
        return deserialize_variant_untagged<Backend, config::config_of<Backend>>(src, out);
    }
    // 14. Tuple (includes std::array, std::pair, std::tuple)
    else if constexpr(meta::tuple_like<U>) {
        tuple_visitor<Backend, U> vis{out};
        auto err = Backend::visit_array(src, vis);
        if(err != Backend::success)
            return err;
        return vis.finish();
    }
    // 15. Range types (sequence and map)
    else if constexpr(std::ranges::input_range<U>) {
        constexpr auto kind = format_kind<U>;
        if constexpr(kind == range_format::map) {
            out.clear();
            map_visitor<Backend, U> vis{out};
            return Backend::visit_object(src, vis);
        } else {
            if constexpr(requires { out.clear(); }) {
                out.clear();
            }
            seq_visitor<Backend, typename std::ranges::range_value_t<U>, U> vis{out};
            return Backend::visit_array(src, vis);
        }
    }
    // 16. Enum (always as integer, even if underlying type is char)
    else if constexpr(std::is_enum_v<U>) {
        using underlying_t = std::underlying_type_t<U>;
        if constexpr(std::is_signed_v<underlying_t>) {
            std::int64_t v;
            auto err = Backend::read_int64(src, v);
            if(err != Backend::success)
                return err;
            if(v < static_cast<std::int64_t>(std::numeric_limits<underlying_t>::min()) ||
               v > static_cast<std::int64_t>(std::numeric_limits<underlying_t>::max())) [[unlikely]]
                return Backend::number_out_of_range;
            out = static_cast<U>(static_cast<underlying_t>(v));
        } else {
            std::uint64_t v;
            auto err = Backend::read_uint64(src, v);
            if(err != Backend::success)
                return err;
            if(v > static_cast<std::uint64_t>(std::numeric_limits<underlying_t>::max())) [[unlikely]]
                return Backend::number_out_of_range;
            out = static_cast<U>(static_cast<underlying_t>(v));
        }
        return Backend::success;
    }
    // 17. Reflectable struct
    else if constexpr(meta::reflectable_class<U>) {
        return deserialize_struct<Backend, U, config::config_of<Backend>>(src, out);
    }
    else {
        // Type not handled by the new visitor-based deserialization.
        // Return an error rather than static_assert so that struct fields
        // with custom deserialize_traits (e.g. content::Value) gracefully
        // fail at runtime instead of preventing compilation.
        return Backend::type_mismatch;
    }
}

}  // namespace kota::codec
