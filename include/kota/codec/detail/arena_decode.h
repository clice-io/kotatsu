#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/support/ranges.h"
#include "kota/support/type_list.h"
#include "kota/support/type_traits.h"
#include "kota/meta/attrs.h"
#include "kota/meta/enum.h"
#include "kota/meta/schema.h"
#include "kota/codec/config.h"
#include "kota/codec/detail/arena_traits.h"
#include "kota/codec/detail/common.h"
#include "kota/codec/traits.h"

namespace kota::codec::arena {

namespace detail {

using codec::detail::clean_t;

template <typename B, typename T>
constexpr bool root_unboxed_for =
    (meta::reflectable_class<T> && !B::template can_inline_struct_field<T> &&
     !std::ranges::input_range<T> && !is_pair_v<T> && !is_tuple_v<T>) ||
    is_pair_v<T> || is_tuple_v<T> || is_specialization_of<std::variant, T>;

template <typename Config, typename B, typename Raw, typename Attrs, typename V>
auto decode_value_at(const B& d,
                     typename B::TableView view,
                     typename B::slot_id sid,
                     V& out,
                     bool required) -> std::expected<void, typename B::error_type>;

template <typename Config, typename B, typename T>
auto decode_table(const B& d, typename B::TableView view, T& out)
    -> std::expected<void, typename B::error_type>;

template <typename Config, typename B, typename T>
auto decode_tuple_like(const B& d, typename B::TableView view, T& out)
    -> std::expected<void, typename B::error_type>;

template <typename Config, typename B, typename T>
auto decode_variant(const B& d, typename B::TableView view, T& out)
    -> std::expected<void, typename B::error_type>;

template <typename Config, typename B, typename T>
auto decode_sequence(const B& d,
                     typename B::TableView view,
                     typename B::slot_id sid,
                     T& out,
                     bool required) -> std::expected<void, typename B::error_type>;

template <typename Config, typename B, typename T>
auto decode_map(const B& d,
                typename B::TableView view,
                typename B::slot_id sid,
                T& out,
                bool required) -> std::expected<void, typename B::error_type>;

template <typename Config, typename B, typename T>
auto decode_unboxed(const B& d, typename B::TableView view, T& out)
    -> std::expected<void, typename B::error_type> {
    using U = detail::clean_t<T>;
    if constexpr(is_pair_v<U> || is_tuple_v<U>) {
        return decode_tuple_like<Config>(d, view, out);
    } else if constexpr(is_specialization_of<std::variant, U>) {
        return decode_variant<Config>(d, view, out);
    } else if constexpr(meta::reflectable_class<U> && !B::template can_inline_struct_field<U> &&
                        !std::ranges::input_range<U>) {
        return decode_table<Config>(d, view, out);
    } else {
        return std::unexpected(B::error_type::unsupported_type);
    }
}

template <typename Config, typename B, typename T>
auto decode_root(const B& d, T& out) -> std::expected<void, typename B::error_type> {
    using E = typename B::error_type;
    using U = std::remove_cvref_t<T>;
    using clean_u_t = detail::clean_t<U>;

    auto view = d.root_view();

    if constexpr(meta::annotated_type<U>) {
        return decode_root<Config>(d, meta::annotated_value(out));
    } else if constexpr(is_specialization_of<std::optional, U>) {
        using value_t = typename U::value_type;
        using clean_value_t = detail::clean_t<value_t>;

        if constexpr(root_unboxed_for<B, clean_value_t>) {
            if(!view.any_field_present()) {
                out.reset();
                return {};
            }

            if(!out.has_value()) {
                if constexpr(std::default_initializable<value_t>) {
                    out.emplace();
                } else {
                    return std::unexpected(E::unsupported_type);
                }
            }

            auto status = decode_unboxed<Config>(d, view, out.value());
            if(!status) {
                out.reset();
                return status;
            }
            return {};
        } else {
            auto sid_r = B::field_slot_id(0);
            if(!sid_r) {
                return std::unexpected(sid_r.error());
            }
            if(view.has(*sid_r)) {
                if(!out.has_value()) {
                    if constexpr(std::default_initializable<value_t>) {
                        out.emplace();
                    } else {
                        return std::unexpected(E::unsupported_type);
                    }
                }
                auto status = decode_value_at<Config, B, value_t, std::tuple<>>(d,
                                                                                view,
                                                                                *sid_r,
                                                                                out.value(),
                                                                                true);
                if(!status) {
                    out.reset();
                    return status;
                }
                return {};
            }
            out.reset();
            return {};
        }
    } else if constexpr(root_unboxed_for<B, clean_u_t>) {
        return decode_unboxed<Config>(d, view, out);
    } else {
        auto sid_r = B::field_slot_id(0);
        if(!sid_r) {
            return std::unexpected(sid_r.error());
        }
        return decode_value_at<Config, B, U, std::tuple<>>(d, view, *sid_r, out, true);
    }
}

template <typename Config, typename B, typename T, std::size_t I>
auto decode_struct_slot(const B& d, typename B::TableView view, T& out)
    -> std::expected<void, typename B::error_type> {
    using schema = meta::virtual_schema<T, Config>;
    using slot_t = kota::type_list_element_t<I, typename schema::slots>;
    using raw_t = std::remove_cv_t<typename slot_t::raw_type>;
    using attrs_t = typename slot_t::attrs;

    constexpr std::size_t offset = schema::fields[I].offset;

    auto* base = reinterpret_cast<std::byte*>(std::addressof(out));
    auto& field_value = *reinterpret_cast<raw_t*>(base + offset);

    auto sid_r = B::field_slot_id(I);
    if(!sid_r) {
        return std::unexpected(sid_r.error());
    }
    return decode_value_at<Config, B, raw_t, attrs_t>(d,
                                                      view,
                                                      *sid_r,
                                                      field_value,
                                                      /*required=*/false);
}

template <typename Config, typename B, typename T>
auto decode_table(const B& d, typename B::TableView view, T& out)
    -> std::expected<void, typename B::error_type> {
    using E = typename B::error_type;
    using U = std::remove_cvref_t<T>;
    static_assert(meta::reflectable_class<U>, "decode_table requires reflectable class");

    if(!view.valid()) {
        return std::unexpected(E::invalid_state);
    }

    using schema = meta::virtual_schema<U, Config>;
    using slots = typename schema::slots;
    constexpr std::size_t N = kota::type_list_size_v<slots>;

    std::expected<void, E> status{};
    bool ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return ([&] {
            auto r = decode_struct_slot<Config, B, U, Is>(d, view, out);
            if(!r) {
                status = std::unexpected(r.error());
                return false;
            }
            return true;
        }() && ...);
    }(std::make_index_sequence<N>{});

