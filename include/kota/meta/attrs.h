#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <optional>
#include <string_view>
#include <type_traits>

#include "name.h"
#include "spec.h"
#include "struct.h"
#include "kota/support/naming.h"
#include "kota/support/tuple_traits.h"
#include "kota/support/type_traits.h"

namespace kota::meta {

template <typename T>
concept annotated_type = requires {
    typename std::remove_cvref_t<T>::annotated_type;
    typename std::remove_cvref_t<T>::attrs;
};

namespace attrs {

// Field-level

/// The value attributes of one annotated field. Tag is the struct generated
/// by KOTATSU_ANNOTATE (or hand-written) whose `spec` static member is a
/// meta::spec_result; only the short tag name appears in template arguments.
template <typename Tag>
struct spec {
    constexpr const static field_spec& value = Tag::spec.value;
};

// Struct-level

/// The value attributes of one annotated struct or variant type. Unlike spec,
/// this attr forks the type_info identity of the annotated type — a renamed
/// or tagged type and its bare form have different serialized schemas.
template <typename Tag>
struct struct_spec {
    constexpr const static meta::struct_spec& value = Tag::spec;
};

}  // namespace attrs

template <typename T>
struct is_spec_attr {
    constexpr static bool value = false;
};

template <typename Tag>
struct is_spec_attr<attrs::spec<Tag>> {
    constexpr static bool value = true;
};

[[maybe_unused]] constexpr inline field_spec empty_field_spec{};

/// The value spec inside an attrs tuple; the empty spec when absent.
template <typename AttrsTuple>
constexpr const inline field_spec& spec_of = [] -> const field_spec& {
    if constexpr(tuple_any_of_v<AttrsTuple, is_spec_attr>) {
        return tuple_find_t<AttrsTuple, is_spec_attr>::value;
    } else {
        return empty_field_spec;
    }
}();

/// The value spec attached to a (possibly annotated) field type.
template <typename T>
constexpr const inline field_spec& field_spec_of = [] -> const field_spec& {
    if constexpr(annotated_type<T>) {
        return spec_of<typename T::attrs>;
    } else {
        return empty_field_spec;
    }
}();

template <typename T>
struct is_struct_spec_attr {
    constexpr static bool value = false;
};

template <typename Tag>
struct is_struct_spec_attr<attrs::struct_spec<Tag>> {
    constexpr static bool value = true;
};

[[maybe_unused]] constexpr inline struct_spec empty_struct_spec{};

/// The struct-level spec inside an attrs tuple; the empty spec when absent.
template <typename AttrsTuple>
constexpr const inline struct_spec& struct_spec_of = [] -> const struct_spec& {
    if constexpr(tuple_any_of_v<AttrsTuple, is_struct_spec_attr>) {
        return tuple_find_t<AttrsTuple, is_struct_spec_attr>::value;
    } else {
        return empty_struct_spec;
    }
}();

namespace detail {

/// The layers of a merged config, one per policy: each declares its member
/// only when its policy is set, so an untouched policy keeps shining through
/// from Base.
template <typename Base, naming::Casing RenameAll>
struct rename_layer : Base {
    using field_rename = naming::rename_policy_t<RenameAll>;
};

template <typename Base>
struct rename_layer<Base, naming::Casing::Identity> : Base {};

template <typename Base, bool DenyUnknown>
struct deny_layer : Base {
    constexpr static bool deny_unknown_fields = true;
};

template <typename Base>
struct deny_layer<Base, false> : Base {};

template <typename Base, bool DefaultedFields>
struct defaulted_layer : Base {
    constexpr static bool defaulted_fields = true;
};

template <typename Base>
struct defaulted_layer<Base, false> : Base {};

/// Base under every policy layer: the one config type a merge makes.
template <typename Base, naming::Casing RenameAll, bool DenyUnknown, bool DefaultedFields>
struct merged_config :
    defaulted_layer<deny_layer<rename_layer<Base, RenameAll>, DenyUnknown>, DefaultedFields> {};

template <typename Base, naming::Casing RenameAll, bool DenyUnknown, bool DefaultedFields>
struct merge_config_impl {
    using type = merged_config<Base, RenameAll, DenyUnknown, DefaultedFields>;
};

template <typename Base>
struct merge_config_impl<Base, naming::Casing::Identity, false, false> {
    using type = Base;
};

/// Merging onto an already-merged base rebuilds a single normalized layer
/// instead of stacking (deeper rename overrides, deny and defaulted are
/// sticky), so equivalent merge chains produce the same config type —
/// type_info instance sharing (and thus one $defs entry per struct) depends
/// on that.
template <typename Base,
          naming::Casing R0,
          bool D0,
          bool F0,
          naming::Casing RenameAll,
          bool DenyUnknown,
          bool DefaultedFields>
struct merge_config_impl<merged_config<Base, R0, D0, F0>, RenameAll, DenyUnknown, DefaultedFields> {
    using type = merged_config<Base,
                               RenameAll != naming::Casing::Identity ? RenameAll : R0,
                               D0 || DenyUnknown,
                               F0 || DefaultedFields>;
};

template <typename Base, naming::Casing R0, bool D0, bool F0>
struct merge_config_impl<merged_config<Base, R0, D0, F0>, naming::Casing::Identity, false, false> {
    using type = merged_config<Base, R0, D0, F0>;
};

}  // namespace detail

/// Base config with an annotated node's rename_all / deny_unknown_fields /
/// defaulted_fields layered on top; a spec carrying none of these policies
/// leaves Base untouched. A deeper merge overrides an earlier rename, deny
/// and defaulted are sticky: once set they are never merged away. This is the
/// single implementation of the merge — the codec dispatch applies it when
/// crossing an annotated reflectable node and meta's repr resolver replays
/// it, so type_info always describes the documents the codec reads and
/// writes.
template <typename Base, typename AttrsTuple>
using merged_config_t =
    typename detail::merge_config_impl<Base,
                                       struct_spec_of<AttrsTuple>.rename_all,
                                       struct_spec_of<AttrsTuple>.deny_unknown_fields,
                                       struct_spec_of<AttrsTuple>.defaulted_fields>::type;

/// The config a node of type T carrying AttrsTuple (a field's attrs, or an
/// annotation's) is read and written under: its rename_all /
/// deny_unknown_fields / defaulted_fields merge onto Config when T is a
/// reflectable struct, the only kind those policies act on, and are inert on
/// every other node.
template <typename Config, typename T, typename AttrsTuple>
using node_config_t =
    std::conditional_t<reflectable_class<T>, merged_config_t<Config, AttrsTuple>, Config>;

/// Resolve serialized names for variant alternatives: the annotation's
/// tag_names when provided (count must match), meta::type_name of each
/// alternative otherwise.
template <typename SpecAttr, typename... Ts>
constexpr auto resolve_tag_names() {
    constexpr const struct_spec& spec = SpecAttr::value;
    if constexpr(spec.tag_names.count != 0) {
        static_assert(spec.tag_names.count == sizeof...(Ts),
                      "tagged: number of custom names must match variant alternatives");
        std::array<std::string_view, sizeof...(Ts)> names{};
        std::ranges::copy_n(spec.tag_names.names().begin(), sizeof...(Ts), names.begin());
        return names;
    } else {
        return std::array<std::string_view, sizeof...(Ts)>{type_name<Ts>()...};
    }
}

namespace behavior {

template <typename Policy>
struct enum_string {
    using policy = Policy;
};

/// Omits a field when Pred holds for its value. A predicate taking
/// (const Value&, bool is_serialize) decides for encoding and decoding
/// alike; one taking only the value speaks for encoding, and a decode never
/// skips on it.
template <typename Pred>
struct skip_if {
    using predicate = Pred;
};

/// Adapter-based serialization. The adapter follows the meta::repr member
/// protocol (see repr.h), bound per-field instead of per-type; a field's
/// `with` adapter takes priority over its type's repr.
template <typename Adapter>
struct with {
    using adapter = Adapter;
};

/// Type conversion: convert to Target type before serializing via default path.
template <typename Target>
struct as {
    using target = Target;
};

}  // namespace behavior

template <typename Pred, typename Value>
constexpr bool evaluate_skip_predicate(const Value& value, bool is_serialize) {
    if constexpr(requires {
                     { Pred{}(value, is_serialize) } -> std::convertible_to<bool>;
                 }) {
        return static_cast<bool>(Pred{}(value, is_serialize));
    } else if constexpr(requires {
                            { Pred{}(value) } -> std::convertible_to<bool>;
                        }) {
        return is_serialize && static_cast<bool>(Pred{}(value));
    } else {
        static_assert(
            dependent_false<Pred>,
            "behavior::skip_if predicate must return bool and accept (const Value&, bool) or (const Value&)");
        return false;
    }
}

namespace pred {

struct optional_none {
    template <typename T>
    constexpr bool operator()(const std::optional<T>& value, bool is_serialize) const {
        return is_serialize && !value.has_value();
    }
};

struct empty {
    template <typename T>
    constexpr bool operator()(const T& value, bool is_serialize) const {
        if constexpr(requires { value.empty(); }) {
            return is_serialize && value.empty();
        } else {
            return false;
        }
    }
};

struct default_value {
    template <typename T>
    constexpr bool operator()(const T& value, bool is_serialize) const {
        if constexpr(requires { value == T(); }) {
            return is_serialize && static_cast<bool>(value == T());
        } else {
            return false;
        }
    }
};

}  // namespace pred

/// Evaluate a built-in skip_when condition against a field value.
template <skip_when When, typename Value>
constexpr bool evaluate_skip_when(const Value& value, bool is_serialize) {
    static_assert(When != skip_when::never);
    if constexpr(When == skip_when::none) {
        return pred::optional_none{}(value, is_serialize);
    } else if constexpr(When == skip_when::empty) {
        return pred::empty{}(value, is_serialize);
    } else {
        return pred::default_value{}(value, is_serialize);
    }
}

/// True for the closed set of behavior attributes.
template <typename T>
constexpr bool is_behavior_attr_v =
    is_specialization_of<behavior::enum_string, T> || is_specialization_of<behavior::skip_if, T> ||
    is_specialization_of<behavior::with, T> || is_specialization_of<behavior::as, T>;

/// True for behavior providers (with/as/enum_string) — at most one per field.
template <typename T>
struct is_behavior_provider {
    constexpr static bool value = is_specialization_of<behavior::with, T> ||
                                  is_specialization_of<behavior::as, T> ||
                                  is_specialization_of<behavior::enum_string, T>;
};

namespace detail {

template <typename AttrsTuple>
constexpr bool validate_attrs() {
    static_assert(tuple_count_of_v<AttrsTuple, is_behavior_provider> <= 1,
                  "At most one behavior provider (with/as/enum_string) allowed per field");
    static_assert(tuple_count_of_v<AttrsTuple, is_spec_attr> <= 1,
                  "At most one annotation spec allowed per field");
    static_assert(tuple_count_of_v<AttrsTuple, is_struct_spec_attr> <= 1,
                  "At most one struct annotation spec allowed per type");
    static_assert(!(spec_of<AttrsTuple>.skip_if != skip_when::never &&
                    tuple_has_spec_v<AttrsTuple, behavior::skip_if>),
                  "A built-in skip_if condition and a custom skip_if predicate conflict");
    return true;
}

}  // namespace detail

}  // namespace kota::meta
