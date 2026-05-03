#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/type_list.h"
#include "kota/support/type_traits.h"
#include "kota/meta/attrs.h"
#include "kota/meta/struct.h"

namespace kota::codec {

template <typename Backend, typename T>
auto deserialize(typename Backend::value_type& src, T& out) -> typename Backend::error_type;

namespace detail_v2 {

/// Match a tag string against variant alternatives and deserialize
template <typename Backend, typename... Ts, typename Names, typename Reader>
auto match_and_deserialize_alt(std::string_view tag_value,
                               const Names& names,
                               std::variant<Ts...>& out,
                               Reader&& reader) -> typename Backend::error_type {
    using E = typename Backend::error_type;
    bool matched = false;
    E err = Backend::type_mismatch;

    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (([&] {
             if(matched || names[I] != tag_value)
                 return;
             matched = true;

             using alt_t = std::variant_alternative_t<I, std::variant<Ts...>>;
             if constexpr(std::same_as<alt_t, std::monostate>) {
                 std::monostate alt{};
                 err = reader(alt);
                 if(err == Backend::success) {
                     out.template emplace<I>();
                 }
             } else if constexpr(std::default_initializable<alt_t>) {
                 alt_t alt{};
                 err = reader(alt);
                 if(err == Backend::success) {
                     out = std::move(alt);
                 }
             } else {
                 err = Backend::type_mismatch;
             }
         }()),
         ...);
    }(std::make_index_sequence<sizeof...(Ts)>{});

    if(!matched)
        return Backend::type_mismatch;
    return err;
}

}  // namespace detail_v2

/// Externally tagged: {"Circle": {"radius": 5.0}}
template <typename Backend, typename TagAttr, typename... Ts>
auto deserialize_externally_tagged(typename Backend::value_type& src, std::variant<Ts...>& out)
    -> typename Backend::error_type {
    using E = typename Backend::error_type;
    constexpr auto names = meta::resolve_tag_names<TagAttr, Ts...>();

    struct external_visitor {
        std::variant<Ts...>& out;
        const decltype(names)& tag_names;
        E err = Backend::success;
        bool found = false;

        E visit_field(std::string_view key, typename Backend::value_type& val) {
            if(found)
                return Backend::type_mismatch;
            found = true;
            err = detail_v2::match_and_deserialize_alt<Backend>(
                key,
                tag_names,
                out,
                [&](auto& alt) { return deserialize<Backend>(val, alt); });
            return err;
        }
    };

    external_visitor vis{out, names};
    auto err = Backend::visit_object(src, vis);
    if(err != Backend::success)
        return err;
    if(!vis.found)
        return Backend::type_mismatch;
    return vis.err;
}

/// Internally tagged: {"type": "Circle", "radius": 5.0}
template <typename Backend, typename TagAttr, typename... Ts>
auto deserialize_internally_tagged(typename Backend::value_type& src, std::variant<Ts...>& out)
    -> typename Backend::error_type {
    constexpr auto names = meta::resolve_tag_names<TagAttr, Ts...>();
    constexpr std::string_view tag_field = TagAttr::field_names[0];

    // For backends that support capture_raw_json + with_reparsed (e.g. simdjson),
    // we must capture the raw JSON first, then scan the tag from one parse and
    // deserialize from a fresh parse. This avoids issues with forward-only parsers
    // where scan_field consumes the object state.
    constexpr bool has_reparse = requires(typename Backend::value_type& v) {
        { Backend::capture_raw_json(v) } -> std::same_as<std::pair<std::string, typename Backend::error_type>>;
        {
            Backend::with_reparsed(
                std::string_view{},
                [](typename Backend::value_type&) -> typename Backend::error_type {
                    return Backend::success;
                })
        } -> std::same_as<typename Backend::error_type>;
    };

    if constexpr(has_reparse) {
        // Capture raw JSON before any parsing consumes the value.
        auto [raw_json, cap_err] = Backend::capture_raw_json(src);
        if(cap_err != Backend::success)
            return cap_err;

        // Scan the tag field from one parse.
        std::string tag_value_str;
        auto scan_err = Backend::with_reparsed(raw_json, [&](typename Backend::value_type& val) {
            std::string_view sv;
            auto err = Backend::scan_field(val, tag_field, sv);
            if(err != Backend::success)
                return err;
            tag_value_str.assign(sv.data(), sv.size());
            return Backend::success;
        });
        if(scan_err != Backend::success)
            return scan_err;

        // Deserialize from a fresh parse.
        return detail_v2::match_and_deserialize_alt<Backend>(
            tag_value_str,
            names,
            out,
            [&](auto& alt) -> typename Backend::error_type {
                using alt_t = std::remove_cvref_t<decltype(alt)>;
                static_assert(meta::reflectable_class<alt_t>,
                              "internally_tagged requires struct alternatives");
                return Backend::with_reparsed(
                    raw_json,
                    [&](typename Backend::value_type& val) -> typename Backend::error_type {
                        return deserialize<Backend>(val, alt);
                    });
            });
    } else {
        std::string_view tag_value;
        auto err = Backend::scan_field(src, tag_field, tag_value);
        if(err != Backend::success)
            return err;

        return detail_v2::match_and_deserialize_alt<Backend>(
            tag_value,
            names,
            out,
            [&](auto& alt) -> typename Backend::error_type {
                using alt_t = std::remove_cvref_t<decltype(alt)>;
                static_assert(meta::reflectable_class<alt_t>,
                              "internally_tagged requires struct alternatives");
                return deserialize<Backend>(src, alt);
            });
    }
}

