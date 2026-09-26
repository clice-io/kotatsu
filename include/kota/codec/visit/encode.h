#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "config.h"
#include "context.h"
#include "kota/support/type_list.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/meta/enum.h"
#include "kota/meta/repr.h"
#include "kota/meta/schema.h"
#include "kota/meta/struct.h"
#include "kota/meta/type_info.h"
#include "kota/meta/type_kind.h"

namespace kota::codec {

/// Backend-internal dispatch override for one (visitor, type) pair. Not a
/// user extension point: declare a type's representation via meta::repr, or
/// per-field via behavior::with — those are visible to schema consumers, a
/// serialize_visit specialization is not.
template <typename Vis, typename T, typename Config = default_config<>, typename = void>
struct serialize_visit {};

template <typename Config, typename Vis, typename T>
bool encode_value(Vis& vis, const T& value);

template <typename Config, typename Vis, typename T>
bool encode_struct_fields(Vis& vis, const T& value);

namespace detail {

/// Encode a value through a representation declaration (a meta::repr
/// specialization or a behavior::with adapter): declarative to() when
/// present, the imperative serialize<Config>() body otherwise.
template <typename Repr, typename Config, typename Vis, typename V>
bool repr_encode(Vis& vis, const V& value) {
    using declared_t = meta::declared_repr_t<Repr>;
    if constexpr(std::is_same_v<declared_t, meta::dynamic>) {
        static_assert(!is_layout_computed<Vis>(),
                      "this backend computes the output layout statically and cannot encode a "
                      "meta::dynamic repr");
    }
    if constexpr(requires { Repr::to(value); }) {
        static_assert(
            !requires { Repr::template serialize<Config>(vis, value); },
            "repr protocol: define to() or serialize() for encoding, not both");
        static_assert(std::is_same_v<std::remove_cvref_t<decltype(Repr::to(value))>, declared_t>,
                      "repr protocol: to() must return the declared representation type");
        return encode_value<Config>(vis, Repr::to(value));
    } else if constexpr(requires { Repr::template serialize<Config>(vis, value); }) {
        return Repr::template serialize<Config>(vis, value);
    } else {
        static_assert(dependent_false<Repr>,
                      "repr protocol: no encode path — define to() or serialize()");
        return false;
    }
}

/// Encodes a variant in one of the three tagged shapes selected by the
/// spec's meta::tag_mode (tag names resolve through resolve_tag_names):
/// - external:  { "TagName": payload }
/// - internal:  { "<tag>": "TagName", ...payload fields } — requires every
///   alternative to be a struct, whose fields are spliced in after the tag
/// - adjacent:  { "<tag>": "TagName", "<content>": payload }
/// Tagging only applies on human-readable backends; binary backends encode
/// the alternative index instead (see the backend's visit_variant).
template <typename Config, typename SpecAttr, typename Vis, typename Var>
bool encode_tagged_variant(Vis& vis, const Var& var) {
    // MSVC mis-handles uncaptured constexpr locals inside the nested generic
    // lambdas below (C3861/ICE), so the spec is read via SpecAttr each time.
    return [&]<typename... Ts>(const std::variant<Ts...>&) -> bool {
        constexpr auto names = meta::resolve_tag_names<SpecAttr, Ts...>();
        std::string_view tag_name = names[var.index()];

        if constexpr(SpecAttr::value.tagging == meta::tag_mode::external) {
            return vis.visit_struct(var, [&](auto& sv) -> bool {
                return sv.visit_field(std::size_t(0), tag_name, [&](auto& pv) -> bool {
                    return std::visit(
                        [&](const auto& alt) -> bool { return encode_value<Config>(pv, alt); },
                        var);
                });
            });
        } else if constexpr(SpecAttr::value.tagging == meta::tag_mode::internal) {
            return std::visit(
                [&](const auto& alt) -> bool {
                    using alt_t = std::remove_cvref_t<decltype(alt)>;
                    static_assert(meta::reflectable_class<alt_t>,
                                  "internally tagged requires struct alternatives");
                    return vis.visit_struct(alt, [&](auto& sv) -> bool {
                        KOTA_CODEC_TRY(sv.visit_field(
                            std::size_t(0),
                            SpecAttr::value.tag,
                            [&](auto& tv) -> bool { return tv.visit_str(tag_name); }));
                        return encode_struct_fields<Config>(sv, alt);
                    });
                },
                var);
        } else {
            static_assert(SpecAttr::value.tagging == meta::tag_mode::adjacent);
            return vis.visit_struct(var, [&](auto& sv) -> bool {
                KOTA_CODEC_TRY(
                    sv.visit_field(std::size_t(0), SpecAttr::value.tag, [&](auto& tv) -> bool {
                        return tv.visit_str(tag_name);
                    }));
                return sv.visit_field(
                    std::size_t(1),
                    SpecAttr::value.content,
                    [&](auto& cv) -> bool {
                        return std::visit(
                            [&](const auto& alt) -> bool { return encode_value<Config>(cv, alt); },
                            var);
                    });
            });
        }
    }(var);
}

/// Encode a single struct field, applying behavior transforms if present.
template <typename Config, std::size_t I, typename Vis, typename T>
bool encode_one_field(Vis& vis, const T& value) {
    using schema = meta::virtual_schema<T, Config>;
    using slots = typename schema::slots;
    using slot_t = type_list_element_t<I, slots>;
    using raw_t = typename slot_t::raw_type;
    using attrs_t = typename slot_t::attrs;

    constexpr std::size_t offset = schema::fields[I].offset;
    const auto* base = reinterpret_cast<const std::byte*>(std::addressof(value));
    const auto& field_ref = *reinterpret_cast<const raw_t*>(base + offset);

    if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::skip_if>) {
        using pred = typename tuple_find_spec_t<attrs_t, meta::behavior::skip_if>::predicate;
        if(meta::evaluate_skip_predicate<pred>(field_ref, true)) {
            return true;
        }
    } else if constexpr(constexpr auto when = meta::spec_of<attrs_t>.skip_if;
                        when != meta::skip_when::never) {
        if(meta::evaluate_skip_when<when>(field_ref, true)) {
            return true;
        }
    }

