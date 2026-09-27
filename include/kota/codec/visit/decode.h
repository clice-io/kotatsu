#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <format>
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

#include "config.h"
#include "context.h"
#include "dispatch.h"
#include "kota/support/ranges.h"
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

/// How a visitor reads one type, overridden per backend or protocol; the
/// decode counterpart of serialize_visit (see there), with a static
/// visit(Vis&, T&) returning bool. It is consulted before annotations and
/// meta::repr, and untagged variant probing always tries an alternative that
/// has one, whatever its declared shape.
template <typename Vis, typename T, typename Config>
struct deserialize_visit {};

template <typename Config, typename Vis, typename T>
bool decode_value(Vis& vis, T& out);

template <typename Config, typename Vis, typename T>
bool decode_struct_fields(Vis& vis, T& out);

template <typename Config, typename TagAttr, typename Vis, typename Var>
bool decode_variant(Vis& vis, Var& var);

namespace detail {

/// Decode a value through a representation declaration (a meta::repr
/// specialization or a behavior::with adapter): declarative from() when
/// present, the imperative deserialize<Config>() body otherwise.
template <typename Repr, typename Config, typename Vis, typename V>
bool repr_decode(Vis& vis, V& out) {
    using declared_t = meta::declared_repr_t<Repr>;
    if constexpr(std::is_same_v<declared_t, meta::dynamic>) {
        static_assert(!layout_computed<Vis>,
                      "this backend computes the output layout statically and cannot decode a "
                      "meta::dynamic repr");
    }
    if constexpr(requires { Repr::from(std::declval<declared_t>()); }) {
        static_assert(
            !requires { Repr::template deserialize<Config>(vis, out); },
            "repr protocol: define from() or deserialize() for decoding, not both");
        static_assert(
            requires(declared_t&& d) { out = Repr::from(std::move(d)); },
            "repr protocol: from() must accept the declared representation type and return "
            "the value type");
        declared_t declared{};
        KOTA_CODEC_TRY(decode_value<Config>(vis, declared));
        out = Repr::from(std::move(declared));
        return true;
    } else if constexpr(requires { Repr::template deserialize<Config>(vis, out); }) {
        return Repr::template deserialize<Config>(vis, out);
    } else {
        static_assert(dependent_false<Repr>,
                      "repr protocol: no decode path — define from() or deserialize()");
        return false;
    }
}

/// True when the visitor uses data-driven (push-style) decoding.
template <typename Vis>
concept data_driven = requires { requires Vis::data_driven; };

/// Ensure an optional/pointer is allocated before writing into it.
template <typename T>
void ensure_allocated(T& out) {
    if constexpr(is_optional_v<T>) {
        if(!out.has_value()) {
            out.emplace();
        }
    } else if constexpr(is_specialization_of<std::unique_ptr, T>) {
        if(!out) {
            out = std::make_unique<typename T::element_type>();
        }
    } else if constexpr(is_specialization_of<std::shared_ptr, T>) {
        if(!out) {
            out = std::make_shared<typename T::element_type>();
        }
    }
}

/// Construct the I-th alternative of a variant and decode into it.
template <typename Config, typename Vis, typename... Ts>
bool construct_and_visit(Vis& vis, std::variant<Ts...>& out, std::size_t index) {
    if(index >= sizeof...(Ts)) {
        return scoped_context<rich_error>::fail(
            rich_error(std::format("invalid variant index {}", index)));
    }
    return with_index<sizeof...(Ts)>(index, [&](auto i) {
        constexpr std::size_t I = decltype(i)::value;
        return decode_value<Config>(vis, out.template emplace<I>());
    });
}

/// True when the visitor supports peeking the source data kind without consuming.
template <typename Vis>
concept has_peek_kind = requires(Vis& v) {
    { v.peek_kind() } -> std::same_as<meta::type_kind>;
};

/// True when the visitor supports speculative read with automatic rollback on failure.
template <typename Vis>
concept has_try_read = requires(Vis& v) {
    {
        v.try_read([](Vis&) -> bool { return true; })
    } -> std::same_as<bool>;
};

/// Sentinel type: no tag attribute (untagged variant).
struct no_tag {};

/// True when the visitor provides native variant support (bincode/fbs).
template <typename Vis>
concept has_native_variant =
    requires(Vis& v) { v.visit_variant([](std::size_t, auto&) -> bool { return true; }); };

/// The alternative a tag names; N when it names none.
template <std::size_t N>
std::size_t find_tag_index(std::string_view name, const std::array<std::string_view, N>& names) {
    return static_cast<std::size_t>(std::ranges::find(names, name) - names.begin());
}

/// Fails with the error for a tag that names no alternative.
inline bool fail_unknown_tag(std::string_view name) {
    return scoped_context<rich_error>::fail(
        rich_error(std::format("unknown variant tag '{}'", name)));
}

/// Reads an enum as its name, rename(name) spelling the enumerator.
template <typename E, typename Vis, typename Rename>
bool decode_enum_name(Vis& vis, E& out, Rename rename) {
    std::string name;
    KOTA_CODEC_TRY(vis.visit_str(name));
    if(auto value = meta::enum_value<E>(rename(name))) {
        out = *value;
        return true;
    }
    return scoped_context<rich_error>::fail(
        rich_error(std::format("unknown enum value '{}'", name)));
}

/// Decodes a value under a node's attributes (a struct field's, or an
/// annotation's), mirroring encode_with_attrs: behavior::with >
/// behavior::as > behavior::enum_string > variant tagging > the rename_all /
/// deny_unknown_fields merge.
template <typename Config, typename Attrs, typename Vis, typename T>
bool decode_with_attrs(Vis& vis, T& out) {
    if constexpr(tuple_has_spec_v<Attrs, meta::behavior::with>) {
        using adapter = typename tuple_find_spec_t<Attrs, meta::behavior::with>::adapter;
        return repr_decode<adapter, Config>(vis, out);
    } else if constexpr(tuple_has_spec_v<Attrs, meta::behavior::as>) {
        using target = typename tuple_find_spec_t<Attrs, meta::behavior::as>::target;
        target converted{};
        KOTA_CODEC_TRY(decode_value<Config>(vis, converted));
        out = T(std::move(converted));
        return true;
    } else if constexpr(tuple_has_spec_v<Attrs, meta::behavior::enum_string>) {
        using policy = typename tuple_find_spec_t<Attrs, meta::behavior::enum_string>::policy;
        static_assert(std::is_enum_v<T>, "behavior::enum_string requires an enum type");
        return decode_enum_name(vis, out, [](std::string_view name) {
            return policy{}(false, name);
        });
    } else if constexpr(meta::struct_spec_of<Attrs>.tagging != meta::tag_mode::none) {
        static_assert(is_specialization_of<std::variant, T>,
                      "a tagging attribute requires a std::variant");
        if constexpr(is_human_readable<Config, Vis>()) {
            using spec_attr = tuple_find_t<Attrs, meta::is_struct_spec_attr>;
            return decode_variant<Config, spec_attr>(vis, out);
        } else {
            return decode_value<Config>(vis, out);
        }
    } else {
        return decode_value<meta::node_config_t<Config, T, Attrs>>(vis, out);
    }
}

/// Decode a field's value applying behavior transforms, without visit_field wrapping.
/// Used by match_field (data-driven path) where the field reader is already provided.
template <typename Config, std::size_t I, typename Vis, typename T>
bool decode_field_value(Vis& vis, T& out) {
    using field = FieldAt<Config, I, T>;
    auto& field_ref = field::of(out);
    // Only a skip_if predicate can hold while decoding: skip_when conditions
    // judge a value being written.
    if constexpr(tuple_has_spec_v<typename field::attrs, meta::behavior::skip_if>) {
        if(skipped<typename field::attrs>(field_ref, false)) {
            return vis.visit_skip();
        }
    }
    return trace_path<Config>(decode_with_attrs<Config, typename field::attrs>(vis, field_ref),
                              field::name);
}

/// The slot whose name or alias is key; fields.size() when none is.
inline std::size_t find_field_slot(std::span<const meta::field_info> fields, std::string_view key) {
    for(std::size_t i = 0; i < fields.size(); ++i) {
        if(fields[i].name == key ||
           std::ranges::find(fields[i].aliases, key) != fields[i].aliases.end()) {
            return i;
        }
    }
    return fields.size();
}

/// Data-driven field matching: decodes the slot key names into out, or
/// skips (or rejects) an unknown key. Sets the slot's bit in field_mask
/// when given.
template <typename Config, typename T, typename Vis>
bool match_field(std::string_view key, Vis& reader, T& out, std::uint64_t* field_mask = nullptr) {
    using schema = meta::virtual_schema<T, Config>;
    constexpr std::size_t N = type_list_size_v<typename schema::slots>;
    static_assert(N <= 64, "struct field count exceeds field_mask capacity (max 64 fields)");

    std::size_t slot = find_field_slot(schema::fields, key);
    if(slot == N) {
        if constexpr(Config::deny_unknown_fields || schema::deny_unknown) {
            // Prefer the backend hook so DOM backends (e.g. TOML) can attach
            // the offending node's source location to the error.
            if constexpr(requires {
                             { reader.fail_unknown_field(key) } -> std::same_as<bool>;
                         }) {
                return reader.fail_unknown_field(key);
            } else {
                return scoped_context<rich_error>::fail(rich_error::unknown_field(key));
            }
        } else {
            return reader.visit_skip();
        }
    }

    if(field_mask) {
        *field_mask |= std::uint64_t{1} << slot;
    }
    return with_index<N>(slot, [&](auto i) {
        return decode_field_value<Config, decltype(i)::value>(reader, out);
    });
}

/// Whether the input must carry a slot: its type is not nullable (seen
/// through annotations), and neither its attrs nor, for an annotated type,
/// that annotation give it a default or a skip condition.
template <typename Slot>
constexpr bool slot_required = [] {
    using raw_t = std::remove_cv_t<typename Slot::raw_type>;
    using attrs_t = typename Slot::attrs;
    constexpr auto kind = meta::kind_of<raw_t>();
    if(kind == meta::type_kind::optional || kind == meta::type_kind::pointer ||
       kind == meta::type_kind::null) {
        return false;
    }
    if(tuple_has_spec_v<attrs_t, meta::behavior::skip_if> ||
       meta::spec_of<attrs_t>.skip_if != meta::skip_when::never ||
       meta::spec_of<attrs_t>.defaulted) {
        return false;
    }
    if constexpr(meta::annotated_type<raw_t>) {
        return !meta::spec_of<typename raw_t::attrs>.defaulted;
    } else {
        return true;
    }
}();

/// Bit I set when slot I of T under Config is required.
template <typename Config, typename T>
constexpr std::uint64_t required_mask = []<typename... Slots>(type_list<Slots...>) {
    static_assert(sizeof...(Slots) <= 64,
                  "struct field count exceeds field_mask capacity (max 64 fields)");
    std::uint64_t mask = 0;
    std::size_t i = 0;
    ((mask |= std::uint64_t{slot_required<Slots>} << i++), ...);
    return mask;
}(typename meta::virtual_schema<T, Config>::slots{});

/// After data-driven struct decode, fails on the first required field the
/// input left out.
template <typename Config, typename T>
bool check_required_fields(std::uint64_t field_mask) {
    std::uint64_t missing = required_mask<Config, T> & ~field_mask;
    if(missing == 0) {
        return true;
    }
    return scoped_context<rich_error>::fail(rich_error::missing_field(
        meta::virtual_schema<T, Config>::fields[std::countr_zero(missing)].name));
}

/// External tagged: { "TagName": value }
template <typename Config, typename SpecAttr, typename Vis, typename... Ts>
bool decode_externally_tagged(Vis& vis, std::variant<Ts...>& var) {
    constexpr auto names = meta::resolve_tag_names<SpecAttr, Ts...>();
    bool found = false;
    bool result = vis.visit_struct([&](std::string_view key, auto& fv) -> bool {
        if(found) {
            return scoped_context<rich_error>::fail(
                rich_error("externally tagged variant: expected exactly one field"));
        }
        found = true;
        auto idx = find_tag_index(key, names);
        if(idx >= sizeof...(Ts)) {
            return fail_unknown_tag(key);
        }
        return construct_and_visit<Config>(fv, var, idx);
    });
    if(result && !found) {
        return scoped_context<rich_error>::fail(
            rich_error("externally tagged variant: expected exactly one field"));
    }
    return result;
}

/// Fails on the tag entry a data-driven look-ahead found no alternative for.
/// The look-ahead read this same entry, so reading it again either fails as a
/// string (a tag that is not one) or yields a name no alternative has.
template <typename Reader>
bool fail_unusable_tag(Reader& tag) {
    std::string name;
    KOTA_CODEC_TRY(tag.visit_str(name));
    return fail_unknown_tag(name);
}

/// Looks up, without consuming anything, the alternative the tag entry
/// names, so that entries before the tag can be placed; N when the tag is
/// absent or names no alternative.
template <typename Vis, std::size_t N>
std::size_t peek_tag_index(Vis& vis,
                           std::string_view tag_key,
                           const std::array<std::string_view, N>& names) {
    std::size_t idx = N;
    vis.try_read([&](auto& fork) -> bool {
        fork.visit_struct([&](std::string_view key, auto& fv) -> bool {
            if(key == tag_key) {
                std::string name;
                fv.visit_str(name);
                idx = find_tag_index(name, names);
                return false;
            }
            return true;
        });
        return false;
    });
    return idx;
}

/// Internal tagged: { "tag": "TagName", ...fields... }
/// The tag is looked up before any field is placed, so it may follow them.
template <typename Config, typename SpecAttr, typename Vis, typename... Ts>
bool decode_internally_tagged(Vis& vis, std::variant<Ts...>& var) {
    static_assert(data_driven<Vis> && has_try_read<Vis>,
                  "a tagged variant decodes through a data-driven visitor with try_read");
    constexpr std::string_view tag_key = SpecAttr::value.tag;
    constexpr auto names = meta::resolve_tag_names<SpecAttr, Ts...>();
    constexpr std::size_t npos = sizeof...(Ts);

    std::size_t idx = peek_tag_index(vis, tag_key, names);
    if(idx != npos) {
        with_index<sizeof...(Ts)>(idx, [&](auto i) {
            var.template emplace<decltype(i)::value>();
            return true;
        });
    }

    std::uint64_t field_mask = 0;
    bool result = vis.visit_struct([&](std::string_view key, auto& fv) -> bool {
        if(key == tag_key) {
            if(idx != npos)
                return fv.visit_skip();
            return fail_unusable_tag(fv);
        }
        if(idx == npos) {
            // Without a usable tag data fields cannot be placed, and need
            // not be: the tag's own entry reports why it is unusable, and
            // an absent tag is reported after the pass.
            return fv.visit_skip();
        }
        return with_index<sizeof...(Ts)>(idx, [&](auto i) {
            constexpr std::size_t I = decltype(i)::value;
            return match_field<Config, std::variant_alternative_t<I, std::variant<Ts...>>>(
                key,
                fv,
                std::get<I>(var),
                &field_mask);
        });
    });

    if(!result) {
        return false;
    }
    if(idx == npos) {
        return scoped_context<rich_error>::fail(
            rich_error("internally tagged variant: missing tag field"));
    }
    return with_index<sizeof...(Ts)>(idx, [&](auto i) {
        constexpr std::size_t I = decltype(i)::value;
        return check_required_fields<Config, std::variant_alternative_t<I, std::variant<Ts...>>>(
            field_mask);
    });
}

/// Adjacent tagged: { "t": "TagName", "c": value }
template <typename Config, typename SpecAttr, typename Vis, typename... Ts>
bool decode_adjacently_tagged(Vis& vis, std::variant<Ts...>& var) {
    static_assert(data_driven<Vis> && has_try_read<Vis>,
                  "a tagged variant decodes through a data-driven visitor with try_read");
    constexpr std::string_view tag_key = SpecAttr::value.tag;
    constexpr std::string_view content_key = SpecAttr::value.content;
    constexpr auto names = meta::resolve_tag_names<SpecAttr, Ts...>();
    constexpr std::size_t npos = sizeof...(Ts);

    std::size_t idx = peek_tag_index(vis, tag_key, names);

    std::size_t tag_count = 0;
    std::size_t content_count = 0;

    bool result = vis.visit_struct([&](std::string_view key, auto& fv) -> bool {
        if(key == tag_key) {
            ++tag_count;
            if(idx != npos)
                return fv.visit_skip();
            return fail_unusable_tag(fv);
        }
        if(key == content_key) {
            ++content_count;
            if(idx == npos) {
                // Without a usable tag the content cannot be placed: the
                // tag's own entry reports why, and an absent tag is
                // reported after the pass.
                return fv.visit_skip();
            }
            if(content_count > 1)
                return fv.visit_skip();
            return construct_and_visit<Config>(fv, var, idx);
        }
        return fv.visit_skip();
    });

    if(!result)
        return false;
    if(idx == npos) {
        return scoped_context<rich_error>::fail(
            rich_error("adjacently tagged variant: missing tag field"));
    }
    if(content_count == 0) {
        return scoped_context<rich_error>::fail(
            rich_error("adjacently tagged variant: missing content field"));
    }
    if(tag_count > 1) {
        return scoped_context<rich_error>::fail(
            rich_error("adjacently tagged variant: duplicate tag field"));
    }
    if(content_count > 1) {
        return scoped_context<rich_error>::fail(
            rich_error("adjacently tagged variant: duplicate content field"));
    }
    return true;
}

template <typename Config, typename Vis, typename T>
constexpr bool kind_compatible(meta::type_kind src, bool widen);

template <typename Config, typename Vis, typename T>
constexpr bool kind_compatible_impl(meta::type_kind src, bool widen) {
    using enum meta::type_kind;
    constexpr auto target = meta::kind_of<T>();

    if(src == unknown)
        return true;
    if constexpr(target == boolean)
        return src == boolean;
    else if constexpr(meta::int_like<T> || meta::uint_like<T>)
        return src == int64 || src == uint64;
    else if constexpr(target == float32 || target == float64)
        // Widened: every backend's visit_float accepts integer input, so a
        // float alternative may claim it — but only after the exact-kind pass
        // has let a true integer alternative go first.
        return src == float64 || (widen && (src == int64 || src == uint64));
    else if constexpr(target == character || meta::str_like<T>)
        return src == string;
    else if constexpr(target == null)
        return src == null;
    else if constexpr(target == optional || target == pointer) {
        // Null engages the nullable wrapper itself; anything else is judged
        // by the wrapped type, so a wrapper alternative never strict-claims
        // input a later alternative matches exactly —
        // variant<optional<double>, int> hands an integer to int.
        if(src == null)
            return true;
        constexpr auto wrapped = [] {
            if constexpr(target == optional)
                return std::type_identity<typename T::value_type>{};
            else
                return std::type_identity<typename T::element_type>{};
        }();
        return kind_compatible<Config, Vis, typename decltype(wrapped)::type>(src, widen);
    } else if constexpr(target == structure)
        return src == structure;
    else if constexpr(target == array || target == set)
        return src == array;
    else if constexpr(target == map)
        return src == structure;
    else if constexpr(is_specialization_of<std::variant, T>)
        // A nested variant is exactly as compatible as its alternatives:
        // aggregate them so it neither strict-claims input only a widening
        // alternative accepts nor shadows a later exact match.
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return (kind_compatible<Config, Vis, std::variant_alternative_t<Is, T>>(src, widen) ||
                    ...);
        }(std::make_index_sequence<std::variant_size_v<T>>{});
    else if constexpr(target == enumeration)
        return src == string || src == int64 || src == uint64;
    else
        return true;
}

