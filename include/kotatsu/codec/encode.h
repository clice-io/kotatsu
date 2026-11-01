#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "context.h"
#include "format.h"
#include "repr.h"
#include "type_info.h"
#include "kotatsu/support/type_list.h"
#include "kotatsu/support/utf8.h"
#include "kotatsu/meta/annotation.h"
#include "kotatsu/meta/enum.h"
#include "kotatsu/meta/struct.h"
#include "kotatsu/meta/type_kind.h"
#include "kotatsu/codec/attribute.h"
#include "kotatsu/codec/repr.h"
#include "kotatsu/codec/type_info.h"

namespace kotatsu::codec {

/// How a visitor writes one type, overridden per backend or protocol:
/// specialize it for one visitor type, or for every visitor a constraint
/// admits, with a static visit(Vis&, const T&) returning bool. The backends
/// use it for their own document types (json raw_value, toml tables,
/// dyn::Value) and protocol layers for types they shape themselves (ipc's
/// Error and Literal). A specialization is consulted before everything
/// else, annotations and codec::repr included, and a type may not have both
/// a specialization and a codec::repr for the visitor's format. To say how a
/// type of your own is represented, prefer codec::repr (or behavior::with on
/// a field): those are visible to schema consumers (type_info, the JSON
/// schema, the fbs views), a specialization is not.
template <typename Vis, typename T>
struct serialize_visit {};

template <typename Format, typename Vis, typename T>
bool encode_value(Vis& vis, const T& value);

template <typename Format, typename Vis, typename T>
bool encode_struct_fields(Vis& vis, const T& value);

namespace detail {

/// Encodes a value as its type writes it, its type's annotation aside: a
/// codec::repr, or what its kind is.
template <typename Format, typename Vis, typename T>
bool encode_plain(Vis& vis, const T& value);

}  // namespace detail

/// True when the visitor writes UTF-8 text (json, toml), which its format tag
/// says, so its value and map key writers alike.
template <typename Vis>
concept writes_utf8 = requires { requires codec::format_of_t<Vis>::utf8; };

namespace detail {

/// Writes text, a string the value holds. A visitor writing UTF-8 text gets
/// it checked first: one that is not UTF-8 fails, or is replaced as a decoder
/// reads it, as the encode's options say. Names the program declares (field
/// and tag names, enumerator spellings) are written as they are.
template <typename Format, typename Vis, typename Text>
bool encode_text(Vis& vis, const Text& text) {
    if constexpr(writes_utf8<Vis>) {
        std::string_view view(text);
        if(!is_utf8(view)) {
            if(current_encode_options().invalid_utf8 == invalid_utf8::Replace) {
                return vis.visit_str(replace_invalid_utf8(view));
            } else {
                return scoped_context<rich_error>::fail(rich_error("invalid UTF-8 in a string"));
            }
        }
    }
    return vis.visit_str(text);
}

/// Fails the encode of an enum value without a reflected member name, which
/// has no string spelling: "" would make a document no decode maps back.
template <typename E>
bool fail_unnamed_enum(E value) {
    // Unary plus keeps a char-sized underlying value a number.
    return scoped_context<rich_error>::fail(
        rich_error(std::format("enum value {} has no reflected name", +std::to_underlying(value))));
}

/// Encodes an enum through an enum_names repr: the name its enumerator is
/// spelled with.
template <typename Repr, typename Format, typename Vis, typename E>
bool encode_enum_name(Vis& vis, const E& value) {
    static_assert(std::is_enum_v<E>, "codec::enum_names is the repr of an enum");
    using policy = codec::detail::enum_names_policy_t<Repr, E, Format>;
    const auto& names = codec::detail::enum_names_of<E, policy>;
    const auto index = meta::enum_index(value);
    if(index == names.size()) {
        return fail_unnamed_enum(value);
    }
    return vis.visit_str(names[index]);
}

/// Encode a value through a representation declaration (a codec::repr
/// specialization or a behavior::with adapter): declarative to() when
/// present, the imperative encode() body otherwise.
template <typename Repr, typename Format, typename Vis, typename V>
bool repr_encode(Vis& vis, const V& value) {
    using declared_t = codec::declared_repr_t<Repr>;
    if constexpr(std::is_same_v<declared_t, codec::dynamic>) {
        static_assert(!layout_computed<Vis>,
                      "this backend computes the output layout statically and cannot encode a "
                      "codec::dynamic repr");
    }
    if constexpr(codec::names_enum<Repr>) {
        return encode_enum_name<Repr, Format>(vis, value);
    } else if constexpr(requires { Repr::to(value); }) {
        static_assert(
            !requires { Repr::encode(vis, value); },
            "repr protocol: define to() or encode() for encoding, not both");
        static_assert(std::is_same_v<std::remove_cvref_t<decltype(Repr::to(value))>, declared_t>,
                      "repr protocol: to() must return the declared representation type");
        return encode_value<Format>(vis, Repr::to(value));
    } else if constexpr(requires { Repr::encode(vis, value); }) {
        return Repr::encode(vis, value);
    } else {
        static_assert(dependent_false<Repr>,
                      "repr protocol: no encode path — define to() or encode()");
        return false;
    }
}

/// Encodes a variant without tags: the alternative index and payload on a
/// backend with native variants, the bare payload otherwise.
template <typename Format, typename Vis, typename... Ts>
bool encode_untagged_variant(Vis& vis, const std::variant<Ts...>& var) {
    if constexpr(requires(Vis& v) {
                     v.visit_variant(std::size_t{}, [](auto&) -> bool { return true; });
                 }) {
        return vis.visit_variant(var.index(), [&](auto& pv) -> bool {
            return std::visit(
                [&](const auto& alt) -> bool { return encode_value<Format>(pv, alt); },
                var);
        });
    } else {
        return std::visit([&](const auto& alt) -> bool { return encode_value<Format>(vis, alt); },
                          var);
    }
}

/// Encodes a variant in one of the three tagged shapes selected by the
/// spec's codec::tag_mode (tag names resolve through resolve_tag_names):
/// - external:  { "TagName": payload }
/// - internal:  { "<tag>": "TagName", ...payload fields } — requires every
///   alternative to be a struct, whose fields are spliced in after the tag,
///   or std::monostate, which writes the tag alone
/// - adjacent:  { "<tag>": "TagName", "<content>": payload }
/// Tagging only applies on human-readable backends; elsewhere the variant is
/// encoded untagged (a binary backend writes the alternative index). Either
/// way the variant is written as itself: a repr of its type does not apply.
template <typename Format, typename SpecAttr, typename Vis, typename Var>
bool encode_tagged_variant(Vis& vis, const Var& var) {
    if constexpr(!is_human_readable<Format, Vis>()) {
        return encode_untagged_variant<Format>(vis, var);
    } else {
        // MSVC mis-handles uncaptured constexpr locals inside the nested
        // generic lambdas below (C3861/ICE), so the spec is read via SpecAttr
        // each time.
        return [&]<typename... Ts>(const std::variant<Ts...>&) -> bool {
            constexpr auto names = codec::resolve_tag_names<SpecAttr, Format, Ts...>();
            std::string_view tag_name = names[var.index()];

            if constexpr(SpecAttr::value.tagging == codec::tag_mode::external) {
                return vis.visit_struct(var, [&](auto& sv) -> bool {
                    return sv.visit_field(named_field{0, tag_name}, [&](auto& pv) -> bool {
                        return std::visit(
                            [&](const auto& alt) -> bool { return encode_value<Format>(pv, alt); },
                            var);
                    });
                });
            } else if constexpr(SpecAttr::value.tagging == codec::tag_mode::internal) {
                return std::visit(
                    [&](const auto& alt) -> bool {
                        using alt_t = std::remove_cvref_t<decltype(alt)>;
                        assert_internal_alternative<Format, alt_t>();
                        static_assert(
                            !requires(Vis& v, const alt_t& value) {
                                serialize_visit<Vis, alt_t>::visit(v, value);
                            },
                            "internally tagged: an alternative the visitor writes with a "
                            "serialize_visit has no fields to write beside the tag");
                        assert_internal_tag_fits<Format, SpecAttr, alt_t>();
                        return vis.visit_struct(alt, [&](auto& sv) -> bool {
                            KOTATSU_CODEC_TRY(sv.visit_field(
                                named_field{0, SpecAttr::value.tag},
                                [&](auto& tv) -> bool { return tv.visit_str(tag_name); }));
                            // A monostate is a unit variant: the tag alone.
                            if constexpr(std::is_same_v<alt_t, std::monostate>) {
                                return true;
                            } else {
                                return encode_struct_fields<Format>(sv, alt);
                            }
                        });
                    },
                    var);
            } else {
                static_assert(SpecAttr::value.tagging == codec::tag_mode::adjacent);
                return vis.visit_struct(var, [&](auto& sv) -> bool {
                    KOTATSU_CODEC_TRY(
                        sv.visit_field(named_field{0, SpecAttr::value.tag},
                                       [&](auto& tv) -> bool { return tv.visit_str(tag_name); }));
                    return sv.visit_field(named_field{1, SpecAttr::value.content},
                                          [&](auto& cv) -> bool {
                                              return std::visit(
                                                  [&](const auto& alt) -> bool {
                                                      return encode_value<Format>(cv, alt);
                                                  },
                                                  var);
                                          });
                });
            }
        }(var);
    }
}

/// Encodes a value under a node's attributes (a struct field's, or an
/// annotation's): behavior::with > behavior::as > variant tagging > the struct-level policy merge
/// (scoped_format_t), the precedence meta's repr resolver replays.
template <typename Format, typename Attrs, typename Vis, typename T>
bool encode_with_attrs(Vis& vis, const T& value) {
    if constexpr(meta::derived_variant<T>) {
        return encode_with_attrs<Format, Attrs>(vis,
                                                static_cast<const meta::variant_base_t<T>&>(value));
    } else if constexpr(tuple_has_spec_v<Attrs, codec::behavior::with>) {
        using adapter = typename tuple_find_spec_t<Attrs, codec::behavior::with>::adapter;
        return repr_encode<adapter, Format>(vis, value);
    } else if constexpr(tuple_has_spec_v<Attrs, codec::behavior::as>) {
        using target = typename tuple_find_spec_t<Attrs, codec::behavior::as>::target;
        target converted(value);
        return encode_value<Format>(vis, converted);
    } else if constexpr(codec::struct_spec_of<Attrs>.tagging != codec::tag_mode::none) {
        static_assert(taggable<T>, "a tagging attribute requires a std::variant");
        using spec_attr = struct_spec_attr_t<Attrs>;
        return encode_tagged_variant<Format, spec_attr>(vis, value);
    } else {
        return encode_plain<codec::node_format_t<Format, T, Attrs>>(vis, value);
    }
}

/// Encode a single struct field, applying behavior transforms if present.
template <typename Format, std::size_t I, typename Vis, typename T>
bool encode_one_field(Vis& vis, const T& value) {
    using descriptor = codec::field<T, I, Format>;

    // A visitor that writes every field has nothing to mark one absent, so
    // presence omits fields only elsewhere.
    if constexpr(!writes_every_field<Vis>) {
        if constexpr(omits_unstated_defaults<Format> && !descriptor::info.schema_default) {
            return true;
        }
        if(descriptor::omitted(value)) {
            return true;
        }
    }
    const auto& field_ref = descriptor::of(value);

    bool ok = vis.visit_field(descriptor{}, [&](auto& fv) -> bool {
        return encode_with_attrs<codec::members_format_t<Format>, typename descriptor::attrs>(
            fv,
            field_ref);
    });
    return trace_path(ok, descriptor::name);
}

}  // namespace detail

template <typename Format, typename Vis, typename T>
bool encode_value(Vis& vis, const T& value) {
    if constexpr(!std::is_same_v<meta::attrs_of_t<T>, std::tuple<>>) {
        return detail::encode_with_attrs<Format, meta::attrs_of_t<T>>(vis, value);
    } else {
        return detail::encode_plain<Format>(vis, value);
    }
}

/// Encodes `value` through `vis` under the format the visitor carries: what a
/// repr's encode() or a serialize_visit specialization calls to write the
/// values it is made of.
template <typename Vis, typename T>
    requires (!std::is_void_v<format_of_t<Vis>>)
bool encode_value(Vis& vis, const T& value) {
    return encode_value<format_of_t<Vis>>(vis, value);
}

namespace detail {

template <typename Format, typename Vis, typename T>
bool encode_plain(Vis& vis, const T& value) {
    if constexpr(requires(Vis& v, const T& val) { serialize_visit<Vis, T>::visit(v, val); }) {
        static_assert(!codec::has_repr<T, Format>,
                      "type has both a serialize_visit specialization and a codec::repr; "
                      "keep exactly one");
        return serialize_visit<Vis, T>::visit(vis, value);
    } else if constexpr(codec::has_repr<T, Format>) {
        return repr_encode<codec::repr_for<T, Format>, Format>(vis, value);
    } else if constexpr(meta::derived_variant<T>) {
        return encode_plain<Format>(vis, static_cast<const meta::variant_base_t<T>&>(value));
    } else {
        constexpr auto kind = meta::kind_of<T>();
        using enum meta::type_kind;

        if constexpr(kind == boolean) {
            return vis.visit_bool(value);
        } else if constexpr(meta::int_like<T>) {
            return vis.visit_int(value);
        } else if constexpr(meta::uint_like<T>) {
            return vis.visit_uint(value);
        } else if constexpr(kind == float32 || kind == float64) {
            return vis.visit_float(value);
        } else if constexpr(meta::str_like<T> && std::is_array_v<T>) {
            // A char array need not end in a null character, so its text
            // stops at the array's end.
            std::string_view text(value, std::extent_v<T>);
            return detail::encode_text<Format>(vis, text.substr(0, text.find('\0')));
        } else if constexpr(meta::str_like<T> && std::is_pointer_v<T>) {
            // A null C string holds no text at all: it writes null, where
            // the visitor can write one; a map key writer cannot.
            if(value == nullptr) {
                if constexpr(requires { vis.visit_null(); }) {
                    return vis.visit_null();
                } else {
                    return scoped_context<rich_error>::fail(rich_error("null C string map key"));
                }
            }
            return detail::encode_text<Format>(vis, value);
        } else if constexpr(meta::str_like<T>) {
            return detail::encode_text<Format>(vis, value);
        } else if constexpr(kind == character) {
            return vis.visit_char(value);
        } else if constexpr(kind == bytes) {
            return vis.visit_bytes(value);
        } else if constexpr(kind == null) {
            return vis.visit_null();
        } else if constexpr(kind == optional || kind == pointer) {
            if constexpr(is_specialization_of<std::weak_ptr, T>) {
                // A weak pointer writes what the shared pointer it locks to
                // would, presence marker included.
                return encode_value<Format>(vis, value.lock());
            } else if constexpr(kind == pointer &&
                                requires(Vis& v, const T& p) { v.visit_pointer(p); }) {
                return vis.visit_pointer(value);
            } else {
                if(value) {
                    if constexpr(requires(Vis& v, int x) {
                                     v.visit_some(x, [](auto&) -> bool { return true; });
                                 }) {
                        return vis.visit_some(*value, [&](auto& sv) -> bool {
                            return encode_value<Format>(sv, *value);
                        });
                    } else {
                        return encode_value<Format>(vis, *value);
                    }
                }
                return vis.visit_null();
            }
        } else if constexpr(kind == enumeration) {
            if constexpr(requires { vis.visit_enum(value); }) {
                return vis.visit_enum(value);
            } else {
                using U = std::underlying_type_t<T>;
                if constexpr(std::is_signed_v<U>) {
                    return vis.visit_int(static_cast<U>(value));
                } else {
                    return vis.visit_uint(static_cast<U>(value));
                }
            }
        } else if constexpr(kind == array || kind == set) {
            return vis.visit_seq(value, [&](auto& sv) -> bool {
                using element_t = std::ranges::range_value_t<T>;
                std::size_t idx = 0;
                for(const auto& elem: value) {
                    bool ok = sv.visit_element([&](auto& ev) -> bool {
                        // vector<bool> iteration yields a proxy reference on
                        // libc++; materialize it as the element type before
                        // dispatch, mirroring the decode path.
                        if constexpr(std::is_same_v<std::remove_cvref_t<decltype(elem)>,
                                                    element_t>) {
                            return encode_value<Format>(ev, elem);
                        } else {
                            return encode_value<Format>(ev, static_cast<element_t>(elem));
                        }
                    });
                    KOTATSU_CODEC_TRY(detail::trace_path(ok, idx));
                    ++idx;
                }
                return true;
            });
        } else if constexpr(kind == tuple) {
            return vis.visit_tuple(value, [&](auto& sv) -> bool {
                return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                    return (detail::trace_path(sv.visit_element([&](auto& ev) -> bool {
                                return encode_value<Format>(ev, std::get<Is>(value));
                            }),
                                               Is) &&
                            ...);
                }(std::make_index_sequence<std::tuple_size_v<T>>{});
            });
        } else if constexpr(kind == map) {
            return vis.visit_map(value, [&](auto& mv) -> bool {
                std::size_t idx = 0;
                for(const auto& [k, v]: value) {
                    bool ok = mv.visit_entry(
                        [&](auto& kv) -> bool { return encode_value<Format>(kv, k); },
                        [&](auto& vv) -> bool { return encode_value<Format>(vv, v); });
                    KOTATSU_CODEC_TRY(detail::trace_path(ok, idx));
                    ++idx;
                }
                return true;
            });
        } else if constexpr(kind == structure) {
            return vis.visit_struct(value, [&](auto& sv) -> bool {
                return encode_struct_fields<Format>(sv, value);
            });
        } else if constexpr(kind == variant) {
            if constexpr(is_expected_v<T>) {
                if(value.has_value()) {
                    if constexpr(!std::is_void_v<typename T::value_type>) {
                        return encode_value<Format>(vis, *value);
                    } else {
                        return vis.visit_null();
                    }
                } else {
                    using E = std::remove_cvref_t<decltype(value.error())>;
                    // An error of a type the schema does not know reaches only
                    // a backend that shows any value.
                    if constexpr(meta::kind_of<E>() != meta::type_kind::unknown ||
                                 requires(Vis& v, const E& e) { v.visit_opaque(e); }) {
                        return encode_value<Format>(vis, value.error());
                    } else {
                        return vis.visit_null();
                    }
                }
            } else {
                return detail::encode_untagged_variant<Format>(vis, value);
            }
        } else if constexpr(std::is_pointer_v<T> &&
                            requires(Vis& v, const T& p) { v.visit_pointer(p); }) {
            return vis.visit_pointer(value);
        } else if constexpr(requires(Vis& v, const T& x) { v.visit_opaque(x); }) {
            // Only a backend that can show any value, like the debug one,
            // takes a type the schema knows nothing about.
            return vis.visit_opaque(value);
        } else {
            static_assert(dependent_false<T>,
                          "cannot serialize this type; specialize serialize_visit to add support");
            return false;
        }
    }
}

}  // namespace detail

