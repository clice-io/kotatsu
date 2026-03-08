#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "eventide/reflection/struct.h"
#include "eventide/serde/serde/annotation.h"
#include "eventide/serde/serde/attrs.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/traits.h"
#include "eventide/serde/serde/utils/common.h"
#include "eventide/serde/serde/utils/field_dispatch.h"

namespace eventide::serde {

template <serializer_like S, typename V, typename T, typename E>
constexpr auto serialize(S& s, const V& v) -> std::expected<T, E>;

template <deserializer_like D, typename V, typename E>
constexpr auto deserialize(D& d, V& v) -> std::expected<void, E>;

namespace detail {

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_externally_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E> {
    constexpr auto names = resolve_tag_names<TagAttr, Ts...>();

    auto s_struct = s.serialize_struct("", 1);
    if(!s_struct) {
        return std::unexpected(s_struct.error());
    }

    auto name = names[value.index()];
    std::expected<void, E> field_result{};
    std::visit([&](const auto& item) { field_result = s_struct->serialize_field(name, item); },
               value);

    if(!field_result) {
        return std::unexpected(field_result.error());
    }
    return s_struct->end();
}

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_adjacently_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E> {
    constexpr auto names = resolve_tag_names<TagAttr, Ts...>();

    auto s_struct = s.serialize_struct("", 2);
    if(!s_struct) {
        return std::unexpected(s_struct.error());
    }

    auto name = names[value.index()];
    auto tag_result = s_struct->serialize_field(TagAttr::field_names[0], name);
    if(!tag_result) {
        return std::unexpected(tag_result.error());
    }

    std::expected<void, E> content_result{};
    std::visit(
        [&](const auto& item) {
            content_result = s_struct->serialize_field(TagAttr::field_names[1], item);
        },
        value);

    if(!content_result) {
        return std::unexpected(content_result.error());
    }
    return s_struct->end();
}

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_externally_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E> {
    constexpr auto names = resolve_tag_names<TagAttr, Ts...>();

    auto d_struct = d.deserialize_struct("", 1);
    if(!d_struct) {
        return std::unexpected(d_struct.error());
    }

    auto key = d_struct->next_key();
    if(!key) {
        return std::unexpected(key.error());
    }
    if(!key->has_value()) {
        return std::unexpected(E::type_mismatch);
    }

    std::string_view key_name = **key;
    bool matched = false;
    std::expected<void, E> status{};

    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (([&] {
             if(matched) {
                 return;
             }
             if(names[I] != key_name) {
                 return;
             }
             matched = true;

             using alt_t = std::variant_alternative_t<I, std::variant<Ts...>>;
             if constexpr(std::same_as<alt_t, std::monostate>) {
                 std::monostate alt{};
                 auto result = d_struct->deserialize_value(alt);
                 if(!result) {
                     status = std::unexpected(result.error());
                 } else {
                     value.template emplace<I>();
                 }
             } else if constexpr(std::default_initializable<alt_t>) {
                 alt_t alt{};
                 auto result = d_struct->deserialize_value(alt);
                 if(!result) {
                     status = std::unexpected(result.error());
                 } else {
                     value = std::move(alt);
                 }
             } else {
                 status = std::unexpected(E::invalid_state);
             }
         }()),
         ...);
    }(std::make_index_sequence<sizeof...(Ts)>{});

    if(!matched) {
        return std::unexpected(E::type_mismatch);
    }
    if(!status) {
        return std::unexpected(status.error());
    }
    return d_struct->end();
}

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_adjacently_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E> {
    constexpr auto names = resolve_tag_names<TagAttr, Ts...>();

    auto d_struct = d.deserialize_struct("", 2);
    if(!d_struct) {
        return std::unexpected(d_struct.error());
    }

    std::string tag_value;
    bool has_tag = false;
    bool has_content = false;

    auto deserialize_content_for_tag = [&](auto&& read_content_alt) -> std::expected<void, E> {
        bool matched = false;
        std::expected<void, E> status{};

        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (([&] {
                 if(matched || names[I] != tag_value) {
                     return;
                 }
                 matched = true;

                 using alt_t = std::variant_alternative_t<I, std::variant<Ts...>>;
                 if constexpr(std::same_as<alt_t, std::monostate>) {
                     std::monostate alt{};
                     auto result = read_content_alt(alt);
                     if(!result) {
                         status = std::unexpected(result.error());
                     } else {
                         value.template emplace<I>();
                     }
                 } else if constexpr(std::default_initializable<alt_t>) {
                     alt_t alt{};
                     auto result = read_content_alt(alt);
                     if(!result) {
                         status = std::unexpected(result.error());
                     } else {
                         value = std::move(alt);
                     }
                 } else {
                     status = std::unexpected(E::invalid_state);
                 }
             }()),
             ...);
        }(std::make_index_sequence<sizeof...(Ts)>{});

        if(!matched) {
            return std::unexpected(E::type_mismatch);
        }
        if(!status) {
            return std::unexpected(status.error());
        }
        return {};
    };

