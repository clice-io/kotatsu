#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "eventide/common/expected_try.h"
#include "eventide/reflection/annotation.h"
#include "eventide/reflection/attrs.h"
#include "eventide/reflection/struct.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/serde/utils/common.h"
#include "eventide/serde/serde/utils/field_dispatch.h"
#include "eventide/serde/serde/utils/fwd.h"

namespace eventide::serde::detail {

/// Match tag_value against variant alternative names, construct the matching alternative,
/// call reader(alt) to deserialize it, then assign to the variant.
template <typename E, typename... Ts, typename Names, typename Reader>
constexpr auto match_and_deserialize_alt(std::string_view tag_value,
                                         const Names& names,
                                         std::variant<Ts...>& value,
                                         Reader&& reader) -> std::expected<void, E> {
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
                 auto result = reader(alt);
                 if(!result) {
                     status = std::unexpected(result.error());
                 } else {
                     value.template emplace<I>();
                 }
             } else if constexpr(std::default_initializable<alt_t>) {
                 alt_t alt{};
                 auto result = reader(alt);
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
        return std::unexpected(E::custom(std::format("unknown variant tag '{}'", tag_value)));
    }
    return status;
}

/// Visit variant and call emitter with the active alternative's value.
/// Propagates the emitter's result through expected.
template <typename E, typename R, typename... Ts, typename Emitter>
constexpr auto visit_variant_alt(const std::variant<Ts...>& value, Emitter&& emit)
    -> std::expected<R, E> {
    std::expected<R, E> result{std::unexpected(E::invalid_state)};
    std::visit([&](const auto& item) { result = emit(item); }, value);
    return result;
}

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_externally_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E> {
    constexpr auto names = refl::resolve_tag_names<TagAttr, Ts...>();

    ETD_EXPECTED_TRY_V(auto s_struct, s.serialize_struct("", 1));

    auto name = names[value.index()];
    ETD_EXPECTED_TRY(
        (visit_variant_alt<E, void>(value, [&](const auto& item) -> std::expected<void, E> {
            return s_struct.serialize_field(name, item);
        })));

    return s_struct.end();
}

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_adjacently_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E> {
    constexpr auto names = refl::resolve_tag_names<TagAttr, Ts...>();

    ETD_EXPECTED_TRY_V(auto s_struct, s.serialize_struct("", 2));

    auto name = names[value.index()];
    ETD_EXPECTED_TRY(s_struct.serialize_field(TagAttr::field_names[0], name));

    ETD_EXPECTED_TRY(
        (visit_variant_alt<E, void>(value, [&](const auto& item) -> std::expected<void, E> {
            return s_struct.serialize_field(TagAttr::field_names[1], item);
        })));

    return s_struct.end();
}

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_externally_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E> {
    constexpr auto names = refl::resolve_tag_names<TagAttr, Ts...>();

    ETD_EXPECTED_TRY_V(auto d_struct, d.deserialize_struct("", 1));

    ETD_EXPECTED_TRY_V(auto key, d_struct.next_key());
    if(!key.has_value()) {
        return std::unexpected(E::custom("expected externally tagged variant key"));
    }

    ETD_EXPECTED_TRY((match_and_deserialize_alt<E>(*key, names, value, [&](auto& alt) {
        return d_struct.deserialize_value(alt);
    })));

    return d_struct.end();
}

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_adjacently_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E> {
    constexpr auto names = refl::resolve_tag_names<TagAttr, Ts...>();

    ETD_EXPECTED_TRY_V(auto d_struct, d.deserialize_struct("", 2));

    std::string tag_value;

    auto deserialize_content_for_tag = [&](auto&& read_content_alt) -> std::expected<void, E> {
        return match_and_deserialize_alt<E>(
            tag_value,
            names,
            value,
            std::forward<decltype(read_content_alt)>(read_content_alt));
    };

    // Read content directly from the struct deserializer
    auto read_content_direct = [&](auto& alt) -> std::expected<void, E> {
        ETD_EXPECTED_TRY(d_struct.deserialize_value(alt));
        return {};
    };

    // Expect the next key to match a specific field name
    auto expect_next_key = [&](std::string_view expected) -> std::expected<void, E> {
        ETD_EXPECTED_TRY_V(auto key, d_struct.next_key());
        if(!key.has_value() || *key != expected) {
            return std::unexpected(
                E::custom(std::format("expected adjacent tag field '{}'", expected)));
        }
        return {};
    };

    if constexpr(detail::can_buffer_adjacently_tagged_v<D>) {
        using captured_t = detail::captured_dom_value_t<D>;
        std::optional<captured_t> buffered_content;
        bool has_tag = false;
        bool has_content = false;

        while(true) {
            ETD_EXPECTED_TRY_V(auto key, d_struct.next_key());
            if(!key.has_value()) {
                break;
            }

            if(*key == TagAttr::field_names[0]) {
                if(has_tag) {
                    return std::unexpected(E::duplicate_field(TagAttr::field_names[0]));
                }
                ETD_EXPECTED_TRY(d_struct.deserialize_value(tag_value));
                has_tag = true;
            } else if(*key == TagAttr::field_names[1]) {
                if(has_content) {
                    return std::unexpected(E::duplicate_field(TagAttr::field_names[1]));
                }
                has_content = true;

                if(has_tag) {
                    ETD_EXPECTED_TRY(deserialize_content_for_tag(read_content_direct));
                } else {
                    captured_t captured{};
                    ETD_EXPECTED_TRY(d_struct.deserialize_value(captured));
                    buffered_content.emplace(std::move(captured));
                }
            } else {
                ETD_EXPECTED_TRY(d_struct.skip_value());
            }
        }

        if(!has_tag || !has_content) {
            if(!has_tag) {
                return std::unexpected(E::missing_field(TagAttr::field_names[0]));
            }
            return std::unexpected(E::missing_field(TagAttr::field_names[1]));
        }

        if(buffered_content.has_value()) {
            ETD_EXPECTED_TRY(deserialize_content_for_tag([&](auto& alt) -> std::expected<void, E> {
                content::Deserializer<typename D::config_type> buffered_deserializer(
                    *buffered_content);
                ETD_EXPECTED_TRY(serde::deserialize(buffered_deserializer, alt));
                ETD_EXPECTED_TRY(buffered_deserializer.finish());
                return {};
            }));
        }

        return d_struct.end();
    } else {
        ETD_EXPECTED_TRY(expect_next_key(TagAttr::field_names[0]));
        ETD_EXPECTED_TRY(d_struct.deserialize_value(tag_value));
        ETD_EXPECTED_TRY(expect_next_key(TagAttr::field_names[1]));
        ETD_EXPECTED_TRY(deserialize_content_for_tag(read_content_direct));
        return d_struct.end();
    }
}