template <typename Format, typename Vis, typename T>
bool encode_struct_fields(Vis& vis, const T& value) {
    using schema = codec::virtual_schema<T, Format>;
    using slots = typename schema::slots;
    constexpr std::size_t N = type_list_size_v<slots>;

    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (detail::encode_one_field<Format, Is>(vis, value) && ...);
    }(std::make_index_sequence<N>{});
}

namespace detail {

/// A backend's encode entry point, bar building its visitor and output:
/// encode_value under the format F, with the options given, inside a fresh
/// error context.
template <typename F, typename Vis, typename T>
std::expected<void, rich_error> run_encode(Vis& vis,
                                           const T& value,
                                           const encode_options& options = {}) {
    static_assert(std::derived_from<F, format>, "a format extends codec::format");
    rich_error err;
    scoped_context<rich_error> guard(err);
    scoped_context<const encode_options> given(options);
    if(!encode_value<F>(vis, value)) {
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

/// Encodes a map key into its string form, handing the result to Sink::emit
/// as a string_view that is only valid for the duration of that call.
template <typename Sink, typename Format = void>
struct MapKeyWriter {
    Sink sink;
    using format = Format;

    template <typename T>
    bool visit_str(const T& v) {
        sink.emit(std::string_view(v));
        return true;
    }

    template <typename T>
    bool visit_int(T v) {
        sink.emit(std::to_string(static_cast<std::int64_t>(v)));
        return true;
    }

    template <typename T>
    bool visit_uint(T v) {
        sink.emit(std::to_string(static_cast<std::uint64_t>(v)));
        return true;
    }
};

/// Sink for backends whose native map type is keyed by std::string.
struct StringKeySink {
    std::string& output;

    void emit(std::string_view key) {
        output = key;
    }
};

}  // namespace kotatsu::codec