template <typename Config, typename Vis, typename T>
constexpr bool kind_compatible(meta::type_kind src, bool widen) {
    using format = meta::format_of_t<Vis>;
    if constexpr(requires(Vis& v, T& out) { deserialize_visit<Vis, T, Config>::visit(v, out); }) {
        // A backend dispatch override decodes however it likes — RawValue's
        // JSON specialization accepts any value — so the type's declared
        // shape says nothing about what it accepts; always probe it.
        return true;
    } else {
        // An alternative arrives as its resolved representation — behavior
        // attrs on an annotation first, then (chained) reprs under the
        // visitor's format — so compatibility is judged against that; a
        // dynamic representation can be anything.
        using resolved_t = meta::resolved_repr_t<T, format>;
        if constexpr(std::is_same_v<resolved_t, meta::dynamic>) {
            return true;
        } else if constexpr(meta::resolves_to_tagged_variant<T, format> &&
                            is_human_readable<Config, Vis>()) {
            // A tagging spec survived resolution: the tagged decoders read an
            // object ({tag: value}, {tag, content}, or tag-in-fields), so the
            // alternatives' own kinds never face the input directly. A
            // non-human-readable config instead ignores tagging on decode and
            // reads the underlying variant, so its alternatives' kinds apply
            // through the branch below.
            return src == meta::type_kind::unknown || src == meta::type_kind::structure;
        } else {
            return kind_compatible_impl<Config, Vis, resolved_t>(src, widen);
        }
    }
}