/// Adjacently tagged: {"type": "Circle", "data": {"radius": 5.0}}
template <typename Backend, typename TagAttr, typename... Ts>
auto deserialize_adjacently_tagged(typename Backend::value_type& src, std::variant<Ts...>& out)
    -> typename Backend::error_type {
    using E = typename Backend::error_type;
    using value_type = typename Backend::value_type;
    constexpr auto names = meta::resolve_tag_names<TagAttr, Ts...>();

    // Check if backend supports capture/reparse (needed for forward-only parsers
    // when content appears before tag).
    constexpr bool has_reparse = requires(value_type& v) {
        { Backend::capture_raw_json(v) } -> std::same_as<std::pair<std::string, E>>;
        {
            Backend::with_reparsed(
                std::string_view{},
                [](value_type&) -> E { return Backend::success; })
        } -> std::same_as<E>;
    };

    struct adjacent_visitor {
        std::variant<Ts...>& out;
        const decltype(names)& tag_names;
        E err = Backend::success;

        std::string tag_value;
        bool has_tag = false;
        bool has_content = false;

        // For backends without reparse, store the value directly.
        // For reparse-capable backends, store captured raw JSON instead.
        std::optional<value_type> deferred_content;
        std::string deferred_raw_json;

        E visit_field(std::string_view key, value_type& val) {
            if(key == TagAttr::field_names[0]) {
                if(has_tag)
                    return Backend::type_mismatch;
                std::string_view sv;
                auto e = Backend::read_string(val, sv);
                if(e != Backend::success)
                    return e;
                tag_value = std::string(sv);
                has_tag = true;

                if(has_content) {
                    if constexpr(has_reparse) {
                        // Reparse the captured raw JSON for deserialization
                        err = detail_v2::match_and_deserialize_alt<Backend>(
                            tag_value,
                            tag_names,
                            out,
                            [&](auto& alt) {
                                return Backend::with_reparsed(
                                    deferred_raw_json,
                                    [&](value_type& fresh_val) -> E {
                                        return deserialize<Backend>(fresh_val, alt);
                                    });
                            });
                    } else if(deferred_content) {
                        err = detail_v2::match_and_deserialize_alt<Backend>(
                            tag_value,
                            tag_names,
                            out,
                            [&](auto& alt) {
                                return deserialize<Backend>(*deferred_content, alt);
                            });
                    }
                    return err;
                }
            } else if(key == TagAttr::field_names[1]) {
                if(has_content)
                    return Backend::type_mismatch;
                has_content = true;

                if(has_tag) {
                    err = detail_v2::match_and_deserialize_alt<Backend>(
                        tag_value,
                        tag_names,
                        out,
                        [&](auto& alt) { return deserialize<Backend>(val, alt); });
                    return err;
                } else {
                    // Content before tag: need to defer
                    if constexpr(has_reparse) {
                        // Capture raw JSON so we can reparse later
                        auto [raw, cap_err] = Backend::capture_raw_json(val);
                        if(cap_err != Backend::success)
                            return cap_err;
                        deferred_raw_json = std::move(raw);
                    } else {
                        deferred_content.emplace(val);
                    }
                }
            }
            return Backend::success;
        }
    };

    adjacent_visitor vis{out, names, Backend::success, {}, false, false, {}, {}};
    auto err = Backend::visit_object(src, vis);
    if(err != Backend::success)
        return err;
    if(!vis.has_tag || !vis.has_content)
        return Backend::type_mismatch;
    return vis.err;
}

}  // namespace kota::codec
