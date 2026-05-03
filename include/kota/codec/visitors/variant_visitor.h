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

namespace detail {

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

}  // namespace detail

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
            err = detail::match_and_deserialize_alt<Backend>(
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
/// Uses visit_object_keys to find the tag (which resets the iterator),
/// then re-parses the raw JSON to deserialize the matched alternative.
/// The re-parse is necessary because simdjson's ondemand string buffer is
/// append-only — the first scan pass consumes buffer space for unescaped
/// keys/values, and re-iterating the same document for deserialization would
/// overflow that buffer.
template <typename Backend, typename TagAttr, typename... Ts>
auto deserialize_internally_tagged(typename Backend::value_type& src, std::variant<Ts...>& out)
    -> typename Backend::error_type {
    using E = typename Backend::error_type;
    using value_type = typename Backend::value_type;
    constexpr auto names = meta::resolve_tag_names<TagAttr, Ts...>();

struct tag_scanner {
        std::string& tag_value;
        bool found = false;

        E on_field(std::string_view key, meta::type_kind, value_type& val) {
            if(key == TagAttr::field_names[0]) {
                std::string_view sv;
                auto err = Backend::read_string(val, sv);
                if(err != Backend::success)
                    return err;
                tag_value.assign(sv.data(), sv.size());
                found = true;
            }
            return Backend::success;
        }
    };

    std::string tag_value_str;
    tag_scanner scanner{tag_value_str};
    auto err = Backend::visit_object_keys(src, scanner);
    if(err != Backend::success)
        return err;
    if(!scanner.found)
        return Backend::type_mismatch;

    return detail::match_and_deserialize_alt<Backend>(
        tag_value_str,
        names,
        out,
        [&](auto& alt) -> E {
            using alt_t = std::remove_cvref_t<decltype(alt)>;
            static_assert(meta::reflectable_class<alt_t>,
                          "internally_tagged requires struct alternatives");
            return deserialize<Backend>(src, alt);
        });
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
                        err = detail::match_and_deserialize_alt<Backend>(
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
                        err = detail::match_and_deserialize_alt<Backend>(
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
                    err = detail::match_and_deserialize_alt<Backend>(
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