/// True when this alternative decodes through decode_untagged_variant itself —
/// no dispatch override, a bare std::variant possibly behind nullable
/// wrappers (which are transparent to non-null input) or declarative repr
/// chain links (whose from() conversion runs after the variant is read) — so
/// probing it re-enters the pass structure below and the outer pass level
/// must flow into it. An imperative repr decodes through its own body
/// instead, so its inner behavior is opaque to the pass structure.
template <typename Config, typename Vis, typename T>
constexpr bool probes_as_untagged_variant() {
    if constexpr(requires(Vis& v, T& out) { deserialize_visit<Vis, T, Config>::visit(v, out); })
        return false;
    else if constexpr(meta::has_repr<T, meta::format_of_t<Vis>>) {
        using chosen = meta::repr_for<T, meta::format_of_t<Vis>>;
        if constexpr(
            requires { chosen::from(std::declval<meta::declared_repr_t<chosen>>()); } &&
            !requires(Vis& v, T& out) { chosen::template deserialize<Config>(v, out); })
            return probes_as_untagged_variant<Config, Vis, meta::declared_repr_t<chosen>>();
        else
            return false;
    } else if constexpr(is_optional_v<T>)
        return probes_as_untagged_variant<Config, Vis, typename T::value_type>();
    else if constexpr(is_specialization_of<std::unique_ptr, T> ||
                      is_specialization_of<std::shared_ptr, T>)
        return probes_as_untagged_variant<Config, Vis, typename T::element_type>();
    else
        return is_specialization_of<std::variant, T>;
}

