#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "context.h"
#include "format.h"
#include "type_info.h"
#include "kotatsu/support/numeric.h"
#include "kotatsu/support/ranges.h"
#include "kotatsu/support/type_list.h"
#include "kotatsu/meta/annotation.h"
#include "kotatsu/meta/enum.h"
#include "kotatsu/meta/struct.h"
#include "kotatsu/meta/type_kind.h"
#include "kotatsu/codec/attribute.h"
#include "kotatsu/codec/repr.h"
#include "kotatsu/codec/type_info.h"

namespace kotatsu::codec {

/// How a visitor reads one type, overridden per backend or protocol; the
/// decode counterpart of serialize_visit (see there), with a static
/// visit(Vis&, T&) returning bool. It is consulted before annotations and
/// codec::repr, and untagged variant probing always tries an alternative that
/// has one, whatever its declared shape.
template <typename Vis, typename T>
struct deserialize_visit {};

template <typename Format, typename Vis, typename T>
bool decode_value(Vis& vis, T& out);

template <typename Format, typename Vis, typename T>
bool decode_struct_fields(Vis& vis, T& out);

namespace detail {

/// Decodes a value as its type reads it, its type's annotation aside: a
/// codec::repr, or what its kind is.
template <typename Format, typename Vis, typename T>
bool decode_plain(Vis& vis, T& out);

}  // namespace detail

template <typename Format, typename TagAttr, typename Vis, typename Var>
bool decode_variant(Vis& vis, Var& var);

namespace detail {

/// Decodes an enum through an enum_names repr: the enumerator whose name, as
/// it is spelled, the document holds exactly.
template <typename Repr, typename Format, typename Vis, typename E>
bool decode_enum_name(Vis& vis, E& out) {
    static_assert(std::is_enum_v<E>, "codec::enum_names is the repr of an enum");
    using policy = codec::detail::enum_names_policy_t<Repr, Format>;
    const auto& names = codec::detail::enum_names_of<E, policy>;
    std::string name;
    KOTATSU_CODEC_TRY(vis.visit_str(name));
    for(std::size_t i = 0; i < names.size(); ++i) {
        if(names[i] == name) {
            out = meta::reflection<E>::member_values[i];
            return true;
        }
    }
    return scoped_context<rich_error>::fail(
        rich_error(std::format("unknown enum value '{}'", name)));
}

/// Decode a value through a representation declaration (a codec::repr
/// specialization or a behavior::with adapter): declarative from() when
/// present, the imperative decode() body otherwise.
template <typename Repr, typename Format, typename Vis, typename V>
bool repr_decode(Vis& vis, V& out) {
    using declared_t = codec::declared_repr_t<Repr>;
    if constexpr(std::is_same_v<declared_t, codec::dynamic>) {
        static_assert(!layout_computed<Vis>,
                      "this backend computes the output layout statically and cannot decode a "
                      "codec::dynamic repr");
    }
    if constexpr(codec::names_enum<Repr>) {
        return decode_enum_name<Repr, Format>(vis, out);
    } else if constexpr(requires { Repr::from(std::declval<declared_t>()); }) {
        static_assert(
            !requires { Repr::decode(vis, out); },
            "repr protocol: define from() or decode() for decoding, not both");
        static_assert(
            requires(declared_t&& d) { out = Repr::from(std::move(d)); },
            "repr protocol: from() must accept the declared representation type and return "
            "the value type");
        auto declared = declared_t();
        KOTATSU_CODEC_TRY(decode_value<Format>(vis, declared));
        out = Repr::from(std::move(declared));
        return true;
    } else if constexpr(requires { Repr::decode(vis, out); }) {
        return Repr::decode(vis, out);
    } else {
        static_assert(dependent_false<Repr>,
                      "repr protocol: no decode path — define from() or decode()");
        return false;
    }
}

/// True when the visitor uses data-driven (push-style) decoding.
template <typename Vis>
concept data_driven = requires { requires Vis::data_driven; };

/// Calls f with std::integral_constant<std::size_t, I> for the I equal to
/// the runtime index, which must be below N, and returns f's result.
template <std::size_t N, typename F>
bool with_index(std::size_t index, F&& f) {
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        bool result = false;
        (void)((Is == index && ((result = f(std::integral_constant<std::size_t, Is>{})), true)) ||
               ...);
        return result;
    }(std::make_index_sequence<N>{});
}

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
template <typename Format, typename Vis, typename... Ts>
bool construct_and_visit(Vis& vis, std::variant<Ts...>& out, std::size_t index) {
    if(index >= sizeof...(Ts)) {
        return scoped_context<rich_error>::fail(
            rich_error(std::format("invalid variant index {}", index)));
    }
    return with_index<sizeof...(Ts)>(index, [&](auto i) {
        constexpr std::size_t I = decltype(i)::value;
        return decode_value<Format>(vis, out.template emplace<I>());
    });
}

/// True when the visitor can tell the kind of the value ahead without
/// consuming it. A reader answers with the kinds a document can hold, as
/// untagged variant probing and dyn::Value decoding expect: null, boolean,
/// int64 (and uint64 for an integer beyond int64), float64, string, array,
/// structure for any object or table, and unknown for anything else (a TOML
/// date, a value it cannot classify). The narrower and container kinds of
/// type_kind (int8, float32, set, map, tuple, ...) are never returned.
template <typename Vis>
concept has_peek_kind = requires(Vis& v) {
    { v.peek_kind() } -> std::same_as<meta::type_kind>;
};

/// The callable has_try_read hands try_read. A named type rather than a
/// lambda: once a named module checks the concept and its BMI is compiled to
/// an object file, clang crashes mangling a lambda declared in a
/// requires-expression (llvm/llvm-project#181933).
template <typename Vis>
struct try_read_callable {
    bool operator()(Vis&) const {
        return true;
    }
};

/// True when the visitor supports speculative read with automatic rollback on failure.
template <typename Vis>
concept has_try_read = requires(Vis& v) {
    { v.try_read(try_read_callable<Vis>{}) } -> std::same_as<bool>;
};

/// Sentinel type: no tag attribute (untagged variant).
struct no_tag {};

/// The callable has_native_variant hands visit_variant, named for the reason
/// try_read_callable is.
struct visit_variant_callable {
    bool operator()(std::size_t, auto&) const {
        return true;
    }
};

/// True when the visitor provides native variant support (bincode/fbs).
template <typename Vis>
concept has_native_variant = requires(Vis& v) { v.visit_variant(visit_variant_callable{}); };

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

/// Decodes a value under a node's attributes (a struct field's, or an
/// annotation's), mirroring encode_with_attrs: behavior::with >
/// behavior::as > variant tagging > the rename_all /
/// deny_unknown_fields / presence_all merge.
template <typename Format, typename Attrs, typename Vis, typename T>
bool decode_with_attrs(Vis& vis, T& out) {
    if constexpr(tuple_has_spec_v<Attrs, codec::behavior::with>) {
        using adapter = typename tuple_find_spec_t<Attrs, codec::behavior::with>::adapter;
        if constexpr(names_enum<adapter>) {
            return repr_decode<adapter, codec::node_format_t<Format, T, Attrs>>(vis, out);
        } else {
            return repr_decode<adapter, Format>(vis, out);
        }
    } else if constexpr(tuple_has_spec_v<Attrs, codec::behavior::as>) {
        using target = typename tuple_find_spec_t<Attrs, codec::behavior::as>::target;
        auto converted = target();
        KOTATSU_CODEC_TRY(decode_value<Format>(vis, converted));
        out = T(std::move(converted));
        return true;
    } else if constexpr(codec::spec_of<codec::struct_spec, Attrs>.tagging !=
                        codec::tag_mode::none) {
        static_assert(taggable<T>, "a tagging attribute requires a std::variant");
        // decode_variant reads the variant as itself, tagged or not; a repr
        // of its type does not apply.
        using spec_attr = meta::spec_attr_t<codec::struct_spec, Attrs>;
        if constexpr(meta::derived_variant<T>) {
            return decode_variant<Format, spec_attr>(vis,
                                                     static_cast<meta::variant_base_t<T>&>(out));
        } else {
            return decode_variant<Format, spec_attr>(vis, out);
        }
    } else {
        return decode_plain<codec::node_format_t<Format, T, Attrs>>(vis, out);
    }
}

/// Runs one step of a data-driven decode: the value under `at`, a key as the
/// document spells it or an element index. `at` goes in front of the path of
/// the error the step fails with, and of every unknown field it reports to
/// sink.
template <typename Format, typename Step, typename F>
bool decode_step(UnknownFields* sink, const Step& at, F&& step) {
    if(!sink) {
        return trace_path(step(), at);
    }
    auto reported = sink->entries.size();
    bool ok = trace_path(step(), at);
    for(auto& entry: std::span(sink->entries).subspan(reported)) {
        prepend_step(entry, at);
    }
    return ok;
}

/// An unknown key, whose value reader reads: fails under Deny, and is
/// otherwise reported to sink. The error is located at the key when the
/// reader knows where it is.
template <typename Reader>
bool
    pass_unknown_field(bool deny, std::string_view key, const Reader& reader, UnknownFields* sink) {
    constexpr bool located = requires(const Reader& r) { r.key_location(); };
    auto unknown = [&] {
        auto error = rich_error::unknown_field(key);
        if constexpr(located) {
            error.location = reader.key_location();
        }
        return error;
    };
    if(deny) {
        return scoped_context<rich_error>::fail(unknown());
    }
    if(sink) {
        sink->entries.push_back(unknown());
    }
    return true;
}

/// Decode a field's value applying behavior transforms, without visit_field
/// wrapping, below key, the name the document gives it. Used by match_field
/// (data-driven path) where the field reader is already provided.
template <typename Format, std::size_t I, typename Vis, typename T>
bool decode_field_value(Vis& vis, T& out, std::string_view key, UnknownFields* sink) {
    using descriptor = codec::field<T, I, Format>;
    auto& field_ref = descriptor::of(out);
    return decode_step<Format>(sink, key, [&] {
        return decode_with_attrs<codec::members_format_t<Format>, typename descriptor::attrs>(
            vis,
            field_ref);
    });
}

/// The slot whose name or alias is key; fields.size() when none is.
inline std::size_t find_field_slot(std::span<const codec::field_info> fields,
                                   std::string_view key) {
    for(std::size_t i = 0; i < fields.size(); ++i) {
        if(fields[i].name == key ||
           std::ranges::find(fields[i].aliases, key) != fields[i].aliases.end()) {
            return i;
        }
    }
    return fields.size();
}

/// Data-driven field matching: decodes the slot key names into out and sets
/// the slot's bit in field_mask, or passes over an unknown key, which fails
/// under deny_unknown, the struct's or the options'.
template <typename Format, typename T, typename Vis>
bool match_field(std::string_view key,
                 Vis& reader,
                 T& out,
                 std::uint64_t& field_mask,
                 UnknownFields* sink,
                 bool deny_unknown = false) {
    using schema = codec::virtual_schema<T, Format>;
    constexpr std::size_t N = type_list_size_v<typename schema::slots>;
    static_assert(N <= 64, "struct field count exceeds field_mask capacity (max 64 fields)");

    std::size_t slot = find_field_slot(schema::fields, key);
    if(slot == N) {
        // The data-driven readers move to the next entry whether or not the
        // callback read this one.
        const bool deny =
            deny_unknown || schema::deny_unknown || current_decode_options().deny_unknown_fields;
        return pass_unknown_field(deny, key, reader, sink);
    }

    field_mask |= std::uint64_t{1} << slot;
    return with_index<N>(slot, [&](auto i) {
        return decode_field_value<Format, decltype(i)::value>(reader, out, key, sink);
    });
}

/// Makes room in out for the elements a counted container's access says
/// are coming, as many as its size_hint and no more than a mebibyte of them:
/// the hint is cut to the bytes left, but an element may take more memory
/// than its bytes, and a container inside each of a forged count's elements
/// would multiply what was reserved.
template <typename Container, typename Access>
void reserve_for(Container& out, const Access& access) {
    if constexpr(requires { out.reserve(access.size_hint()); }) {
        constexpr std::size_t most =
            (std::size_t{1} << 20) / sizeof(std::ranges::range_value_t<Container>);
        out.reserve(std::min(access.size_hint(), most));
    }
}

/// Bit I set when slot I of T under Format is required: its presence is
/// required, so a document must carry it.
template <typename Format, typename T>
constexpr std::uint64_t required_mask = [] {
    constexpr auto& fields = codec::virtual_schema<T, Format>::fields;
    static_assert(fields.size() <= 64,
                  "struct field count exceeds field_mask capacity (max 64 fields)");
    std::uint64_t mask = 0;
    for(std::size_t i = 0; i < fields.size(); ++i) {
        if(fields[i].presence == codec::presence_kind::required) {
            mask |= std::uint64_t{1} << i;
        }
    }
    return mask;
}();

/// After data-driven struct decode, fails on the first required field the
/// input left out.
template <typename Format, typename T>
bool check_required_fields(std::uint64_t field_mask) {
    std::uint64_t missing = required_mask<Format, T> & ~field_mask;
    if(missing == 0) {
        return true;
    }
    return scoped_context<rich_error>::fail(rich_error::missing_field(
        codec::virtual_schema<T, Format>::fields[std::countr_zero(missing)].name));
}

/// External tagged: { "TagName": value }, the value below its tag.
template <typename Format, typename SpecAttr, typename Vis, typename... Ts>
bool decode_externally_tagged(Vis& vis, std::variant<Ts...>& var) {
    constexpr auto names = codec::resolve_tag_names<SpecAttr, Format, Ts...>();
    bool found = false;
    auto* sink = scoped_context<UnknownFields>::try_current();
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
        return decode_step<Format>(sink, key, [&] {
            return construct_and_visit<Format>(fv, var, idx);
        });
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
    KOTATSU_CODEC_TRY(tag.visit_str(name));
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

/// Internal tagged: { "tag": "TagName", ...fields... }, the tag alone for a
/// std::monostate alternative. The tag is looked up before any field is
/// placed, so it may follow them.
template <typename Format, typename SpecAttr, typename Vis, typename... Ts>
bool decode_internally_tagged(Vis& vis, std::variant<Ts...>& var) {
    static_assert(data_driven<Vis> && has_try_read<Vis>,
                  "a tagged variant decodes through a data-driven visitor with try_read");
    constexpr std::string_view tag_key = SpecAttr::value.tag;
    (assert_internal_alternative<Format, Ts>(), ...);
    static_assert(
        (!requires(Vis& v, Ts& value) { deserialize_visit<Vis, Ts>::visit(v, value); } && ...),
        "internally tagged: an alternative the visitor reads with a deserialize_visit has no "
        "fields to read beside the tag");
    assert_internal_tag_fits<Format, SpecAttr, Ts...>();
    constexpr auto names = codec::resolve_tag_names<SpecAttr, Format, Ts...>();
    constexpr std::size_t npos = sizeof...(Ts);
    // The variant's own deny_unknown_fields reaches every alternative's keys.
    constexpr bool variant_denies =
        SpecAttr::value.deny_unknown_fields || format_denies_unknown<Format>();

    std::size_t idx = peek_tag_index(vis, tag_key, names);
    if(idx != npos) {
        with_index<sizeof...(Ts)>(idx, [&](auto i) {
            var.template emplace<decltype(i)::value>();
            return true;
        });
    }

    std::uint64_t field_mask = 0;
    std::size_t tag_count = 0;
    auto* sink = scoped_context<UnknownFields>::try_current();
    bool result = vis.visit_struct([&](std::string_view key, auto& fv) -> bool {
        if(key == tag_key) {
            ++tag_count;
            return idx != npos || fail_unusable_tag(fv);
        }
        if(idx == npos) {
            // Without a usable tag data fields cannot be placed, and need
            // not be: the tag's own entry reports why it is unusable, and
            // an absent tag is reported after the pass.
            return true;
        }
        return with_index<sizeof...(Ts)>(idx, [&](auto i) {
            constexpr std::size_t I = decltype(i)::value;
            using alt_t = std::variant_alternative_t<I, std::variant<Ts...>>;
            if constexpr(std::is_same_v<alt_t, std::monostate>) {
                // A unit variant has no fields: any key but the tag is unknown.
                const bool deny = variant_denies || current_decode_options().deny_unknown_fields;
                return pass_unknown_field(deny, key, fv, sink);
            } else {
                using scope = codec::node_format_t<Format, alt_t, meta::attrs_of_t<alt_t>>;
                return match_field<scope, alt_t>(key,
                                                 fv,
                                                 std::get<I>(var),
                                                 field_mask,
                                                 sink,
                                                 variant_denies);
            }
        });
    });

    if(!result) {
        return false;
    }
    if(idx == npos) {
        return scoped_context<rich_error>::fail(
            rich_error("internally tagged variant: missing tag field"));
    }
    if(tag_count > 1) {
        return scoped_context<rich_error>::fail(
            rich_error("internally tagged variant: duplicate tag field"));
    }
    return with_index<sizeof...(Ts)>(idx, [&](auto i) {
        constexpr std::size_t I = decltype(i)::value;
        using alt_t = std::variant_alternative_t<I, std::variant<Ts...>>;
        if constexpr(std::is_same_v<alt_t, std::monostate>) {
            return true;
        } else {
            using scope = codec::node_format_t<Format, alt_t, meta::attrs_of_t<alt_t>>;
            return check_required_fields<scope, alt_t>(field_mask);
        }
    });
}

/// Adjacent tagged: { "t": "TagName", "c": value }, the value below its
/// content key. Any other key is unknown, denied under deny_unknown_fields.
template <typename Format, typename SpecAttr, typename Vis, typename... Ts>
bool decode_adjacently_tagged(Vis& vis, std::variant<Ts...>& var) {
    static_assert(data_driven<Vis> && has_try_read<Vis>,
                  "a tagged variant decodes through a data-driven visitor with try_read");
    constexpr std::string_view tag_key = SpecAttr::value.tag;
    constexpr std::string_view content_key = SpecAttr::value.content;
    constexpr auto names = codec::resolve_tag_names<SpecAttr, Format, Ts...>();
    constexpr std::size_t npos = sizeof...(Ts);

    std::size_t idx = peek_tag_index(vis, tag_key, names);

    std::size_t tag_count = 0;
    std::size_t content_count = 0;
    auto* sink = scoped_context<UnknownFields>::try_current();

    bool result = vis.visit_struct([&](std::string_view key, auto& fv) -> bool {
        if(key == tag_key) {
            ++tag_count;
            return idx != npos || fail_unusable_tag(fv);
        }
        if(key != content_key) {
            const bool deny = SpecAttr::value.deny_unknown_fields ||
                              format_denies_unknown<Format>() ||
                              current_decode_options().deny_unknown_fields;
            return pass_unknown_field(deny, key, fv, sink);
        }
        // Without a usable tag the content cannot be placed: the tag's own
        // entry reports why, and an absent tag is reported after the pass.
        // A duplicate content entry is reported after the pass too.
        if(++content_count == 1 && idx != npos) {
            return decode_step<Format>(sink, key, [&] {
                return construct_and_visit<Format>(fv, var, idx);
            });
        }
        return true;
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

template <typename Format, typename Vis, typename T>
constexpr bool kind_compatible(meta::type_kind src, bool widen);

template <typename Format, typename Vis, typename T>
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
        return kind_compatible<Format, Vis, typename decltype(wrapped)::type>(src, widen);
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
            return (kind_compatible<Format, Vis, std::variant_alternative_t<Is, T>>(src, widen) ||
                    ...);
        }(std::make_index_sequence<std::variant_size_v<T>>{});
    else if constexpr(target == enumeration)
        return src == string || src == int64 || src == uint64;
    else
        return true;
}

template <typename Format, typename Vis, typename T>
constexpr bool kind_compatible(meta::type_kind src, bool widen) {
    if constexpr(requires(Vis& v, T& out) { deserialize_visit<Vis, T>::visit(v, out); }) {
        // A backend dispatch override decodes however it likes — raw_value's
        // JSON specialization accepts any value — so the type's declared
        // shape says nothing about what it accepts; always probe it.
        return true;
    } else {
        // An alternative arrives as its resolved representation — behavior
        // attrs on an annotation first, then (chained) reprs under the
        // visitor's format — so compatibility is judged against that; a
        // dynamic representation can be anything.
        using resolved_t = codec::resolved_repr_t<T, codec::members_format_t<Format>>;
        if constexpr(std::is_same_v<resolved_t, codec::dynamic>) {
            return true;
        } else if constexpr(codec::resolves_to_tagged_variant<T, codec::members_format_t<Format>> &&
                            Format::human_readable) {
            // A tagging spec survived resolution: the tagged decoders read an
            // object ({tag: value}, {tag, content}, or tag-in-fields), so the
            // alternatives' own kinds never face the input directly. A
            // non-human-readable format instead ignores tagging on decode and
            // reads the underlying variant, so its alternatives' kinds apply
            // through the branch below.
            return src == meta::type_kind::unknown || src == meta::type_kind::structure;
        } else {
            return kind_compatible_impl<Format, Vis, resolved_t>(src, widen);
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
template <typename Format, typename Vis, typename T>
constexpr bool probes_as_untagged_variant() {
    if constexpr(requires(Vis& v, T& out) { deserialize_visit<Vis, T>::visit(v, out); })
        return false;
    else if constexpr(codec::has_repr<T, Format>) {
        using chosen = codec::repr_for<T, Format>;
        if constexpr(
            requires { chosen::from(std::declval<codec::declared_repr_t<chosen>>()); } &&
            !requires(Vis& v, T& out) { chosen::decode(v, out); })
            return probes_as_untagged_variant<Format, Vis, codec::declared_repr_t<chosen>>();
        else
            return false;
    } else if constexpr(is_optional_v<T>)
        return probes_as_untagged_variant<Format, Vis, typename T::value_type>();
    else if constexpr(is_specialization_of<std::unique_ptr, T> ||
                      is_specialization_of<std::shared_ptr, T>)
        return probes_as_untagged_variant<Format, Vis, typename T::element_type>();
    else
        return is_specialization_of<std::variant, T>;
}

template <typename Format, typename Vis, typename... Ts>
bool
    untagged_variant_pass(Vis& vis, std::variant<Ts...>& out, meta::type_kind src_kind, bool widen);

/// Probe one nested-variant alternative at the outer pass level. Null engages
/// the outermost nullable wrapper as usual; any other input flows through the
/// wrappers and declarative repr links into the variant's own pass at the
/// same widen level, so a wrapped or repr'd nested variant cannot run its
/// widening pass during an outer exact pass any more than a bare one can.
/// Reached only for chains probes_as_untagged_variant admitted, so a repr
/// link here always has the declarative from() decode path.
template <typename Format, typename Vis, typename T>
bool nested_alternative_pass(Vis& vis, T& out, meta::type_kind src_kind, bool widen) {
    if constexpr(codec::has_repr<T, Format>) {
        using chosen = codec::repr_for<T, Format>;
        auto declared = codec::declared_repr_t<chosen>();
        KOTATSU_CODEC_TRY(nested_alternative_pass<Format>(vis, declared, src_kind, widen));
        out = chosen::from(std::move(declared));
        return true;
    } else if constexpr(is_specialization_of<std::variant, T>) {
        return untagged_variant_pass<Format>(vis, out, src_kind, widen);
    } else {
        if(src_kind == meta::type_kind::null)
            return decode_value<Format>(vis, out);
        ensure_allocated(out);
        return nested_alternative_pass<Format>(vis, *out, src_kind, widen);
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
template <typename Format, typename Vis, typename... Ts>
bool untagged_variant_pass(Vis& vis,
                           std::variant<Ts...>& out,
                           meta::type_kind src_kind,
                           bool widen) {
    auto* sink = scoped_context<UnknownFields>::try_current();
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) -> bool {
        return (([&] {
                    using alt_t = std::variant_alternative_t<Is, std::variant<Ts...>>;
                    constexpr bool nested = probes_as_untagged_variant<Format, Vis, alt_t>();
                    bool strict = kind_compatible<Format, Vis, alt_t>(src_kind, false);
                    bool admit = widen ? (nested || !strict) &&
                                             kind_compatible<Format, Vis, alt_t>(src_kind, true)
                                       : strict;
                    if(!admit)
                        return false;
                    auto reported = sink ? sink->entries.size() : 0;
                    bool claimed = vis.try_read([&](auto& fork) -> bool {
                        if constexpr(nested) {
                            return nested_alternative_pass<Format>(fork,
                                                                   out.template emplace<Is>(),
                                                                   src_kind,
                                                                   widen);
                        } else {
                            return construct_and_visit<Format>(fork, out, Is);
                        }
                    });
                    // A probe that fails takes back the unknown fields it
                    // reported: the value was not read as this alternative.
                    if(!claimed && sink) {
                        sink->entries.resize(reported);
                    }
                    return claimed;
                }()) ||
                ...);
    }(std::index_sequence_for<Ts...>{});
}

template <typename Format, typename Vis, typename... Ts>
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
    return untagged_variant_pass<Format>(vis, out, src_kind, false) ||
           untagged_variant_pass<Format>(vis, out, src_kind, true) ||
           construct_and_visit<Format>(vis, out, last);
}

/// Decode a single struct field, applying behavior transforms if present.
/// Mirrors encode_one_field: wraps the decode in vis.visit_field.
template <typename Format, std::size_t I, typename Vis, typename T>
bool decode_one_field(Vis& vis, T& out) {
    using descriptor = codec::field<T, I, Format>;
    auto& field_ref = descriptor::of(out);
    bool ok = vis.visit_field(descriptor{}, [&](auto& fv) -> bool {
        return decode_with_attrs<codec::members_format_t<Format>, typename descriptor::attrs>(
            fv,
            field_ref);
    });
    return trace_path(ok, descriptor::name);
}

}  // namespace detail

/// Unified variant decode: dispatches to native, tagged, or untagged path.
template <typename Format, typename SpecAttr, typename Vis, typename Var>
bool decode_variant(Vis& vis, Var& var) {
    return [&]<typename... Ts>(std::variant<Ts...>&) -> bool {
        if constexpr(detail::has_native_variant<Vis>) {
            return vis.visit_variant([&](std::size_t index, auto& pv) -> bool {
                return detail::construct_and_visit<Format>(pv, var, index);
            });
        } else if constexpr(!std::is_same_v<SpecAttr, detail::no_tag>) {
            if constexpr(!Format::human_readable) {
                return detail::decode_untagged_variant<Format>(vis, var);
            } else {
                constexpr auto tagging = SpecAttr::value.tagging;
                if constexpr(tagging == codec::tag_mode::external) {
                    return detail::decode_externally_tagged<Format, SpecAttr>(vis, var);
                } else if constexpr(tagging == codec::tag_mode::internal) {
                    return detail::decode_internally_tagged<Format, SpecAttr>(vis, var);
                } else {
                    static_assert(tagging == codec::tag_mode::adjacent);
                    return detail::decode_adjacently_tagged<Format, SpecAttr>(vis, var);
                }
            }
        } else {
            return detail::decode_untagged_variant<Format>(vis, var);
        }
    }(var);
}

template <typename Format, typename Vis, typename T>
bool decode_value(Vis& vis, T& out) {
    using V = std::remove_const_t<T>;

    if constexpr(!std::is_same_v<meta::attrs_of_t<V>, std::tuple<>>) {
        return detail::decode_with_attrs<Format, meta::attrs_of_t<V>>(vis, out);
    } else {
        return detail::decode_plain<Format>(vis, out);
    }
}

/// Decodes `out` through `vis` under the format the visitor carries: what a
/// repr's decode() or a deserialize_visit specialization calls to read the
/// values it is made of.
template <typename Vis, typename T>
    requires (!std::is_void_v<format_of_t<Vis>>)
bool decode_value(Vis& vis, T& out) {
    return decode_value<format_of_t<Vis>>(vis, out);
}

namespace detail {

template <typename Format, typename Vis, typename T>
bool decode_plain(Vis& vis, T& out) {
    using V = std::remove_const_t<T>;

    if constexpr(requires(Vis& v, V& val) { deserialize_visit<Vis, V>::visit(v, val); }) {
        static_assert(!codec::has_repr<V, Format>,
                      "type has both a deserialize_visit specialization and a codec::repr; "
                      "keep exactly one");
        return deserialize_visit<Vis, V>::visit(vis, out);
    } else if constexpr(codec::has_repr<V, Format>) {
        return repr_decode<codec::repr_for<V, Format>, Format>(vis, out);
    } else if constexpr(meta::derived_variant<V>) {
        return decode_plain<Format>(vis, static_cast<meta::variant_base_t<V>&>(out));
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
                    return decode_value<Format>(sv, *out);
                });
            } else {
                if(vis.peek_null()) {
                    out = V{};
                    return vis.visit_null();
                }
                detail::ensure_allocated(out);
                return decode_value<Format>(vis, *out);
            }
        } else if constexpr(kind == enumeration) {
            if constexpr(requires { vis.visit_enum(out); }) {
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
                auto* sink = scoped_context<UnknownFields>::try_current();
                bool result = vis.visit_struct([&](std::string_view key, auto& fv) -> bool {
                    return detail::match_field<Format, V>(key, fv, out, field_mask, sink);
                });
                if constexpr(!presence_checked<Vis>) {
                    if(result) {
                        result = detail::check_required_fields<Format, V>(field_mask);
                    }
                }
                return result;
            } else {
                return vis.visit_struct(out, [&](auto& sv) -> bool {
                    return decode_struct_fields<Format>(sv, out);
                });
            }
        } else if constexpr(kind == array || kind == set) {
            using element_t = std::ranges::range_value_t<V>;
            if constexpr(detail::data_driven<Vis>) {
                if constexpr(requires { out.clear(); }) {
                    out.clear();
                }
                std::size_t idx = 0;
                auto* sink = scoped_context<UnknownFields>::try_current();
                return vis.visit_seq([&](auto& ev) -> bool {
                    auto decode = [&](element_t& item) {
                        return detail::decode_step<Format>(sink, idx, [&] {
                            return decode_value<Format>(ev, item);
                        });
                    };
                    // An element of a container whose emplace_back gives it
                    // back (a vector, a deque, a list) is decoded where it
                    // ends up, rather than moved there; one that fails is
                    // taken back.
                    if constexpr(requires {
                                     { out.emplace_back() } -> std::same_as<element_t&>;
                                     out.pop_back();
                                 }) {
                        if(!decode(out.emplace_back())) {
                            out.pop_back();
                            return false;
                        }
                    } else {
                        auto item = element_t();
                        KOTATSU_CODEC_TRY(decode(item));
                        kotatsu::detail::append_sequence_element(out, std::move(item));
                    }
                    ++idx;
                    return true;
                });
            } else {
                return vis.visit_seq(out, [&](auto& sv) -> bool {
                    if constexpr(requires { out.clear(); }) {
                        out.clear();
                    }
                    detail::reserve_for(out, sv);
                    std::size_t idx = 0;
                    // Here an element is decoded apart and moved in, which
                    // measures faster than in place once out has made room.
                    while(sv.has_element()) {
                        auto item = element_t();
                        bool ok = sv.visit_element(
                            [&](auto& ev) -> bool { return decode_value<Format>(ev, item); });
                        KOTATSU_CODEC_TRY(detail::trace_path(ok, idx));
                        kotatsu::detail::append_sequence_element(out, std::move(item));
                        ++idx;
                    }
                    return true;
                });
            }
        } else if constexpr(kind == tuple) {
            if constexpr(detail::data_driven<Vis>) {
                constexpr std::size_t expected = std::tuple_size_v<V>;
                std::size_t idx = 0;
                auto* sink = scoped_context<UnknownFields>::try_current();
                bool seq_ok = vis.visit_tuple([&](auto& ev) -> bool {
                    if(idx == expected) {
                        return scoped_context<rich_error>::fail(rich_error(
                            std::format("too many elements for tuple (expected {})", expected)));
                    }
                    KOTATSU_CODEC_TRY(detail::decode_step<Format>(sink, idx, [&] {
                        return detail::with_index<std::tuple_size_v<V>>(idx, [&](auto i) {
                            return decode_value<Format>(ev, std::get<decltype(i)::value>(out));
                        });
                    }));
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
                        return (detail::trace_path(sv.visit_element([&](auto& ev) -> bool {
                                    return decode_value<Format>(ev, std::get<Is>(out));
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
                auto* sink = scoped_context<UnknownFields>::try_current();
                // An entry's path names it by its key, as the document spells it.
                return vis.visit_map([&](auto& kv, auto& vv) -> bool {
                    const std::string_view text = kv.str;
                    auto key = key_t();
                    KOTATSU_CODEC_TRY(detail::trace_path(decode_value<Format>(kv, key), text));
                    auto val = mapped_t();
                    KOTATSU_CODEC_TRY(detail::decode_step<Format>(sink, text, [&] {
                        return decode_value<Format>(vv, val);
                    }));
                    kotatsu::detail::insert_map_entry(out, std::move(key), std::move(val));
                    return true;
                });
            } else {
                return vis.visit_map(out, [&](auto& sv) -> bool {
                    if constexpr(requires { out.clear(); }) {
                        out.clear();
                    }
                    detail::reserve_for(out, sv);
                    std::size_t idx = 0;
                    while(sv.has_entry()) {
                        auto key = key_t();
                        auto val = mapped_t();
                        bool ok = sv.visit_entry(
                            [&](auto& kv) -> bool { return decode_value<Format>(kv, key); },
                            [&](auto& vv) -> bool { return decode_value<Format>(vv, val); });
                        KOTATSU_CODEC_TRY(detail::trace_path(ok, idx));
                        kotatsu::detail::insert_map_entry(out, std::move(key), std::move(val));
                        ++idx;
                    }
                    return true;
                });
            }
        } else if constexpr(kind == variant) {
            return decode_variant<Format, detail::no_tag>(vis, out);
        } else {
            static_assert(
                dependent_false<V>,
                "cannot deserialize this type; specialize deserialize_visit to add support");
            return false;
        }
    }
}

}  // namespace detail

template <typename Format, typename Vis, typename T>
bool decode_struct_fields(Vis& vis, T& out) {
    using schema = codec::virtual_schema<T, Format>;
    using slots = typename schema::slots;
    constexpr std::size_t N = type_list_size_v<slots>;

    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (detail::decode_one_field<Format, Is>(vis, out) && ...);
    }(std::make_index_sequence<N>{});
}

namespace detail {

/// A backend's decode entry point, bar building its visitor: decode_value
/// under the format F, with the options given, inside a fresh error context.
template <typename F, typename Vis, typename T>
std::expected<void, rich_error> run_decode(Vis& vis, T& out, const decode_options& options = {}) {
    static_assert(std::derived_from<F, format>, "a format extends codec::format");
    rich_error err;
    scoped_context<rich_error> guard(err);
    scoped_context<const decode_options> given(options);
    if(!decode_value<F>(vis, out)) {
        return std::unexpected(std::move(err));
    }
    return {};
}

}  // namespace detail

// Map keys for backends whose native maps take only string keys (json, toml,
// dyn): integer keys are rendered as their decimal string on encode and
// parsed back on decode. Format is the backend's format tag — void for a
// format-agnostic backend — so format-scoped codec::repr specializations
// resolve for key types the same way they do for values.

/// Decodes a map key from its string form.
template <typename Format = void>
struct MapKeyReader {
    std::string_view str;
    using format = Format;

    template <typename T>
    bool visit_str(T& out) {
        out = T(str);
        return true;
    }

    template <typename T>
    bool visit_int(T& out) {
        return parse<std::int64_t>("integer", out);
    }

    template <typename T>
    bool visit_uint(T& out) {
        return parse<std::uint64_t>("unsigned integer", out);
    }

private:
    template <typename Wide, typename T>
    bool parse(std::string_view target, T& out) {
        Wide v = 0;
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), v);
        if(ec != std::errc{} || ptr != str.data() + str.size()) {
            return scoped_context<rich_error>::fail(
                rich_error(std::format("cannot parse map key '{}' as {}", str, target)));
        }
        if(!kotatsu::narrow_int(v, out)) {
            return scoped_context<rich_error>::fail(
                rich_error(std::format("map key '{}' out of {} range", str, target)));
        }
        return true;
    }
};

}  // namespace kotatsu::codec