    constexpr auto idx = std::integral_constant<std::size_t, I>{};
    std::string_view name = schema::fields[I].name;

    bool ok;
    if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::with>) {
        using adapter = typename tuple_find_spec_t<attrs_t, meta::behavior::with>::adapter;
        ok = vis.visit_field(idx, name, [&](auto& fv) -> bool {
            return repr_encode<adapter, Config>(fv, field_ref);
        });
    } else if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::as>) {
        using target = typename tuple_find_spec_t<attrs_t, meta::behavior::as>::target;
        target converted(field_ref);
        ok = vis.visit_field(idx, name, [&](auto& fv) -> bool {
            return encode_value<Config>(fv, converted);
        });
    } else if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::enum_string>) {
        using policy = typename tuple_find_spec_t<attrs_t, meta::behavior::enum_string>::policy;
        static_assert(std::is_enum_v<raw_t>, "behavior::enum_string requires an enum type");
        auto renamed = policy{}(true, meta::enum_name(field_ref));
        std::string_view sv(renamed);
        ok = vis.visit_field(idx, name, [&](auto& fv) -> bool { return fv.visit_str(sv); });
    } else if constexpr(meta::struct_spec_of<attrs_t>.tagging != meta::tag_mode::none) {
        static_assert(meta::kind_of<raw_t>() == meta::type_kind::variant,
                      "tagged attribute requires a variant type");
        if constexpr(!is_human_readable<Config, Vis>()) {
            ok = vis.visit_field(idx, name, [&](auto& fv) -> bool {
                return encode_value<Config>(fv, field_ref);
            });
        } else {
            using spec_attr = tuple_find_t<attrs_t, meta::is_struct_spec_attr>;
            ok = vis.visit_field(idx, name, [&](auto& fv) -> bool {
                return encode_tagged_variant<Config, spec_attr>(fv, field_ref);
            });
        }
    } else if constexpr(meta::reflectable_class<raw_t> &&
                        (meta::struct_spec_of<attrs_t>.rename_all != naming::casing::identity ||
                         meta::struct_spec_of<attrs_t>.deny_unknown_fields)) {
        ok = vis.visit_field(idx, name, [&](auto& fv) -> bool {
            return encode_value<meta::merged_config_t<Config, attrs_t>>(fv, field_ref);
        });
    } else {
        ok = vis.visit_field(idx, name, [&](auto& fv) -> bool {
            return encode_value<Config>(fv, field_ref);
        });
    }

    if constexpr(Config::detailed_error) {
        if(!ok) {
            if(auto* e = scoped_context<typename Vis::error_type>::try_current())
                e->prepend_field(name);
        }
    }
    return ok;
}

}  // namespace detail