template <typename Config, typename Vis, typename... Ts>
bool
    untagged_variant_pass(Vis& vis, std::variant<Ts...>& out, meta::type_kind src_kind, bool widen);

/// Probe one nested-variant alternative at the outer pass level. Null engages
/// the outermost nullable wrapper as usual; any other input flows through the
/// wrappers and declarative repr links into the variant's own pass at the
/// same widen level, so a wrapped or repr'd nested variant cannot run its
/// widening pass during an outer exact pass any more than a bare one can.
/// Reached only for chains probes_as_untagged_variant admitted, so a repr
/// link here always has the declarative from() decode path.
template <typename Config, typename Vis, typename T>
bool nested_alternative_pass(Vis& vis, T& out, meta::type_kind src_kind, bool widen) {
    if constexpr(meta::has_repr<T, meta::format_of_t<Vis>>) {
        using chosen = meta::repr_for<T, meta::format_of_t<Vis>>;
        meta::declared_repr_t<chosen> declared{};
        KOTA_CODEC_TRY(nested_alternative_pass<Config>(vis, declared, src_kind, widen));
        out = chosen::from(std::move(declared));
        return true;
    } else if constexpr(is_specialization_of<std::variant, T>) {
        return untagged_variant_pass<Config>(vis, out, src_kind, widen);
    } else {
        if(src_kind == meta::type_kind::null)
            return decode_value<Config>(vis, out);
        ensure_allocated(out);
        return nested_alternative_pass<Config>(vis, *out, src_kind, widen);
    }
}