    if(!ok) {
        return std::unexpected(status.error());
    }
    return {};
}

template <typename Config, typename B, typename T>
auto decode_tuple_like(const B& d, typename B::TableView view, T& out)
    -> std::expected<void, typename B::error_type> {
    using E = typename B::error_type;
    if(!view.valid()) {
        return std::unexpected(E::invalid_state);
    }

    using U = std::remove_cvref_t<T>;

    std::expected<void, E> status{};
    bool ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return ([&] {
            using element_t = std::tuple_element_t<Is, U>;
            auto sid_r = B::field_slot_id(Is);
            if(!sid_r) {
                status = std::unexpected(sid_r.error());
                return false;
            }
            auto r = decode_value_at<Config, B, element_t, std::tuple<>>(d,
                                                                         view,
                                                                         *sid_r,
                                                                         std::get<Is>(out),
                                                                         /*required=*/false);
            if(!r) {
                status = std::unexpected(r.error());
                return false;
            }
            return true;
        }() && ...);
    }(std::make_index_sequence<std::tuple_size_v<U>>{});

    if(!ok) {
        return std::unexpected(status.error());
    }
    return {};
}

template <typename Config, typename B, typename T>
auto decode_variant(const B& d, typename B::TableView view, T& out)
    -> std::expected<void, typename B::error_type> {
    using E = typename B::error_type;
    using U = std::remove_cvref_t<T>;
    static_assert(is_specialization_of<std::variant, U>, "decode_variant requires variant");

    if(!view.valid()) {
        return std::unexpected(E::invalid_state);
    }

    const auto tag_sid = B::variant_tag_slot_id();
    if(!view.has(tag_sid)) {
        return std::unexpected(E::invalid_state);
    }

    const auto index = static_cast<std::size_t>(view.template get_scalar<std::uint32_t>(tag_sid));
    if(index >= std::variant_size_v<U>) {
        return std::unexpected(E::invalid_state);
    }

    std::expected<void, E> status{};
    bool matched = false;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (([&] {
             if(index != Is) {
                 return;
             }
             matched = true;

             auto sid_r = B::variant_payload_slot_id(Is);
             if(!sid_r) {
                 status = std::unexpected(sid_r.error());
                 return;
             }
             using alt_t = std::variant_alternative_t<Is, U>;
             if constexpr(!std::default_initializable<alt_t>) {
                 status = std::unexpected(E::unsupported_type);
             } else {
                 alt_t alt{};
                 auto r = decode_value_at<Config, B, alt_t, std::tuple<>>(d,
                                                                          view,
                                                                          *sid_r,
                                                                          alt,
                                                                          /*required=*/true);
                 if(!r) {
                     status = std::unexpected(r.error());
                 } else {
                     out = std::move(alt);
                 }
             }
         }()),
         ...);
    }(std::make_index_sequence<std::variant_size_v<U>>{});

    if(!matched) {
        return std::unexpected(E::invalid_state);
    }
    if(!status) {
        return std::unexpected(status.error());
    }
    return {};
}

