#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "kota/support/type_list.h"
#include "kota/support/type_traits.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/detail/config.h"

namespace kota::codec::detail {

template <typename T, typename Config>
consteval bool schema_has_ambiguous_wire_names() {
    using schema = meta::virtual_schema<T, Config>;
    for(std::size_t i = 0; i < schema::count; ++i) {
        for(std::size_t j = i + 1; j < schema::count; ++j) {
            // name vs name
            if(schema::fields[i].name == schema::fields[j].name) {
                return true;
            }
            // name vs aliases
            for(auto alias: schema::fields[j].aliases) {
                if(schema::fields[i].name == alias) {
                    return true;
                }
            }
            for(auto alias: schema::fields[i].aliases) {
                if(alias == schema::fields[j].name) {
                    return true;
                }
            }
            // aliases vs aliases
            for(auto a: schema::fields[i].aliases) {
                for(auto b: schema::fields[j].aliases) {
                    if(a == b) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

template <typename T, typename Config>
struct field_name_table {
    using schema = meta::virtual_schema<T, Config>;

    static consteval std::size_t compute_name_count() {
        std::size_t count = 0;
        for(std::size_t i = 0; i < schema::count; ++i) {
            count += 1 + schema::fields[i].aliases.size();
        }
        return count;
    }

    constexpr static std::size_t name_count = compute_name_count();

    constexpr static auto names = [] {
        std::array<std::string_view, name_count> result{};
        std::size_t out = 0;
        for(std::size_t i = 0; i < schema::count; ++i) {
            result[out++] = schema::fields[i].name;
            for(auto alias: schema::fields[i].aliases) {
                result[out++] = alias;
            }
        }
        return result;
    }();

    constexpr static auto slot_map = [] {
        std::array<std::size_t, name_count> result{};
        std::size_t out = 0;
        for(std::size_t i = 0; i < schema::count; ++i) {
            result[out++] = i;
            for(std::size_t j = 0; j < schema::fields[i].aliases.size(); ++j) {
                result[out++] = i;
            }
        }
        return result;
    }();
};

template <typename Slots, std::size_t I>
consteval std::uint64_t required_field_bit() {
    using slot_t = type_list_element_t<I, Slots>;
    using raw_t = std::remove_cv_t<typename slot_t::raw_type>;
    using attrs_t = typename slot_t::attrs;
    if constexpr(!tuple_has_v<attrs_t, meta::attrs::default_value> &&
                 !is_specialization_of<std::optional, raw_t>) {
        return std::uint64_t(1) << I;
    } else {
        return 0;
    }
}

template <typename Slots, std::size_t... Is>
consteval std::uint64_t required_field_mask_impl(std::index_sequence<Is...>) {
    if constexpr(sizeof...(Is) == 0) {
        return 0;
    } else {
        return (required_field_bit<Slots, Is>() | ...);
    }
}

template <typename T, typename Config>
consteval std::uint64_t schema_required_field_mask() {
    using schema = meta::virtual_schema<T, Config>;
    using slots = typename schema::slots;
    constexpr std::size_t N = type_list_size_v<slots>;
    static_assert(N <= 64, "schema_required_field_mask: >64 slots not supported");
    return required_field_mask_impl<slots>(std::make_index_sequence<N>{});
}

template <typename Config>
constexpr bool config_deny_unknown_v = requires { requires Config::deny_unknown_fields; };

}  // namespace kota::codec::detail