/// One admission pass over an untagged variant's alternatives at the given
/// widen level, each candidate probed through a fork. A nested variant —
/// bare, under nullable wrappers, or behind a declarative repr — re-enters
/// this pass at the same level instead of running its full decode, so an
/// outer exact pass never triggers inner widening: decoding 1000 into
/// variant<variant<int8_t, double>, int64_t> must reach the outer int64_t
/// before any pass hands 1000 to the inner double. The widen pass re-admits
/// such a nested alternative even when it strict-claimed — its exact branch
/// failing (a narrowing int8_t) says nothing about its widening branch —
/// while any other alternative that strict-claimed is skipped: its retry
/// would just repeat the failure.
template <typename Config, typename Vis, typename... Ts>
bool untagged_variant_pass(Vis& vis,
                           std::variant<Ts...>& out,
                           meta::type_kind src_kind,
                           bool widen) {
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) -> bool {
        return (([&] {
                    using alt_t = std::variant_alternative_t<Is, std::variant<Ts...>>;
                    constexpr bool nested = probes_as_untagged_variant<Config, Vis, alt_t>();
                    bool strict = kind_compatible<Config, Vis, alt_t>(src_kind, false);
                    bool admit = widen ? (nested || !strict) &&
                                             kind_compatible<Config, Vis, alt_t>(src_kind, true)
                                       : strict;
                    if(!admit)
                        return false;
                    return vis.try_read([&](auto& fork) -> bool {
                        if constexpr(nested) {
                            return nested_alternative_pass<Config>(fork,
                                                                   out.template emplace<Is>(),
                                                                   src_kind,
                                                                   widen);
                        } else {
                            return construct_and_visit<Config>(fork, out, Is);
                        }
                    });
                }()) ||
                ...);
    }(std::index_sequence_for<Ts...>{});
}

