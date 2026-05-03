#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/type_list.h"
#include "kota/support/type_traits.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/support/string_match.h"
#include "kota/codec/detail/config.h"
#include "kota/codec/detail/spelling.h"
#include "kota/codec/detail/struct_deserialize.h"

namespace kota::codec {

// Forward declarations
template <typename Backend, typename T>
auto deserialize(typename Backend::value_type& src, T& out) -> typename Backend::error_type;

template <typename Backend, typename RawT, typename Attrs>
auto deserialize_field(typename Backend::value_type& val, RawT& field_ref)
    -> typename Backend::error_type;

// Forward declarations for tagged variant support
template <typename Backend, typename Attrs, typename... Ts>
auto deserialize_externally_tagged(typename Backend::value_type& src, std::variant<Ts...>& out)
    -> typename Backend::error_type;

template <typename Backend, typename Attrs, typename... Ts>
auto deserialize_internally_tagged(typename Backend::value_type& src, std::variant<Ts...>& out)
    -> typename Backend::error_type;

template <typename Backend, typename Attrs, typename... Ts>
auto deserialize_adjacently_tagged(typename Backend::value_type& src, std::variant<Ts...>& out)
    -> typename Backend::error_type;

/// struct_visitor: receives field key/value from backend, dispatches to the correct slot
template <typename Backend, typename T, typename Config = meta::default_config>
struct struct_visitor {
    using E = typename Backend::error_type;
    using schema = meta::virtual_schema<T, Config>;
    using table = detail::field_name_table<T, Config>;
    using slots = typename schema::slots;

    T& out;
    std::uint64_t seen_fields = 0;

    E visit_field(std::string_view key, typename Backend::value_type& val) {
        using namespace kota;

        if constexpr(table::name_count == 0) {
            // No fields to match
            if constexpr(schema::deny_unknown || detail::config_deny_unknown_v<Config>) {
                if constexpr(requires { Backend::report_unknown_field(key); }) {
                    Backend::report_unknown_field(key);
                }
                return Backend::type_mismatch;
            }
            return Backend::success;
        } else {
            auto idx = string_match<table::names>(key);
            if(idx) {
                std::size_t slot_idx = table::slot_map[*idx];
                auto err = dispatch_slot(slot_idx, val);
                if(err != Backend::success) [[unlikely]] {
                    if constexpr(requires { Backend::report_prepend_field(key); }) {
                        Backend::report_prepend_field(schema::fields[slot_idx].name);
                    }
                }
                seen_fields |= (std::uint64_t(1) << slot_idx);
                return err;
            }

            if constexpr(schema::deny_unknown || detail::config_deny_unknown_v<Config>) {
                if constexpr(requires { Backend::report_unknown_field(key); }) {
                    Backend::report_unknown_field(key);
                }
                return Backend::type_mismatch;
            }
            return Backend::success;
        }
    }

    E finish() {
        constexpr std::uint64_t required = detail::schema_required_field_mask<T, Config>();
        if constexpr(required != 0) {
            if((seen_fields & required) != required) [[unlikely]] {
                std::uint64_t missing = required & ~seen_fields;
                if constexpr(requires { Backend::report_missing_field(std::string_view{}); }) {
                    for(std::size_t i = 0; i < schema::count; ++i) {
                        if(missing & (std::uint64_t(1) << i)) {
                            Backend::report_missing_field(schema::fields[i].name);
                            break;
                        }
                    }
                }
                return Backend::type_mismatch;
            }
        }
        return Backend::success;
    }

private:
    E dispatch_slot(std::size_t slot_index, typename Backend::value_type& val) {
        constexpr std::size_t N = type_list_size_v<slots>;
        return dispatch_slot_impl(slot_index, val, std::make_index_sequence<N>{});
    }

    template <std::size_t... Is>
    E dispatch_slot_impl(std::size_t slot_index,
                         typename Backend::value_type& val,
                         std::index_sequence<Is...>) {
        E err = Backend::type_mismatch;
        (void)((Is == slot_index ? (err = dispatch_slot_at<Is>(val), true) : false) || ...);
        return err;
    }