template <typename Config, typename B, typename Raw, typename Attrs, typename V>
auto decode_value_at(const B& d,
                     typename B::TableView view,
                     typename B::slot_id sid,
                     V& out,
                     bool required) -> std::expected<void, typename B::error_type> {
    using E = typename B::error_type;
    using U = std::remove_cvref_t<V>;

    if constexpr(meta::annotated_type<U>) {
        using inner = typename U::annotated_type;
        return decode_value_at<Config, B, inner, Attrs>(d,
                                                        view,
                                                        sid,
                                                        meta::annotated_value(out),
                                                        required);
    } else if constexpr(kota::tuple_has_spec_v<Attrs, meta::behavior::as>) {
        using target = typename kota::tuple_find_spec_t<Attrs, meta::behavior::as>::target;
        if(!view.has(sid)) {
            if(required) {
                return std::unexpected(E::invalid_state);
            }
            return {};
        }
        target tmp{};
        KOTA_EXPECTED_TRY((decode_value_at<Config, B, target, std::tuple<>>(d,
                                                                            view,
                                                                            sid,
                                                                            tmp,
                                                                            /*required=*/true)));
        out = static_cast<U>(std::move(tmp));
        return {};
    } else if constexpr(kota::tuple_has_spec_v<Attrs, meta::behavior::enum_string>) {
        using clean_u = detail::clean_t<U>;
        static_assert(std::is_enum_v<clean_u>, "enum_string requires an enum type");
        if(!view.has(sid)) {
            if(required) {
                return std::unexpected(E::invalid_state);
            }
            return {};
        }
        std::string name;
        KOTA_EXPECTED_TRY(
            (decode_value_at<Config, B, std::string, std::tuple<>>(d,
                                                                   view,
                                                                   sid,
                                                                   name,
                                                                   /*required=*/true)));
        auto parsed = meta::enum_value<clean_u>(name);
        if(!parsed.has_value()) {
            return std::unexpected(E::invalid_state);
        }
        out = static_cast<U>(*parsed);
        return {};
    } else if constexpr(kota::tuple_has_spec_v<Attrs, meta::behavior::with>) {
        using adapter = typename kota::tuple_find_spec_t<Attrs, meta::behavior::with>::adapter;
        using wire_t = typename adapter::wire_type;
        if(!view.has(sid)) {
            if(required) {
                return std::unexpected(E::invalid_state);
            }
            return {};
        }
        wire_t wire{};
        KOTA_EXPECTED_TRY((decode_value_at<Config, B, wire_t, std::tuple<>>(d,
                                                                            view,
                                                                            sid,
                                                                            wire,
                                                                            /*required=*/true)));
        out = static_cast<U>(adapter::from_wire(std::move(wire)));
        return {};
    } else if constexpr(arena::streaming_deserialize_traits<B, U>) {
        using traits = kota::codec::deserialize_traits<B, std::remove_cvref_t<U>>;
        return traits::deserialize(d, view, sid, out);
    } else if constexpr(arena::value_deserialize_traits<B, U>) {
        using traits = kota::codec::deserialize_traits<B, std::remove_cvref_t<U>>;
        using wire_t = typename traits::wire_type;
        if(!view.has(sid)) {
            if(required) {
                return std::unexpected(E::invalid_state);
            }
            return {};
        }
        wire_t wire{};
        KOTA_EXPECTED_TRY((decode_value_at<Config, B, wire_t, std::tuple<>>(d,
                                                                            view,
                                                                            sid,
                                                                            wire,
                                                                            /*required=*/true)));
        out = static_cast<U>(traits::deserialize(d, std::move(wire)));
        return {};
    } else {
        using clean_u_t = detail::clean_t<U>;

        if constexpr(is_specialization_of<std::optional, U>) {
            using value_t = typename U::value_type;
            if(!view.has(sid)) {
                out.reset();
                return {};
            }

            if(!out.has_value()) {
                if constexpr(std::default_initializable<value_t>) {
                    out.emplace();
                } else {
                    return std::unexpected(E::unsupported_type);
                }
            }

            auto status = decode_value_at<Config, B, value_t, std::tuple<>>(d,
                                                                            view,
                                                                            sid,
                                                                            out.value(),
                                                                            /*required=*/true);
            if(!status) {
                out.reset();
                return status;
            }
            return {};
        } else if constexpr(is_specialization_of<std::unique_ptr, U>) {
            using value_t = typename U::element_type;
            static_assert(std::default_initializable<value_t>,
                          "arena unique_ptr decode requires default-constructible pointee");
            static_assert(std::same_as<typename U::deleter_type, std::default_delete<value_t>>,
                          "arena unique_ptr decode with custom deleter is unsupported");

            if(!view.has(sid)) {
                out.reset();
                return {};
            }

            auto value = std::make_unique<value_t>();
            auto status = decode_value_at<Config, B, value_t, std::tuple<>>(d,
                                                                            view,
                                                                            sid,
                                                                            *value,
                                                                            /*required=*/true);
            if(!status) {
                return status;
            }
            out = std::move(value);
            return {};
        } else if constexpr(is_specialization_of<std::shared_ptr, U>) {
            using value_t = typename U::element_type;
            static_assert(std::default_initializable<value_t>,
                          "arena shared_ptr decode requires default-constructible pointee");

            if(!view.has(sid)) {
                out.reset();
                return {};
            }

            auto value = std::make_shared<value_t>();
            auto status = decode_value_at<Config, B, value_t, std::tuple<>>(d,
                                                                            view,
                                                                            sid,
                                                                            *value,
                                                                            /*required=*/true);
            if(!status) {
                return status;
            }
            out = std::move(value);
            return {};
        } else {
            if(!view.has(sid)) {
                if(required) {
                    return std::unexpected(E::invalid_state);
                }
                return {};
            }

            if constexpr(std::same_as<clean_u_t, std::nullptr_t>) {
                return {};
            } else if constexpr(std::is_enum_v<clean_u_t>) {
                using underlying_t = std::underlying_type_t<clean_u_t>;
                auto value = view.template get_scalar<underlying_t>(sid);
                out = static_cast<U>(static_cast<clean_u_t>(value));
                return {};
            } else if constexpr(codec::bool_like<clean_u_t> || codec::int_like<clean_u_t> ||
                                codec::uint_like<clean_u_t>) {
                out = static_cast<U>(view.template get_scalar<clean_u_t>(sid));
                return {};
            } else if constexpr(codec::floating_like<clean_u_t>) {
                if constexpr(std::same_as<clean_u_t, float> || std::same_as<clean_u_t, double>) {
                    out = static_cast<U>(view.template get_scalar<clean_u_t>(sid));
                } else {
                    out = static_cast<U>(view.template get_scalar<double>(sid));
                }
                return {};
            } else if constexpr(codec::char_like<clean_u_t>) {
                out = static_cast<U>(static_cast<char>(view.template get_scalar<std::int8_t>(sid)));
                return {};
            } else if constexpr(codec::str_like<clean_u_t>) {
                KOTA_EXPECTED_TRY_V(auto text, d.get_string(view, sid));
                if constexpr(std::same_as<U, std::string> || std::derived_from<U, std::string>) {
                    out.assign(text.data(), text.size());
                    return {};
                } else if constexpr(std::same_as<U, std::string_view>) {
                    out = std::string_view(text.data(), text.size());
                    return {};
                } else if constexpr(std::constructible_from<U, const char*, std::size_t>) {
                    out = U(text.data(), text.size());
                    return {};
                } else {
                    return std::unexpected(E::unsupported_type);
                }
            } else if constexpr(codec::bytes_like<clean_u_t>) {
                if constexpr(std::same_as<U, std::vector<std::byte>>) {
                    KOTA_EXPECTED_TRY_V(auto bytes, d.get_bytes(view, sid));
                    out.resize(bytes.size());
                    for(std::size_t i = 0; i < bytes.size(); ++i) {
                        out[i] = bytes[i];
                    }
                    return {};
                } else {
                    return std::unexpected(E::unsupported_type);
                }
            } else if constexpr(is_specialization_of<std::variant, U>) {
                KOTA_EXPECTED_TRY_V(auto nested, d.get_table(view, sid));
                return decode_variant<Config>(d, nested, out);
            } else if constexpr(std::ranges::input_range<clean_u_t>) {
                constexpr auto kind = kota::format_kind<clean_u_t>;
                if constexpr(kind == kota::range_format::map) {
                    return decode_map<Config>(d, view, sid, out, required);
                } else {
                    return decode_sequence<Config>(d, view, sid, out, required);
                }
            } else if constexpr(is_pair_v<clean_u_t> || is_tuple_v<clean_u_t>) {
                KOTA_EXPECTED_TRY_V(auto nested, d.get_table(view, sid));
                return decode_tuple_like<Config>(d, nested, out);
            } else if constexpr(B::template can_inline_struct_field<clean_u_t>) {
                KOTA_EXPECTED_TRY_V(auto value,
                                    (d.template get_inline_struct<clean_u_t>(view, sid)));
                out = static_cast<U>(value);
                return {};
            } else if constexpr(meta::reflectable_class<clean_u_t>) {
                KOTA_EXPECTED_TRY_V(auto nested, d.get_table(view, sid));
                return decode_table<Config>(d, nested, out);
            } else {
                return std::unexpected(E::unsupported_type);
            }
        }
    }
}