template <typename Config, typename Vis, typename... Ts>
bool decode_untagged_variant(Vis& vis, std::variant<Ts...>& out) {
    static_assert(has_try_read<Vis> && has_peek_kind<Vis>,
                  "untagged variant decode requires a visitor with try_read and peek_kind");
    // Exact-kind pass first, so an integer picks an int alternative over an
    // earlier float one; the widening pass then admits the conversions the
    // decoders themselves perform (visit_float from integer input). The last
    // alternative runs once more on the real visitor so its error surfaces
    // when nothing claims the value.
    auto src_kind = vis.peek_kind();
    constexpr std::size_t last = sizeof...(Ts) - 1;
    return untagged_variant_pass<Config>(vis, out, src_kind, false) ||
           untagged_variant_pass<Config>(vis, out, src_kind, true) ||
           construct_and_visit<Config>(vis, out, last);
}

/// Decode a single struct field, applying behavior transforms if present.
/// Mirrors encode_one_field: wraps the decode in vis.visit_field(idx, name, ...).
template <typename Config, std::size_t I, typename Vis, typename T>
bool decode_one_field(Vis& vis, T& out) {
    using field = FieldAt<Config, I, T>;
    constexpr auto idx = std::integral_constant<std::size_t, I>{};
    auto& field_ref = field::of(out);

    if constexpr(tuple_has_spec_v<typename field::attrs, meta::behavior::skip_if>) {
        if(skipped<typename field::attrs>(field_ref, false)) {
            typename field::type discard{};
            return vis.visit_field(idx, field::name, [&](auto& fv) -> bool {
                return decode_value<Config>(fv, discard);
            });
        }
    }

    bool ok = vis.visit_field(idx, field::name, [&](auto& fv) -> bool {
        return decode_with_attrs<Config, typename field::attrs>(fv, field_ref);
    });
    return trace_path<Config>(ok, field::name);
}

}  // namespace detail