    if constexpr(detail::can_buffer_adjacently_tagged_v<D>) {
        using captured_t = detail::captured_dom_value_t<D>;
        std::optional<captured_t> buffered_content;

        while(true) {
            auto key = d_struct->next_key();
            if(!key) {
                return std::unexpected(key.error());
            }
            if(!key->has_value()) {
                break;
            }

            if(**key == TagAttr::field_names[0]) {
                if(has_tag) {
                    return std::unexpected(E::type_mismatch);
                }
                auto tag_status = d_struct->deserialize_value(tag_value);
                if(!tag_status) {
                    return std::unexpected(tag_status.error());
                }
                has_tag = true;
            } else if(**key == TagAttr::field_names[1]) {
                if(has_content) {
                    return std::unexpected(E::type_mismatch);
                }
                has_content = true;

                if(has_tag) {
                    auto content_status =
                        deserialize_content_for_tag([&](auto& alt) -> std::expected<void, E> {
                            auto result = d_struct->deserialize_value(alt);
                            if(!result) {
                                return std::unexpected(result.error());
                            }
                            return {};
                        });
                    if(!content_status) {
                        return std::unexpected(content_status.error());
                    }
                } else {
                    captured_t captured{};
                    auto capture_status = d_struct->deserialize_value(captured);
                    if(!capture_status) {
                        return std::unexpected(capture_status.error());
                    }
                    buffered_content.emplace(std::move(captured));
                }
            } else {
                auto skipped = d_struct->skip_value();
                if(!skipped) {
                    return std::unexpected(skipped.error());
                }
            }
        }

        if(!has_tag || !has_content) {
            return std::unexpected(E::type_mismatch);
        }

        if(buffered_content.has_value()) {
            auto buffered_status =
                deserialize_content_for_tag([&](auto& alt) -> std::expected<void, E> {
                    content::Deserializer<typename D::config_type> buffered_deserializer(
                        *buffered_content);
                    auto result = serde::deserialize(buffered_deserializer, alt);
                    if(!result) {
                        return std::unexpected(result.error());
                    }
                    auto finish_status = buffered_deserializer.finish();
                    if(!finish_status) {
                        return std::unexpected(finish_status.error());
                    }
                    return {};
                });
            if(!buffered_status) {
                return std::unexpected(buffered_status.error());
            }
        }

        return d_struct->end();
    } else {
        // Fallback for backends that cannot buffer an unresolved field payload.
        auto tag_key = d_struct->next_key();
        if(!tag_key) {
            return std::unexpected(tag_key.error());
        }
        if(!tag_key->has_value() || **tag_key != TagAttr::field_names[0]) {
            return std::unexpected(E::type_mismatch);
        }

        auto tag_status = d_struct->deserialize_value(tag_value);
        if(!tag_status) {
            return std::unexpected(tag_status.error());
        }
        has_tag = true;

        auto content_key = d_struct->next_key();
        if(!content_key) {
            return std::unexpected(content_key.error());
        }
        if(!content_key->has_value() || **content_key != TagAttr::field_names[1]) {
            return std::unexpected(E::type_mismatch);
        }

        auto content_status = deserialize_content_for_tag([&](auto& alt) -> std::expected<void, E> {
            auto result = d_struct->deserialize_value(alt);
            if(!result) {
                return std::unexpected(result.error());
            }
            return {};
        });
        if(!content_status) {
            return std::unexpected(content_status.error());
        }
        return d_struct->end();
    }
}

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_internally_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E> {
    constexpr auto names = resolve_tag_names<TagAttr, Ts...>();
    constexpr std::string_view tag_field = TagAttr::field_names[0];

    return std::visit(
        [&](const auto& item) -> std::expected<typename S::value_type, E> {
            using alt_t = std::remove_cvref_t<decltype(item)>;
            static_assert(refl::reflectable_class<alt_t>,
                          "internally_tagged requires struct alternatives");

            using config_t = config::config_of<S>;
            auto s_struct = s.serialize_struct("", refl::field_count<alt_t>() + 1);
            if(!s_struct) {
                return std::unexpected(s_struct.error());
            }

            // tag field first
            auto tag_name = names[value.index()];
            auto tag_status = s_struct->serialize_field(tag_field, tag_name);
            if(!tag_status) {
                return std::unexpected(tag_status.error());
            }

            // struct fields
            std::expected<void, E> field_result;
            refl::for_each(item, [&](auto field) {
                auto r = serialize_struct_field<config_t, E>(*s_struct, field);
                if(!r) {
                    field_result = std::unexpected(r.error());
                    return false;
                }
                return true;
            });
            if(!field_result) {
                return std::unexpected(field_result.error());
            }
            return s_struct->end();
        },
        value);
}

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_internally_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E> {
    using config_t = config::config_of<D>;

    // Requires capture_dom_value() — buffer to content DOM, then two-pass dispatch
    auto dom_result = d.capture_dom_value();
    if(!dom_result) {
        return std::unexpected(E(dom_result.error()));
    }

    constexpr auto names = resolve_tag_names<TagAttr, Ts...>();
    constexpr std::string_view tag_field = TagAttr::field_names[0];

    auto obj_ref = dom_result->as_ref();
    auto obj = obj_ref.get_object();
    if(!obj) {
        return std::unexpected(E::type_mismatch);
    }

    // Pass 1: find tag
    std::string_view tag_value;
    bool found = false;
    for(auto entry: *obj) {
        if(entry.key == tag_field) {
            auto s = entry.value.get_string();
            if(!s) {
                return std::unexpected(E::type_mismatch);
            }
            tag_value = *s;
            found = true;
            break;
        }
    }
    if(!found) {
        return std::unexpected(E::type_mismatch);
    }

    // Pass 2: match tag -> deserialize full object as that struct type
    // The tag field will be skipped during struct deserialization (no matching struct field)
    bool matched = false;
    std::expected<void, E> status{};

    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (([&] {
             if(matched) {
                 return;
             }
             if(names[I] != tag_value) {
                 return;
             }
             matched = true;

             using alt_t = std::variant_alternative_t<I, std::variant<Ts...>>;
             static_assert(refl::reflectable_class<alt_t>,
                           "internally_tagged requires struct alternatives");

             if constexpr(std::default_initializable<alt_t>) {
                 alt_t alt{};
                 content::Deserializer<config_t> deser(obj_ref);
                 auto r = serde::deserialize(deser, alt);
                 if(!r) {
                     status = std::unexpected(E(r.error()));
                     return;
                 }
                 auto f = deser.finish();
                 if(!f) {
                     status = std::unexpected(E(f.error()));
                     return;
                 }
                 value = std::move(alt);
             } else {
                 status = std::unexpected(E::invalid_state);
             }
         }()),
         ...);
    }(std::make_index_sequence<sizeof...(Ts)>{});

    if(!matched) {
        return std::unexpected(E::type_mismatch);
    }
    return status;
}

}  // namespace detail