    template <std::size_t I>
    E dispatch_slot_at(typename Backend::value_type& val) {
        using slot_t = type_list_element_t<I, slots>;
        using raw_t = std::remove_cv_t<typename slot_t::raw_type>;
        using attrs_t = typename slot_t::attrs;

        constexpr std::size_t offset = schema::fields[I].offset;
        auto* base = reinterpret_cast<std::byte*>(std::addressof(out));
        auto& field_ref = *reinterpret_cast<raw_t*>(base + offset);

        return deserialize_field<Backend, raw_t, attrs_t>(val, field_ref);
    }
};

/// deserialize_field: handles all field-level attribute dispatch
template <typename Backend, typename RawT, typename Attrs>
auto deserialize_field(typename Backend::value_type& val, RawT& field_ref)
    -> typename Backend::error_type {
    // 1. skip_if
    if constexpr(tuple_has_spec_v<Attrs, meta::behavior::skip_if>) {
        using pred = typename tuple_find_spec_t<Attrs, meta::behavior::skip_if>::predicate;
        if(meta::evaluate_skip_predicate<pred>(field_ref, false)) {
            return Backend::success;
        }
    }

    // 2. behavior::with<Adapter>
    if constexpr(tuple_has_spec_v<Attrs, meta::behavior::with>) {
        using Adapter =
            typename tuple_find_spec_t<Attrs, meta::behavior::with>::adapter;
        if constexpr(requires {
                         Adapter::from_wire(std::declval<typename Adapter::wire_type>());
                     }) {
            using wire_t = typename Adapter::wire_type;
            wire_t wire{};
            auto err = deserialize<Backend>(val, wire);
            if(err != Backend::success)
                return err;
            field_ref = Adapter::from_wire(std::move(wire));
            return Backend::success;
        } else if constexpr(requires(typename Backend::value_type & v, RawT & r) {
                                Adapter::template deserialize<Backend>(v, r);
                            }) {
            return Adapter::template deserialize<Backend>(val, field_ref);
        } else {
            return deserialize<Backend>(val, field_ref);
        }
    }
    // 3. behavior::as<Target>
    else if constexpr(tuple_has_spec_v<Attrs, meta::behavior::as>) {
        using Target = typename tuple_find_spec_t<Attrs, meta::behavior::as>::target;
        Target temp{};
        auto err = deserialize<Backend>(val, temp);
        if(err != Backend::success)
            return err;
        field_ref = RawT(std::move(temp));
        return Backend::success;
    }
    // 4. behavior::enum_string<Policy>
    else if constexpr(tuple_has_spec_v<Attrs, meta::behavior::enum_string>) {
        using Policy =
            typename tuple_find_spec_t<Attrs, meta::behavior::enum_string>::policy;
        std::string_view text;
        auto err = Backend::read_string(val, text);
        if(err != Backend::success)
            return err;
        auto mapped = spelling::map_string_to_enum<RawT, Policy>(text);
        if(!mapped) {
            if constexpr(requires { Backend::report_unknown_enum(text); }) {
                Backend::report_unknown_enum(text);
            }
            return Backend::type_mismatch;
        }
        field_ref = *mapped;
        return Backend::success;
    }
    // 5. tagged variant
    else if constexpr(is_specialization_of<std::variant, RawT> &&
                      tuple_any_of_v<Attrs, meta::is_tagged_attr>) {
        using tag_attr = tuple_find_t<Attrs, meta::is_tagged_attr>;
        constexpr auto strategy = meta::tagged_strategy_of<tag_attr>;
        if constexpr(strategy == meta::tagged_strategy::external) {
            return deserialize_externally_tagged<Backend, tag_attr>(val, field_ref);
        } else if constexpr(strategy == meta::tagged_strategy::internal) {
            return deserialize_internally_tagged<Backend, tag_attr>(val, field_ref);
        } else {
            return deserialize_adjacently_tagged<Backend, tag_attr>(val, field_ref);
        }
    }
    // 6. Default: recursive deserialize
    else {
        return deserialize<Backend>(val, field_ref);
    }
}

/// positional_struct_visitor: reads fields in schema order for positional backends (e.g. bincode)
template <typename Backend, typename T, typename Config = meta::default_config>
struct positional_struct_visitor {
    using E = typename Backend::error_type;
    using schema = meta::virtual_schema<T, Config>;
    using slots = typename schema::slots;
    static constexpr std::size_t N = type_list_size_v<slots>;

    T& out;

    E visit_all_positional(typename Backend::value_type& val) {
        return visit_impl(val, std::make_index_sequence<N>{});
    }

private:
    template <std::size_t... Is>
    E visit_impl(typename Backend::value_type& val, std::index_sequence<Is...>) {
        E err = Backend::success;
        (void)((err = visit_at<Is>(val), err == Backend::success) && ...);
        return err;
    }

    template <std::size_t I>
    E visit_at(typename Backend::value_type& val) {
        using slot_t = type_list_element_t<I, slots>;
        using raw_t = std::remove_cv_t<typename slot_t::raw_type>;
        using attrs_t = typename slot_t::attrs;

        constexpr std::size_t offset = schema::fields[I].offset;
        auto* base = reinterpret_cast<std::byte*>(std::addressof(out));
        auto& field_ref = *reinterpret_cast<raw_t*>(base + offset);

        // Handle skip_if: for positional formats, we still need to read and discard
        if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::skip_if>) {
            using pred = typename tuple_find_spec_t<attrs_t, meta::behavior::skip_if>::predicate;
            if(meta::evaluate_skip_predicate<pred>(field_ref, false)) {
                // Discard by reading into a temporary
                raw_t discard{};
                return deserialize<Backend>(val, discard);
            }
        }

        return deserialize_field<Backend, raw_t, attrs_t>(val, field_ref);
    }
};

/// Concept: backend supports positional struct layout
template <typename Backend>
concept positional_backend = requires {
    { Backend::positional } -> std::convertible_to<bool>;
    requires Backend::positional;
};

/// deserialize_struct: entry point for struct deserialization
template <typename Backend, typename T, typename Config = meta::default_config>
auto deserialize_struct(typename Backend::value_type& src, T& out)
    -> typename Backend::error_type {
    if constexpr(positional_backend<Backend>) {
        // Positional backend: read fields in schema order
        positional_struct_visitor<Backend, T, Config> vis{out};
        return Backend::visit_struct_positional(src, vis);
    } else {
        // Detect ambiguous wire names (e.g. alias conflicts) at compile time.
        if constexpr(detail::schema_has_ambiguous_wire_names<T, Config>()) {
            if constexpr(requires { Backend::invalid_state; }) {
                return Backend::invalid_state;
            } else {
                return Backend::type_mismatch;
            }
        } else {
            struct_visitor<Backend, T, Config> vis{out};
            auto err = Backend::visit_object(src, vis);
            if(err != Backend::success)
                return err;
            return vis.finish();
        }
    }
}

}  // namespace kota::codec