template <typename Config, typename B, typename T>
auto decode_sequence(const B& d,
                     typename B::TableView view,
                     typename B::slot_id sid,
                     T& out,
                     bool required) -> std::expected<void, typename B::error_type> {
    using E = typename B::error_type;
    using U = std::remove_cvref_t<T>;
    using element_t = std::ranges::range_value_t<U>;
    using element_clean_t = detail::clean_t<element_t>;

    if constexpr(arena::value_deserialize_traits<B, element_clean_t>) {
        using traits = kota::codec::deserialize_traits<B, element_clean_t>;
        using wire_t = typename traits::wire_type;
        if(!view.has(sid)) {
            if(required) {
                return std::unexpected(E::invalid_state);
            }
            return {};
        }
        std::vector<wire_t> scratch;
        KOTA_EXPECTED_TRY((decode_sequence<Config>(d, view, sid, scratch, /*required=*/true)));
        if constexpr(requires { out.clear(); }) {
            out.clear();
        }
        for(auto& w: scratch) {
            auto ok = kota::detail::append_sequence_element(
                out,
                static_cast<element_t>(traits::deserialize(d, std::move(w))));
            if(!ok) {
                return std::unexpected(E::unsupported_type);
            }
        }
        return {};
    }

    if(!view.has(sid)) {
        if(required) {
            return std::unexpected(E::invalid_state);
        }
        return {};
    }

    if constexpr(requires { out.clear(); }) {
        out.clear();
    }

    constexpr bool index_assignable_fixed_size =
        !kota::detail::sequence_insertable<U, element_t> &&
        requires(U& container, std::size_t index, element_t value) {
            std::tuple_size<U>::value;
            container[index] = std::move(value);
        };

    std::size_t written_count = 0;
    auto store_element = [&](auto&& element) -> std::expected<void, E> {
        if constexpr(index_assignable_fixed_size) {
            constexpr auto expected_count = std::tuple_size_v<U>;
            if(written_count >= expected_count) {
                return std::unexpected(E::invalid_state);
            }
            out[written_count] = static_cast<element_t>(std::forward<decltype(element)>(element));
            ++written_count;
            return {};
        } else {
            auto ok = kota::detail::append_sequence_element(
                out,
                static_cast<element_t>(std::forward<decltype(element)>(element)));
            if(!ok) {
                return std::unexpected(E::unsupported_type);
            }
            return {};
        }
    };

    auto finalize_sequence = [&]() -> std::expected<void, E> {
        if constexpr(index_assignable_fixed_size) {
            constexpr auto expected_count = std::tuple_size_v<U>;
            if(written_count != expected_count) {
                return std::unexpected(E::invalid_state);
            }
        }
        return {};
    };

    if constexpr(std::same_as<element_clean_t, std::byte>) {
        KOTA_EXPECTED_TRY_V(auto bytes, d.get_bytes(view, sid));
        for(std::size_t i = 0; i < bytes.size(); ++i) {
            KOTA_EXPECTED_TRY(store_element(bytes[i]));
        }
        return finalize_sequence();
    } else if constexpr(codec::bool_like<element_clean_t> || codec::int_like<element_clean_t> ||
                        codec::uint_like<element_clean_t>) {
        KOTA_EXPECTED_TRY_V(auto vec, (d.template get_scalar_vector<element_clean_t>(view, sid)));
        for(std::size_t i = 0; i < vec.size(); ++i) {
            KOTA_EXPECTED_TRY(store_element(vec[i]));
        }
        return finalize_sequence();
    } else if constexpr(codec::floating_like<element_clean_t>) {
        if constexpr(std::same_as<element_clean_t, float> ||
                     std::same_as<element_clean_t, double>) {
            KOTA_EXPECTED_TRY_V(auto vec,
                                (d.template get_scalar_vector<element_clean_t>(view, sid)));
            for(std::size_t i = 0; i < vec.size(); ++i) {
                KOTA_EXPECTED_TRY(store_element(vec[i]));
            }
            return finalize_sequence();
        } else {
            KOTA_EXPECTED_TRY_V(auto vec, (d.template get_scalar_vector<double>(view, sid)));
            for(std::size_t i = 0; i < vec.size(); ++i) {
                KOTA_EXPECTED_TRY(store_element(static_cast<element_clean_t>(vec[i])));
            }
            return finalize_sequence();
        }
    } else if constexpr(codec::char_like<element_clean_t>) {
        KOTA_EXPECTED_TRY_V(auto vec, (d.template get_scalar_vector<std::int8_t>(view, sid)));
        for(std::size_t i = 0; i < vec.size(); ++i) {
            KOTA_EXPECTED_TRY(store_element(static_cast<char>(vec[i])));
        }
        return finalize_sequence();
    } else if constexpr(std::is_enum_v<element_clean_t>) {
        using storage_t = std::underlying_type_t<element_clean_t>;
        KOTA_EXPECTED_TRY_V(auto vec, (d.template get_scalar_vector<storage_t>(view, sid)));
        for(std::size_t i = 0; i < vec.size(); ++i) {
            KOTA_EXPECTED_TRY(store_element(static_cast<element_clean_t>(vec[i])));
        }
        return finalize_sequence();
    } else if constexpr(codec::str_like<element_clean_t>) {
        KOTA_EXPECTED_TRY_V(auto vec, d.get_string_vector(view, sid));
        for(std::size_t i = 0; i < vec.size(); ++i) {
            auto text = vec[i];
            if constexpr(std::same_as<element_t, std::string> ||
                         std::derived_from<element_t, std::string>) {
                KOTA_EXPECTED_TRY(store_element(std::string(text.data(), text.size())));
            } else if constexpr(std::same_as<element_t, std::string_view>) {
                KOTA_EXPECTED_TRY(store_element(std::string_view(text.data(), text.size())));
            } else if constexpr(std::constructible_from<element_t, const char*, std::size_t>) {
                KOTA_EXPECTED_TRY(store_element(element_t(text.data(), text.size())));
            } else {
                return std::unexpected(E::unsupported_type);
            }
        }
        return finalize_sequence();
    } else if constexpr(is_pair_v<element_clean_t> || is_tuple_v<element_clean_t>) {
        KOTA_EXPECTED_TRY_V(auto vec, d.get_table_vector(view, sid));
        for(std::size_t i = 0; i < vec.size(); ++i) {
            using dec_t = std::remove_cvref_t<element_t>;
            if constexpr(!std::default_initializable<dec_t>) {
                return std::unexpected(E::unsupported_type);
            } else {
                dec_t element{};
                auto nested = vec[i];
                KOTA_EXPECTED_TRY(decode_tuple_like<Config>(d, nested, element));
                KOTA_EXPECTED_TRY(store_element(std::move(element)));
            }
        }
        return finalize_sequence();
    } else if constexpr(B::template can_inline_struct_element<element_clean_t>) {
        KOTA_EXPECTED_TRY_V(auto vec,
                            (d.template get_inline_struct_vector<element_clean_t>(view, sid)));
        for(std::size_t i = 0; i < vec.size(); ++i) {
            KOTA_EXPECTED_TRY(store_element(vec[i]));
        }
        return finalize_sequence();
    } else if constexpr(meta::reflectable_class<element_clean_t>) {
        KOTA_EXPECTED_TRY_V(auto vec, d.get_table_vector(view, sid));
        for(std::size_t i = 0; i < vec.size(); ++i) {
            using dec_t = std::remove_cvref_t<element_t>;
            if constexpr(!std::default_initializable<dec_t>) {
                return std::unexpected(E::unsupported_type);
            } else {
                dec_t element{};
                auto nested = vec[i];
                KOTA_EXPECTED_TRY(decode_table<Config>(d, nested, element));
                KOTA_EXPECTED_TRY(store_element(std::move(element)));
            }
        }
        return finalize_sequence();
    } else {
        KOTA_EXPECTED_TRY_V(auto vec, d.get_table_vector(view, sid));
        for(std::size_t i = 0; i < vec.size(); ++i) {
            using dec_t = std::remove_cvref_t<element_t>;
            if constexpr(!std::default_initializable<dec_t>) {
                return std::unexpected(E::unsupported_type);
            } else {
                dec_t element{};
                auto nested = vec[i];
                auto sid_r = B::field_slot_id(0);
                if(!sid_r) {
                    return std::unexpected(sid_r.error());
                }
                if constexpr(root_unboxed_for<B, dec_t>) {
                    KOTA_EXPECTED_TRY(decode_unboxed<Config>(d, nested, element));
                } else {
                    KOTA_EXPECTED_TRY(
                        (decode_value_at<Config, B, dec_t, std::tuple<>>(d,
                                                                         nested,
                                                                         *sid_r,
                                                                         element,
                                                                         /*required=*/true)));
                }
                KOTA_EXPECTED_TRY(store_element(std::move(element)));
            }
        }
        return finalize_sequence();
    }
}