/// Bitmask of data-model type categories.
/// Backends map their format-specific "kind" enums to these bits;
/// the shared `expected_type_hints<T>()` maps C++ types to them.
enum class type_hint : std::uint8_t {
    null_like = 1 << 0,
    boolean = 1 << 1,
    integer = 1 << 2,
    floating = 1 << 3,
    string = 1 << 4,
    array = 1 << 5,
    object = 1 << 6,
    any = 0x7F,
};

constexpr type_hint operator|(type_hint a, type_hint b) noexcept {
    return static_cast<type_hint>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

constexpr bool has_any(type_hint set, type_hint flags) noexcept {
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(flags)) != 0;
}

/// Map a C++ type `T` to the set of data-model categories it can deserialize from.
template <typename T>
constexpr type_hint expected_type_hints() {
    using U = std::remove_cvref_t<T>;

    if constexpr(annotated_type<U>) {
        return expected_type_hints<typename U::annotated_type>();
    } else if constexpr(is_specialization_of<std::optional, U>) {
        return type_hint::null_like | expected_type_hints<typename U::value_type>();
    } else if constexpr(std::same_as<U, std::nullptr_t>) {
        return type_hint::null_like;
    } else if constexpr(std::same_as<U, std::monostate>) {
        return type_hint::null_like;
    } else if constexpr(bool_like<U>) {
        return type_hint::boolean;
    } else if constexpr(int_like<U> || uint_like<U>) {
        return type_hint::integer;
    } else if constexpr(floating_like<U>) {
        // floats accept both integer and floating sources
        return type_hint::integer | type_hint::floating;
    } else if constexpr(char_like<U> || std::same_as<U, std::string> ||
                        std::derived_from<U, std::string>) {
        return type_hint::string;
    } else if constexpr(std::same_as<U, std::vector<std::byte>>) {
        return type_hint::array;
    } else if constexpr(is_pair_v<U> || is_tuple_v<U>) {
        return type_hint::array;
    } else if constexpr(std::ranges::input_range<U>) {
        constexpr auto kind = format_kind<U>;
        if constexpr(kind == range_format::map) {
            return type_hint::object;
        } else if constexpr(kind == range_format::sequence || kind == range_format::set) {
            return type_hint::array;
        } else {
            return type_hint::any;
        }
    } else if constexpr(refl::reflectable_class<U>) {
        return type_hint::object;
    } else {
        return type_hint::any;
    }
}