/// Unified variant decode: dispatches to native, tagged, or untagged path.
template <typename Config, typename SpecAttr, typename Vis, typename Var>
bool decode_variant(Vis& vis, Var& var) {
    return [&]<typename... Ts>(std::variant<Ts...>&) -> bool {
        if constexpr(detail::has_native_variant<Vis>) {
            return vis.visit_variant([&](std::size_t index, auto& pv) -> bool {
                return detail::construct_and_visit<Config>(pv, var, index);
            });
        } else if constexpr(!std::is_same_v<SpecAttr, detail::no_tag>) {
            if constexpr(!is_human_readable<Config, Vis>()) {
                return detail::decode_untagged_variant<Config>(vis, var);
            } else {
                constexpr auto tagging = SpecAttr::value.tagging;
                if constexpr(tagging == meta::tag_mode::external) {
                    return detail::decode_externally_tagged<Config, SpecAttr>(vis, var);
                } else if constexpr(tagging == meta::tag_mode::internal) {
                    return detail::decode_internally_tagged<Config, SpecAttr>(vis, var);
                } else {
                    static_assert(tagging == meta::tag_mode::adjacent);
                    return detail::decode_adjacently_tagged<Config, SpecAttr>(vis, var);
                }
            }
        } else {
            return detail::decode_untagged_variant<Config>(vis, var);
        }
    }(var);
}

