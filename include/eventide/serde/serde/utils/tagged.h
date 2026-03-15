#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "eventide/common/expected_try.h"
#include "eventide/reflection/struct.h"
#include "eventide/serde/serde/annotation.h"
#include "eventide/serde/serde/attrs.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/schema/schema.h"
#include "eventide/serde/serde/utils/field_dispatch.h"
#include "eventide/serde/serde/utils/fwd.h"

namespace eventide::serde::detail {

/// Match tag_value against variant alternative names, construct the matching alternative,
/// call reader(alt) to deserialize it, then assign to the variant.
template <typename E, typename... Ts, std::size_t N, typename Reader>
constexpr auto match_and_deserialize_alt(std::string_view tag_value,
                                         const std::array<std::string_view, N>& names,
                                         std::variant<Ts...>& value,
                                         Reader&& reader) -> std::expected<void, E> {
    static_assert(N == sizeof...(Ts));
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
        return std::unexpected(E::type_mismatch);
    }
    return status;
}

/// Visit variant and call emitter with the active alternative's value.
template <typename E, typename R, typename... Ts, typename Emitter>
constexpr auto visit_variant_alt(const std::variant<Ts...>& value, Emitter&& emit)
    -> std::expected<R, E> {
    std::expected<R, E> result{std::unexpected(E::invalid_state)};
    std::visit([&](const auto& item) { result = emit(item); }, value);
    return result;
}

/// Deserialize variant alternative at a runtime index.
/// Creates a fresh deserializer from source, deserializes, and finishes.
template <typename D, typename Source, typename... Ts, std::size_t... Is>
auto deserialize_variant_at_index_impl(std::size_t idx,
                                       Source&& source,
                                       std::variant<Ts...>& v,
                                       std::index_sequence<Is...>)
    -> std::expected<void, typename D::error_type> {
    using E = typename D::error_type;
    std::expected<void, E> result = std::unexpected(E::type_mismatch);
    bool matched = false;

    (([&] {
         if(matched || Is != idx) {
             return;
         }
         matched = true;
         using alt_t = std::variant_alternative_t<Is, std::variant<Ts...>>;
         if constexpr(std::default_initializable<alt_t>) {
             alt_t alt{};
             D deser(std::forward<Source>(source));
             auto status = serde::deserialize(deser, alt);
             if(!status) {
                 result = std::unexpected(status.error());
                 return;
             }
             auto finished = deser.finish();
             if(!finished) {
                 result = std::unexpected(finished.error());
                 return;
             }
             v = std::move(alt);
             result = {};
         } else {
             result = std::unexpected(E::invalid_state);
         }
     }()),
     ...);

    return result;
}

template <typename D, typename Source, typename... Ts>
auto deserialize_variant_at_index(std::size_t idx,
                                  Source&& source,
                                  std::variant<Ts...>& v)
    -> std::expected<void, typename D::error_type> {
    return deserialize_variant_at_index_impl<D>(
        idx, std::forward<Source>(source), v, std::make_index_sequence<sizeof...(Ts)>{});
}

// ─── Schema-driven tagged serialization ─────────────────────────────────────