/// Shared implementation of the probe-deserialize-finish pattern for variant candidates.
/// `D` must be constructible from `Source`.
template <typename D, typename Alt, typename Source, typename... Ts>
auto try_deserialize_variant_candidate(Source&& source, std::variant<Ts...>& value)
    -> std::expected<void, typename D::error_type> {
    Alt candidate{};
    D probe(std::forward<Source>(source));
    if(!probe.valid()) {
        return std::unexpected(probe.error());
    }

    auto status = serde::deserialize(probe, candidate);
    if(!status) {
        return std::unexpected(status.error());
    }

    auto finished = probe.finish();
    if(!finished) {
        return std::unexpected(finished.error());
    }

    value = std::move(candidate);
    return {};
}

/// Shared variant dispatch for DOM-like deserializers.
///
/// Given a source value and a type_hint describing its data-model category,
/// iterate all variant alternatives, check if the hint matches, and attempt
/// deserialization via the probe-deserialize-finish pattern.
///
/// D: the Deserializer type (must be constructible from Source)
/// Source: the captured value (e.g., json::ValueRef, const toml::node*)
/// hint: the serde::type_hint for the current value
/// mismatch_error: the error value to use when no alternative matches
template <typename D, typename Source, typename... Ts>
auto try_variant_dispatch(Source&& source,
                          type_hint hint,
                          std::variant<Ts...>& value,
                          typename D::error_type mismatch_error)
    -> std::expected<void, typename D::error_type> {
    static_assert((std::default_initializable<Ts> && ...),
                  "variant deserialization requires default-constructible alternatives");

    using error_type = typename D::error_type;

    bool matched = false;
    bool considered = false;
    error_type last_error = mismatch_error;

    auto try_alternative = [&](auto type_tag) {
        if(matched) {
            return;
        }

        using alt_t = typename decltype(type_tag)::type;
        if(!has_any(expected_type_hints<alt_t>(), hint)) {
            return;
        }

        considered = true;
        auto status =
            try_deserialize_variant_candidate<D, alt_t>(std::forward<Source>(source), value);
        if(status) {
            matched = true;
        } else {
            last_error = status.error();
        }
    };

    (try_alternative(std::type_identity<Ts>{}), ...);

    if(!matched) {
        return std::unexpected(considered ? last_error : mismatch_error);
    }
    return {};
}

}  // namespace eventide::serde