template <typename Config, typename Vis, typename T>
bool decode_value(Vis& vis, T& out) {
    using V = std::remove_const_t<T>;

    if constexpr(requires(Vis& v, V& val) { deserialize_visit<Vis, V, Config>::visit(v, val); }) {
        static_assert(!meta::has_repr<V, meta::format_of_t<Vis>>,
                      "type has both a deserialize_visit specialization and a meta::repr; "
                      "keep exactly one");
        return deserialize_visit<Vis, V, Config>::visit(vis, out);
    } else if constexpr(meta::annotated_type<V>) {
        return detail::decode_with_attrs<Config, typename V::attrs>(vis,
                                                                    meta::annotated_value(out));
    } else if constexpr(meta::has_repr<V, meta::format_of_t<Vis>>) {
        return detail::repr_decode<meta::repr_for<V, meta::format_of_t<Vis>>, Config>(vis, out);
    } else {
        constexpr auto kind = meta::kind_of<V>();
        using enum meta::type_kind;

        if constexpr(kind == boolean) {
            return vis.visit_bool(out);
        } else if constexpr(meta::int_like<V>) {
            return vis.visit_int(out);
        } else if constexpr(meta::uint_like<V>) {
            return vis.visit_uint(out);
        } else if constexpr(kind == float32 || kind == float64) {
            return vis.visit_float(out);
        } else if constexpr(meta::str_like<V>) {
            return vis.visit_str(out);
        } else if constexpr(kind == character) {
            return vis.visit_char(out);
        } else if constexpr(kind == bytes) {
            return vis.visit_bytes(out);
        } else if constexpr(kind == null) {
            return vis.visit_null();
        } else if constexpr(kind == optional || kind == pointer) {
            if constexpr(requires { vis.visit_option(out, [](auto&) -> bool { return true; }); }) {
                return vis.visit_option(out, [&](auto& sv) -> bool {
                    detail::ensure_allocated(out);
                    return decode_value<Config>(sv, *out);
                });
            } else {
                if(vis.peek_null()) {
                    out = V{};
                    return vis.visit_null();
                }
                detail::ensure_allocated(out);
                return decode_value<Config>(vis, *out);
            }
        } else if constexpr(kind == enumeration) {
            if constexpr(Config::enum_repr == enum_repr::String) {
                return detail::decode_enum_name(vis, out, [](std::string_view name) {
                    return apply_enum_rename<Config>(false, name);
                });
            } else if constexpr(requires { vis.visit_enum(out); }) {
                return vis.visit_enum(out);
            } else {
                using U = std::underlying_type_t<V>;
                U underlying{};
                bool ok;
                if constexpr(std::is_signed_v<U>) {
                    ok = vis.visit_int(underlying);
                } else {
                    ok = vis.visit_uint(underlying);
                }
                if(ok) {
                    out = static_cast<V>(underlying);
                }
                return ok;
            }
        } else if constexpr(kind == structure) {
            if constexpr(detail::data_driven<Vis>) {
                std::uint64_t field_mask = 0;
                bool result = vis.visit_struct([&](std::string_view key, auto& fv) -> bool {
                    return detail::match_field<Config, V>(key, fv, out, &field_mask);
                });
                if(result) {
                    result = detail::check_required_fields<Config, V>(field_mask);
                }
                return result;
            } else {
                return vis.visit_struct(out, [&](auto& sv) -> bool {
                    return decode_struct_fields<Config>(sv, out);
                });
            }
        } else if constexpr(kind == array || kind == set) {
            using element_t = std::ranges::range_value_t<V>;
            if constexpr(detail::data_driven<Vis>) {
                if constexpr(requires { out.clear(); }) {
                    out.clear();
                }
                std::size_t idx = 0;
                return vis.visit_seq([&](auto& ev) -> bool {
                    element_t item{};
                    KOTA_CODEC_TRY(detail::trace_path<Config>(decode_value<Config>(ev, item), idx));
                    kota::detail::append_sequence_element(out, std::move(item));
                    ++idx;
                    return true;
                });
            } else {
                return vis.visit_seq(out, [&](auto& sv) -> bool {
                    if constexpr(requires { out.clear(); }) {
                        out.clear();
                    }
                    std::size_t idx = 0;
                    while(sv.has_element()) {
                        element_t item{};
                        bool ok = sv.visit_element(
                            [&](auto& ev) -> bool { return decode_value<Config>(ev, item); });
                        KOTA_CODEC_TRY(detail::trace_path<Config>(ok, idx));
                        kota::detail::append_sequence_element(out, std::move(item));
                        ++idx;
                    }
                    return true;
                });
            }
        } else if constexpr(kind == tuple) {
            if constexpr(detail::data_driven<Vis>) {
                constexpr std::size_t expected = std::tuple_size_v<V>;
                std::size_t idx = 0;
                bool seq_ok = vis.visit_tuple([&](auto& ev) -> bool {
                    if(idx >= expected) {
                        return scoped_context<rich_error>::fail(rich_error(
                            std::format("too many elements for tuple (expected {})", expected)));
                    }
                    bool ok = detail::with_index<std::tuple_size_v<V>>(idx, [&](auto i) {
                        return decode_value<Config>(ev, std::get<decltype(i)::value>(out));
                    });
                    KOTA_CODEC_TRY(detail::trace_path<Config>(ok, idx));
                    ++idx;
                    return true;
                });
                if(!seq_ok)
                    return false;
                if(idx != expected) {
                    return scoped_context<rich_error>::fail(
                        rich_error(std::format("too few elements for tuple (expected {}, got {})",
                                               expected,
                                               idx)));
                }
                return true;
            } else {
                return vis.visit_tuple(out, [&](auto& sv) -> bool {
                    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                        return (detail::trace_path<Config>(sv.visit_element([&](auto& ev) -> bool {
                                    return decode_value<Config>(ev, std::get<Is>(out));
                                }),
                                                           Is) &&
                                ...);
                    }(std::make_index_sequence<std::tuple_size_v<V>>{});
                });
            }
        } else if constexpr(kind == map) {
            using kv_t = std::ranges::range_value_t<V>;
            using key_t = std::remove_const_t<typename kv_t::first_type>;
            using mapped_t = typename kv_t::second_type;
            if constexpr(detail::data_driven<Vis>) {
                if constexpr(requires { out.clear(); }) {
                    out.clear();
                }
                std::size_t idx = 0;
                return vis.visit_map([&](auto& kv, auto& vv) -> bool {
                    key_t key{};
                    KOTA_CODEC_TRY(detail::trace_path<Config>(decode_value<Config>(kv, key), idx));
                    mapped_t val{};
                    KOTA_CODEC_TRY(detail::trace_path<Config>(decode_value<Config>(vv, val), idx));
                    kota::detail::insert_map_entry(out, std::move(key), std::move(val));
                    ++idx;
                    return true;
                });
            } else {
                return vis.visit_map(out, [&](auto& sv) -> bool {
                    if constexpr(requires { out.clear(); }) {
                        out.clear();
                    }
                    std::size_t idx = 0;
                    while(sv.has_entry()) {
                        key_t key{};
                        mapped_t val{};
                        bool ok = sv.visit_entry(
                            [&](auto& kv) -> bool { return decode_value<Config>(kv, key); },
                            [&](auto& vv) -> bool { return decode_value<Config>(vv, val); });
                        KOTA_CODEC_TRY(detail::trace_path<Config>(ok, idx));
                        kota::detail::insert_map_entry(out, std::move(key), std::move(val));
                        ++idx;
                    }
                    return true;
                });
            }
        } else if constexpr(kind == variant) {
            return decode_variant<Config, detail::no_tag>(vis, out);
        } else {
            static_assert(
                dependent_false<V>,
                "cannot deserialize this type; specialize deserialize_visit to add support");
            return false;
        }
    }
}

template <typename Config, typename Vis, typename T>
bool decode_struct_fields(Vis& vis, T& out) {
    using schema = meta::virtual_schema<T, Config>;
    using slots = typename schema::slots;
    constexpr std::size_t N = type_list_size_v<slots>;

    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (detail::decode_one_field<Config, Is>(vis, out) && ...);
    }(std::make_index_sequence<N>{});
}

}  // namespace kota::codec