/// Unified tagged variant serialization.
/// AnnotatedV is the full annotated type (e.g. annotation<variant<Ts...>, tagged<...>>).
/// The schema layer provides tag_mode, tag/content field names, and alt names.
template <typename E, typename AnnotatedV, typename S, typename... Ts>
constexpr auto serialize_tagged(S& s, const std::variant<Ts...>& value)
    -> std::expected<typename S::value_type, E> {
    constexpr auto mode = schema::resolve_tag_mode<AnnotatedV>();
    static_assert(mode != schema::tag_mode::none, "serialize_tagged requires a tagged variant");

    constexpr auto& names = schema::detail::alt_names_storage<std::remove_cvref_t<AnnotatedV>>::data;
    constexpr auto tag_field = schema::resolve_tag_field<AnnotatedV>();
    constexpr auto content_field = schema::resolve_content_field<AnnotatedV>();

    if constexpr(mode == schema::tag_mode::external) {
        ET_EXPECTED_TRY_V(auto s_struct, s.serialize_struct("", 1));
        auto name = names[value.index()];
        ET_EXPECTED_TRY(
            (visit_variant_alt<E, void>(value, [&](const auto& item) -> std::expected<void, E> {
                return s_struct.serialize_field(name, item);
            })));
        return s_struct.end();
    } else if constexpr(mode == schema::tag_mode::internal) {
        return std::visit(
            [&](const auto& item) -> std::expected<typename S::value_type, E> {
                using alt_t = std::remove_cvref_t<decltype(item)>;
                static_assert(refl::reflectable_class<alt_t>,
                              "internally_tagged requires struct alternatives");
                using config_t = config::config_of<S>;
                ET_EXPECTED_TRY_V(auto s_struct,
                                  s.serialize_struct("", refl::field_count<alt_t>() + 1));
                auto tag_name = names[value.index()];
                ET_EXPECTED_TRY(s_struct.serialize_field(tag_field, tag_name));
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
    } else {
        static_assert(mode == schema::tag_mode::adjacent);
        ET_EXPECTED_TRY_V(auto s_struct, s.serialize_struct("", 2));
        auto name = names[value.index()];
        ET_EXPECTED_TRY(s_struct.serialize_field(tag_field, name));
        ET_EXPECTED_TRY(
            (visit_variant_alt<E, void>(value, [&](const auto& item) -> std::expected<void, E> {
                return s_struct.serialize_field(content_field, item);
            })));
        return s_struct.end();
    }
}

// ─── Schema-driven tagged deserialization ───────────────────────────────────

/// Unified tagged variant deserialization.
/// AnnotatedV is the full annotated type (e.g. annotation<variant<Ts...>, tagged<...>>).
template <typename E, typename AnnotatedV, typename D, typename... Ts>
constexpr auto deserialize_tagged(D& d, std::variant<Ts...>& value)
    -> std::expected<void, E> {
    constexpr auto mode = schema::resolve_tag_mode<AnnotatedV>();
    static_assert(mode != schema::tag_mode::none, "deserialize_tagged requires a tagged variant");

    constexpr auto& names = schema::detail::alt_names_storage<std::remove_cvref_t<AnnotatedV>>::data;
    constexpr auto tag_field = schema::resolve_tag_field<AnnotatedV>();
    constexpr auto content_field = schema::resolve_content_field<AnnotatedV>();

    if constexpr(mode == schema::tag_mode::external) {
        ET_EXPECTED_TRY_V(auto d_struct, d.deserialize_struct("", 1));
        ET_EXPECTED_TRY_V(auto key, d_struct.next_key());
        if(!key.has_value()) {
            return std::unexpected(E::type_mismatch);
        }
        ET_EXPECTED_TRY((match_and_deserialize_alt<E>(*key, names, value, [&](auto& alt) {
            return d_struct.deserialize_value(alt);
        })));
        return d_struct.end();
    } else if constexpr(mode == schema::tag_mode::internal) {
        static_assert(requires { d.consume_variant_source(); },
                      "internally tagged deserialization requires consume_variant_source()");
        ET_EXPECTED_TRY_V(auto source, d.consume_variant_source());

        std::string tag_value;
        {
            D tag_reader(source);
            ET_EXPECTED_TRY_V(auto d_struct, tag_reader.deserialize_struct("", 0));
            bool found = false;
            while(true) {
                ET_EXPECTED_TRY_V(auto key, d_struct.next_key());
                if(!key.has_value()) break;
                if(*key == tag_field) {
                    ET_EXPECTED_TRY(d_struct.deserialize_value(tag_value));
                    found = true;
                    break;
                }
                ET_EXPECTED_TRY(d_struct.skip_value());
            }
            if(!found) {
                return std::unexpected(E::type_mismatch);
            }
        }

        return match_and_deserialize_alt<E>(
            tag_value, names, value, [&](auto& alt) -> std::expected<void, E> {
                using alt_t = std::remove_cvref_t<decltype(alt)>;
                static_assert(refl::reflectable_class<alt_t>,
                              "internally_tagged requires struct alternatives");
                D content_reader(source);
                ET_EXPECTED_TRY(serde::deserialize(content_reader, alt));
                ET_EXPECTED_TRY(content_reader.finish());
                return {};
            });
    } else {
        static_assert(mode == schema::tag_mode::adjacent);
        static_assert(requires { d.consume_variant_source(); },
                      "adjacently tagged deserialization requires consume_variant_source()");
        ET_EXPECTED_TRY_V(auto source, d.consume_variant_source());

        std::string tag_value;
        {
            D tag_reader(source);
            ET_EXPECTED_TRY_V(auto d_struct, tag_reader.deserialize_struct("", 2));
            bool found = false;
            while(true) {
                ET_EXPECTED_TRY_V(auto key, d_struct.next_key());
                if(!key.has_value()) break;
                if(*key == tag_field) {
                    ET_EXPECTED_TRY(d_struct.deserialize_value(tag_value));
                    found = true;
                    break;
                }
                ET_EXPECTED_TRY(d_struct.skip_value());
            }
            if(!found) {
                return std::unexpected(E::type_mismatch);
            }
        }

        return match_and_deserialize_alt<E>(
            tag_value, names, value, [&](auto& alt) -> std::expected<void, E> {
                D content_reader(source);
                ET_EXPECTED_TRY_V(auto d_struct, content_reader.deserialize_struct("", 2));
                bool found_content = false;
                while(true) {
                    ET_EXPECTED_TRY_V(auto key, d_struct.next_key());
                    if(!key.has_value()) break;
                    if(*key == content_field) {
                        ET_EXPECTED_TRY(d_struct.deserialize_value(alt));
                        found_content = true;
                        break;
                    }
                    ET_EXPECTED_TRY(d_struct.skip_value());
                }
                if(!found_content) {
                    return std::unexpected(E::type_mismatch);
                }
                return {};
            });
    }
}

}  // namespace eventide::serde::detail