template <typename E, typename S, typename... Ts, typename TagAttr>
constexpr auto serialize_internally_tagged(S& s, const std::variant<Ts...>& value, TagAttr)
    -> std::expected<typename S::value_type, E> {
    constexpr auto names = refl::resolve_tag_names<TagAttr, Ts...>();
    constexpr std::string_view tag_field = TagAttr::field_names[0];

    return std::visit(
        [&](const auto& item) -> std::expected<typename S::value_type, E> {
            using alt_t = std::remove_cvref_t<decltype(item)>;
            static_assert(refl::reflectable_class<alt_t>,
                          "internally_tagged requires struct alternatives");

            using config_t = config::config_of<S>;
            ETD_EXPECTED_TRY_V(auto s_struct,
                               s.serialize_struct("", refl::field_count<alt_t>() + 1));

            // tag field first
            auto tag_name = names[value.index()];
            ETD_EXPECTED_TRY(s_struct.serialize_field(tag_field, tag_name));

            // struct fields
            std::expected<void, E> field_result;
            refl::for_each(item, [&](auto field) {
                auto r = serialize_struct_field<config_t, E>(s_struct, field);
                if(!r) {
                    field_result = std::unexpected(r.error());
                    return false;
                }
                return true;
            });
            if(!field_result) {
                return std::unexpected(field_result.error());
            }
            return s_struct.end();
        },
        value);
}

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_internally_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E> {
    using config_t = config::config_of<D>;

    // Requires capture_dom_value() — buffer to content DOM, then two-pass dispatch
    ETD_EXPECTED_TRY_V(auto dom_result, d.capture_dom_value());

    constexpr auto names = refl::resolve_tag_names<TagAttr, Ts...>();
    constexpr std::string_view tag_field = TagAttr::field_names[0];

    auto obj_ref = dom_result.as_ref();
    auto obj = obj_ref.get_object();
    if(!obj) {
        return std::unexpected(E::invalid_type("object", "non-object"));
    }

    // Pass 1: find tag
    std::string_view tag_value;
    bool found = false;
    for(auto entry: *obj) {
        if(entry.key == tag_field) {
            auto s = entry.value.get_string();
            if(!s) {
                return std::unexpected(E::invalid_type("string", "non-string"));
            }
            tag_value = *s;
            found = true;
            break;
        }
    }
    if(!found) {
        return std::unexpected(E::missing_field(tag_field));
    }

    // Pass 2: match tag -> deserialize full object as that struct type
    return match_and_deserialize_alt<E>(tag_value,
                                        names,
                                        value,
                                        [&](auto& alt) -> std::expected<void, E> {
                                            using alt_t = std::remove_cvref_t<decltype(alt)>;
                                            static_assert(
                                                refl::reflectable_class<alt_t>,
                                                "internally_tagged requires struct alternatives");

                                            content::Deserializer<config_t> deser(obj_ref);
                                            ETD_EXPECTED_TRY(serde::deserialize(deser, alt));
                                            ETD_EXPECTED_TRY(deser.finish());
                                            return {};
                                        });
}

}  // namespace eventide::serde::detail

namespace eventide::serde {

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

    if constexpr(refl::annotated_type<U>) {
        return expected_type_hints<typename U::annotated_type>();
    } else if constexpr(eventide::is_specialization_of<std::optional, U>) {
        return type_hint::null_like | expected_type_hints<typename U::value_type>();
    } else if constexpr(null_like<U>) {
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