template <typename Config, typename B, typename T>
auto decode_map(const B& d,
                typename B::TableView view,
                typename B::slot_id sid,
                T& out,
                bool required) -> std::expected<void, typename B::error_type> {
    using E = typename B::error_type;
    using U = std::remove_cvref_t<T>;
    using ref_t = std::remove_cvref_t<std::ranges::range_reference_t<U>>;
    using key_t = map_entry_key_t<ref_t>;
    using mapped_t = map_entry_mapped_t<ref_t>;

    if(!view.has(sid)) {
        if(required) {
            return std::unexpected(E::invalid_state);
        }
        return {};
    }

    if constexpr(requires { out.clear(); }) {
        out.clear();
    }
    static_assert(kota::detail::map_insertable<U, key_t, mapped_t>,
                  "arena map decode requires insertable container");

    KOTA_EXPECTED_TRY_V(auto entries, d.get_table_vector(view, sid));

    for(std::size_t i = 0; i < entries.size(); ++i) {
        auto entry = entries[i];
        if(!entry.valid()) {
            return std::unexpected(E::invalid_state);
        }

        auto make_key = [] {
            if constexpr(std::is_class_v<key_t>) {
                key_t k;
                return k;
            } else {
                return key_t{};
            }
        };
        auto make_mapped = [] {
            if constexpr(std::is_class_v<mapped_t>) {
                mapped_t m;
                return m;
            } else {
                return mapped_t{};
            }
        };
        key_t key = make_key();
        mapped_t mapped = make_mapped();
        auto key_sid = B::field_slot_id(0);
        if(!key_sid) {
            return std::unexpected(key_sid.error());
        }
        auto val_sid = B::field_slot_id(1);
        if(!val_sid) {
            return std::unexpected(val_sid.error());
        }
        KOTA_EXPECTED_TRY((decode_value_at<Config, B, key_t, std::tuple<>>(d,
                                                                           entry,
                                                                           *key_sid,
                                                                           key,
                                                                           /*required=*/true)));
        KOTA_EXPECTED_TRY((decode_value_at<Config, B, mapped_t, std::tuple<>>(d,
                                                                              entry,
                                                                              *val_sid,
                                                                              mapped,
                                                                              /*required=*/true)));

        auto ok = kota::detail::insert_map_entry(out, std::move(key), std::move(mapped));
        if(!ok) {
            return std::unexpected(E::unsupported_type);
        }
    }

    return {};
}

}  // namespace detail

using detail::decode_map;
using detail::decode_root;
using detail::decode_sequence;
using detail::decode_table;
using detail::decode_tuple_like;
using detail::decode_unboxed;
using detail::decode_value_at;
using detail::decode_variant;
using detail::root_unboxed_for;

}  // namespace kota::codec::arena