template <typename Config, typename Vis, typename T>
bool encode_value(Vis& vis, const T& value) {
    using V = T;

    if constexpr(requires(Vis& v, const V& val) {
                     serialize_visit<Vis, V, Config>::visit(v, val);
                 }) {
        static_assert(!meta::has_repr<V, meta::format_of_t<Vis>>,
                      "type has both a serialize_visit specialization and a meta::repr; "
                      "keep exactly one");
        return serialize_visit<Vis, V, Config>::visit(vis, value);
    } else if constexpr(meta::annotated_type<V>) {
        using attrs_t = typename V::attrs;
        auto&& inner = meta::annotated_value(value);
        using inner_t = std::remove_cvref_t<decltype(inner)>;

        if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::with>) {
            // Behavior precedence (with > as > enum_string, all above variant
            // tagging) mirrors encode_one_field and meta's repr resolver.
            using adapter = typename tuple_find_spec_t<attrs_t, meta::behavior::with>::adapter;
            return detail::repr_encode<adapter, Config>(vis, inner);
        } else if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::as>) {
            using target = typename tuple_find_spec_t<attrs_t, meta::behavior::as>::target;
            target converted(inner);
            return encode_value<Config>(vis, converted);
        } else if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::enum_string>) {
            using policy = typename tuple_find_spec_t<attrs_t, meta::behavior::enum_string>::policy;
            static_assert(std::is_enum_v<inner_t>, "behavior::enum_string requires an enum type");
            auto renamed = policy{}(true, meta::enum_name(inner));
            std::string_view sv(renamed);
            return vis.visit_str(sv);
        } else if constexpr(is_specialization_of<std::variant, inner_t> &&
                            meta::struct_spec_of<attrs_t>.tagging != meta::tag_mode::none) {
            if constexpr(!is_human_readable<Config, Vis>()) {
                return encode_value<Config>(vis, inner);
            } else {
                using spec_attr = tuple_find_t<attrs_t, meta::is_struct_spec_attr>;
                return detail::encode_tagged_variant<Config, spec_attr>(vis, inner);
            }
        } else if constexpr(meta::reflectable_class<inner_t> &&
                            (meta::struct_spec_of<attrs_t>.rename_all != naming::casing::identity ||
                             meta::struct_spec_of<attrs_t>.deny_unknown_fields)) {
            return encode_value<meta::merged_config_t<Config, attrs_t>>(vis, inner);
        } else {
            return encode_value<Config>(vis, inner);
        }
    } else if constexpr(meta::has_repr<V, meta::format_of_t<Vis>>) {
        return detail::repr_encode<meta::repr_for<V, meta::format_of_t<Vis>>, Config>(vis, value);
    } else {
        constexpr auto kind = meta::kind_of<V>();
        using enum meta::type_kind;

        if constexpr(kind == boolean) {
            return vis.visit_bool(value);
        } else if constexpr(meta::int_like<V>) {
            return vis.visit_int(value);
        } else if constexpr(meta::uint_like<V>) {
            return vis.visit_uint(value);
        } else if constexpr(kind == float32 || kind == float64) {
            if constexpr(Config::nan_repr != nan_repr::Passthrough) {
                // Judge the document's value, not the in-memory one: every
                // writer narrows to double, so a finite long double beyond
                // double's range lands in the document as infinity and must
                // take the non-finite path here.
                double narrowed = static_cast<double>(value);
                if(std::isnan(narrowed) || std::isinf(narrowed)) {
                    if constexpr(Config::nan_repr == nan_repr::Null) {
                        return vis.visit_null();
                    } else if constexpr(Config::nan_repr == nan_repr::String) {
                        return vis.visit_str(std::isnan(narrowed) ? "NaN"
                                             : narrowed > 0       ? "Infinity"
                                                                  : "-Infinity");
                    } else if constexpr(Config::nan_repr == nan_repr::Error) {
                        return scoped_context<typename Vis::error_type>::fail(
                            rich_error("NaN or Infinity is not allowed"));
                    } else {
                        static_assert(dependent_false<V>, "unknown nan_repr value");
                    }
                } else {
                    return vis.visit_float(value);
                }
            } else {
                return vis.visit_float(value);
            }
        } else if constexpr(meta::str_like<V> && std::is_array_v<V>) {
            // A char array need not end in a null character, so its text
            // stops at the array's end.
            std::string_view text(value, std::extent_v<V>);
            return vis.visit_str(text.substr(0, text.find('\0')));
        } else if constexpr(meta::str_like<V>) {
            return vis.visit_str(value);
        } else if constexpr(kind == character) {
            return vis.visit_char(value);
        } else if constexpr(kind == bytes) {
            return vis.visit_bytes(value);
        } else if constexpr(kind == null) {
            return vis.visit_null();
        } else if constexpr(kind == optional || kind == pointer) {
            if constexpr(is_specialization_of<std::weak_ptr, V>) {
                auto sp = value.lock();
                if constexpr(requires(Vis& v, const decltype(sp)& p) { v.visit_pointer(p); }) {
                    return vis.visit_pointer(sp);
                } else {
                    if(sp) {
                        return encode_value<Config>(vis, *sp);
                    }
                    return vis.visit_null();
                }
            } else if constexpr(kind == pointer &&
                                requires(Vis& v, const V& p) { v.visit_pointer(p); }) {
                return vis.visit_pointer(value);
            } else {
                if(value) {
                    if constexpr(requires(Vis& v, int x) {
                                     v.visit_some(x, [](auto&) -> bool { return true; });
                                 }) {
                        return vis.visit_some(*value, [&](auto& sv) -> bool {
                            return encode_value<Config>(sv, *value);
                        });
                    } else {
                        return encode_value<Config>(vis, *value);
                    }
                }
                return vis.visit_null();
            }
        } else if constexpr(kind == enumeration) {
            if constexpr(Config::enum_repr == enum_repr::String) {
                // A value without a reflected member name has no string
                // spelling: emitting "" would produce a document the decode
                // side can never map back, so fail loudly instead.
                auto raw = meta::enum_name(value);
                if(raw.empty()) {
                    using U = std::underlying_type_t<V>;
                    using wide =
                        std::conditional_t<std::is_signed_v<U>, std::int64_t, std::uint64_t>;
                    return scoped_context<typename Vis::error_type>::fail(
                        rich_error(std::format("enum value {} has no reflected name",
                                               static_cast<wide>(value))));
                }
                auto name = apply_enum_rename<Config>(true, raw);
                std::string_view sv(name);
                return vis.visit_str(sv);
            } else if constexpr(requires { vis.visit_enum(value); }) {
                return vis.visit_enum(value);
            } else {
                using U = std::underlying_type_t<V>;
                if constexpr(std::is_signed_v<U>) {
                    return vis.visit_int(static_cast<U>(value));
                } else {
                    return vis.visit_uint(static_cast<U>(value));
                }
            }
        } else if constexpr(kind == array || kind == set) {
            return vis.visit_seq(value, [&](auto& sv) -> bool {
                using element_t = std::ranges::range_value_t<V>;
                std::size_t idx = 0;
                for(const auto& elem: value) {
                    bool ok = sv.visit_element([&](auto& ev) -> bool {
                        // vector<bool> iteration yields a proxy reference on
                        // libc++; materialize it as the element type before
                        // dispatch, mirroring the decode path.
                        if constexpr(std::is_same_v<std::remove_cvref_t<decltype(elem)>,
                                                    element_t>) {
                            return encode_value<Config>(ev, elem);
                        } else {
                            return encode_value<Config>(ev, static_cast<element_t>(elem));
                        }
                    });
                    if(!ok) {
                        if constexpr(Config::detailed_error) {
                            if(auto* e = scoped_context<typename Vis::error_type>::try_current())
                                e->prepend_index(idx);
                        }
                        return false;
                    }
                    ++idx;
                }
                return true;
            });
        } else if constexpr(kind == tuple) {
            return vis.visit_tuple(value, [&](auto& sv) -> bool {
                return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                    return ([&] {
                        bool ok = sv.visit_element([&](auto& ev) -> bool {
                            return encode_value<Config>(ev, std::get<Is>(value));
                        });
                        if constexpr(Config::detailed_error) {
                            if(!ok) {
                                if(auto* e =
                                       scoped_context<typename Vis::error_type>::try_current())
                                    e->prepend_index(Is);
                            }
                        }
                        return ok;
                    }() && ...);
                }(std::make_index_sequence<std::tuple_size_v<V>>{});
            });
        } else if constexpr(kind == map) {
            return vis.visit_map(value, [&](auto& mv) -> bool {
                std::size_t idx = 0;
                for(const auto& [k, v]: value) {
                    bool ok = mv.visit_entry(
                        [&](auto& kv) -> bool { return encode_value<Config>(kv, k); },
                        [&](auto& vv) -> bool { return encode_value<Config>(vv, v); });
                    if(!ok) {
                        if constexpr(Config::detailed_error) {
                            if(auto* e = scoped_context<typename Vis::error_type>::try_current())
                                e->prepend_index(idx);
                        }
                        return false;
                    }
                    ++idx;
                }
                return true;
            });
        } else if constexpr(kind == structure) {
            return vis.visit_struct(value, [&](auto& sv) -> bool {
                return encode_struct_fields<Config>(sv, value);
            });
        } else if constexpr(kind == variant) {
            if constexpr(is_expected_v<V>) {
                if(value.has_value()) {
                    if constexpr(!std::is_void_v<typename V::value_type>) {
                        return encode_value<Config>(vis, *value);
                    } else {
                        return vis.visit_null();
                    }
                } else {
                    using E = std::remove_cvref_t<decltype(value.error())>;
                    // An error of a type the schema does not know reaches only
                    // a backend that shows any value.
                    if constexpr(meta::kind_of<E>() != meta::type_kind::unknown ||
                                 requires(Vis& v, const E& e) { v.visit_opaque(e); }) {
                        return encode_value<Config>(vis, value.error());
                    } else {
                        return vis.visit_null();
                    }
                }
            } else if constexpr(requires(Vis& v) {
                                    v.visit_variant(std::size_t{},
                                                    [](auto&) -> bool { return true; });
                                }) {
                return vis.visit_variant(value.index(), [&](auto& pv) -> bool {
                    return std::visit(
                        [&](const auto& alt) -> bool { return encode_value<Config>(pv, alt); },
                        value);
                });
            } else {
                return std::visit(
                    [&](const auto& alt) -> bool { return encode_value<Config>(vis, alt); },
                    value);
            }
        } else if constexpr(std::is_pointer_v<V> &&
                            requires(Vis& v, const V& p) { v.visit_pointer(p); }) {
            return vis.visit_pointer(value);
        } else if constexpr(requires(Vis& v, const V& x) { v.visit_opaque(x); }) {
            // Only a backend that can show any value, like the debug one,
            // takes a type the schema knows nothing about.
            return vis.visit_opaque(value);
        } else {
            static_assert(dependent_false<V>,
                          "cannot serialize this type; specialize serialize_visit to add support");
            return false;
        }
    }
}

template <typename Config, typename Vis, typename T>
bool encode_struct_fields(Vis& vis, const T& value) {
    using schema = meta::virtual_schema<T, Config>;
    using slots = typename schema::slots;
    constexpr std::size_t N = type_list_size_v<slots>;

    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (detail::encode_one_field<Config, Is>(vis, value) && ...);
    }(std::make_index_sequence<N>{});
}

}  // namespace kota::codec
