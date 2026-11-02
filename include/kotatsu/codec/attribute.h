#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "kotatsu/support/config.h"
#include "kotatsu/support/naming.h"
#include "kotatsu/support/numeric.h"
#include "kotatsu/meta/annotation.h"
#include "kotatsu/meta/struct.h"
#include "kotatsu/meta/type_kind.h"

/// The attributes the codec reads from annotations, the specs they fold into, and what a
/// field's or a struct's attrs say: its name, its presence, how its value is represented, how
/// a variant is tagged.
namespace kotatsu::codec {

/// Whether a field may be left out of a document, and when a write leaves it
/// out. A field's default is the value it has in a value-initialized object
/// of its struct, its default member initializer included. A document
/// without the field leaves it as it is, which in an object read afresh is
/// its default, so a field written only when it is not its default reads
/// back as it was.
enum class presence_kind : std::uint8_t {
    /// Always written; a document without it fails to read. What a field is
    /// unless something says otherwise, but a std::optional or a pointer.
    required,
    /// Always written; a document may leave it out. What a std::optional or a
    /// pointer is unless something says otherwise.
    optional,
    /// Written only when it is not its default, the field of a
    /// value-initialized owner, compared with meta::eq (==, or member by
    /// member); a document may leave it out.
    omit_if_default,
    /// Written only when a predicate does not hold for it
    /// (`presence = type<Pred>`); a document may leave it out.
    omit_if,
};

/// A number a schema states, exact whatever its type: an integer of either
/// sign, or a floating-point value; monostate when unset.
using schema_number = std::variant<std::monostate, std::int64_t, std::uint64_t, double>;

/// a <=> b exactly, whatever their types; unordered when either is unset or
/// NaN.
constexpr std::partial_ordering compare_numbers(const schema_number& a, const schema_number& b) {
    return std::visit(
        []<typename X, typename Y>(X x, Y y) -> std::partial_ordering {
            if constexpr(std::same_as<X, std::monostate> || std::same_as<Y, std::monostate>) {
                return std::partial_ordering::unordered;
            } else if constexpr(std::same_as<X, double> && std::same_as<Y, double>) {
                return x <=> y;
            } else if constexpr(std::same_as<Y, double>) {
                return compare_exact(x, y);
            } else if constexpr(std::same_as<X, double>) {
                // y against x, turned around.
                return 0 <=> compare_exact(y, x);
            } else {
                return std::cmp_less(x, y)      ? std::partial_ordering::less
                       : std::cmp_greater(x, y) ? std::partial_ordering::greater
                                                : std::partial_ordering::equivalent;
            }
        },
        a,
        b);
}

/// What the attributes of one annotation say about a field, or about every field of the type
/// a type annotation annotates.
struct field_spec {
    /// Serialized name replacing the reflected field name; empty means unset.
    std::string_view rename = {};
    /// Documentation exported by schema backends; transparent in serialized output.
    std::string_view description = {};
    /// Extra accepted names during deserialization.
    meta::name_list alias = {};
    /// Whether the field may be left out of a document, and when a write
    /// leaves it out; unset, the struct's presence_all, the format's, or what
    /// its kind is, decides.
    std::optional<presence_kind> presence = {};
    /// Exclude the field from (de)serialization entirely.
    bool skip = false;
    /// Inline the fields of a nested struct into the parent.
    bool flatten = false;

    // What a schema states beyond the type, for its readers: the decoder
    // checks none of it.

    /// Whether a schema states the field's default value; false for one that
    /// differs between machines or runs. Whether the field is required does
    /// not change.
    bool schema_default = true;
    /// The least and the greatest value of a number field, beside the bounds
    /// of its type.
    schema_number minimum = {};
    schema_number maximum = {};
    /// The values a string field takes.
    meta::name_list choices = {};

    constexpr void validate() const {
        if(skip && flatten) {
            KOTATSU_THROW("annotation: skip and flatten conflict");
        }
        for(std::size_t i = 0; i < alias.size(); ++i) {
            if(alias[i].empty()) {
                KOTATSU_THROW("annotation: empty alias name");
            }
            if(alias[i] == rename) {
                KOTATSU_THROW("annotation: alias duplicates the rename name");
            }
            for(std::size_t j = i + 1; j < alias.size(); ++j) {
                if(alias[i] == alias[j]) {
                    KOTATSU_THROW("annotation: duplicate alias name");
                }
            }
        }
        if(compare_numbers(minimum, maximum) == std::partial_ordering::greater) {
            KOTATSU_THROW("annotation: minimum exceeds maximum");
        }
        // JSON has no number for NaN or an infinity; an unbounded side is left
        // unstated.
        constexpr double largest = std::numeric_limits<double>::max();
        for(auto bound: {minimum, maximum}) {
            if(auto* number = std::get_if<double>(&bound);
               number && !(*number >= -largest && *number <= largest)) {
                KOTATSU_THROW("annotation: a bound that is not finite");
            }
        }
        for(std::size_t i = 0; i < choices.size(); ++i) {
            for(std::size_t j = i + 1; j < choices.size(); ++j) {
                if(choices[i] == choices[j]) {
                    KOTATSU_THROW("annotation: duplicate choice");
                }
            }
        }
    }
};

/// How a variant is tagged in serialized form.
enum class tag_mode : std::uint8_t {
    none,
    /// { "TagName": value }
    external,
    /// { "tag": "TagName", ...fields... }
    internal,
    /// { "tag": "TagName", "content": value }
    adjacent,
};

/// What the attributes of one annotation say about a struct's members or a variant's
/// alternatives, on the type or on the field holding it. Unlike field_spec, these values fork
/// the type_info identity of the annotated type (a renamed struct and its bare form have
/// different serialized schemas).
struct struct_spec {
    /// How the struct's fields, the enum's enumerators or the variant's default tags are
    /// spelled, as a rename policy's function; unset, as the format says, so that Identity
    /// keeps them as declared under a format that renames.
    std::optional<naming::rename_fn> rename_all = {};
    /// Reject unknown keys during deserialization.
    bool deny_unknown_fields = false;
    /// The presence of every field the field's own annotation leaves unset.
    std::optional<presence_kind> presence_all = {};
    /// Variant tagging mode; derived from tagged/tag/content when the spec is built.
    tag_mode tagging = tag_mode::none;
    /// Tag field name (internal and adjacent tagging).
    std::string_view tag = {};
    /// Content field name (adjacent tagging).
    std::string_view content = {};
    /// Custom serialized names for the variant alternatives, in declaration order;
    /// empty means the name of each alternative's type.
    meta::name_list tag_names = {};

    /// Derives the tagging mode from the tag and content field names, and checks the
    /// combination.
    constexpr void validate() {
        if(tagging == tag_mode::external && !tag.empty()) {
            KOTATSU_THROW("annotation: tagged = true conflicts with a tag field name");
        }
        if(!content.empty()) {
            if(tag.empty()) {
                KOTATSU_THROW("annotation: content requires a tag field name");
            }
            if(content == tag) {
                KOTATSU_THROW("annotation: tag and content field names must differ");
            }
            tagging = tag_mode::adjacent;
        } else if(!tag.empty()) {
            tagging = tag_mode::internal;
        }

        if(presence_all == presence_kind::omit_if) {
            KOTATSU_THROW("annotation: presence_all takes no predicate");
        }

        if(!tag_names.empty() && tagging == tag_mode::none) {
            KOTATSU_THROW("annotation: tag_names requires a tagging mode");
        }
        for(std::size_t i = 0; i < tag_names.size(); ++i) {
            if(tag_names[i].empty()) {
                KOTATSU_THROW("annotation: empty tag name");
            }
            for(std::size_t j = i + 1; j < tag_names.size(); ++j) {
                if(tag_names[i] == tag_names[j]) {
                    KOTATSU_THROW("annotation: duplicate tag name");
                }
            }
        }
    }
};

/// The codec's attributes, which tell them apart.
enum class attribute : std::uint8_t {
    // field_spec
    rename,
    description,
    alias,
    skip,
    flatten,
    presence,
    /// as and with: how the value is represented.
    representation,
    schema_default,
    minimum,
    maximum,
    choices,
    // struct_spec
    rename_all,
    deny_unknown_fields,
    presence_all,
    tagged,
    tag,
    content,
    tag_names,
};

}  // namespace kotatsu::codec

template <>
struct kotatsu::meta::attribute_traits<kotatsu::codec::attribute> {
    using attribute = codec::attribute;

    /// tagged, tag and content together pick one tagging mode, which an outer annotation
    /// replaces as a whole.
    constexpr static int group(attribute kind) {
        return kind == attribute::tagged || kind == attribute::tag || kind == attribute::content
                   ? 1
                   : 0;
    }

    /// What a field alone has: its name, whether it is skipped or flattened, how its value is
    /// represented, its schema bounds. A type annotation states the rest as the default of
    /// every field of that type.
    constexpr static bool field_only(attribute kind) {
        switch(kind) {
            case attribute::rename:
            case attribute::alias:
            case attribute::skip:
            case attribute::flatten:
            case attribute::representation:
            case attribute::schema_default:
            case attribute::minimum:
            case attribute::maximum:
            case attribute::choices: return true;
            default: return false;
        }
    }
};

namespace kotatsu::codec {

namespace behavior {

/// Leaves a field out of what a write produces when Pred, called with the
/// field's value, returns true (`presence = type<Pred>`).
template <typename Pred>
struct omit_if {
    using predicate = Pred;
};

/// Adapter-based serialization. The adapter follows the repr member
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

namespace dsl {

using meta::dsl::type;

/// `tagged = true` selects external tagging; internal and adjacent tagging are
/// selected by naming the tag (and content) fields instead.
struct tagged_component {
    bool value;

    using spec_type = struct_spec;
    constexpr static attribute kind = attribute::tagged;

    constexpr void apply(struct_spec& spec) const {
        if(value) {
            spec.tagging = tag_mode::external;
        }
    }
};

/// `as = type<Target>` or `with = type<Adapter>`: how the value is represented, the behavior
/// attr `Attr` says.
template <typename Attr>
struct representation_component {
    using spec_type = field_spec;
    using attr = Attr;
    constexpr static attribute kind = attribute::representation;

    constexpr void apply(field_spec&) const {}
};

/// `presence = type<Pred>`: the field is left out when Pred holds for it.
template <typename Pred>
struct omit_if_component {
    using spec_type = field_spec;
    using attr = behavior::omit_if<Pred>;
    constexpr static attribute kind = attribute::presence;

    constexpr void apply(field_spec& spec) const {
        spec.presence = presence_kind::omit_if;
    }
};

/// `minimum = 1`: any integer or floating-point value, kept exactly.
template <attribute A, auto Member>
struct number_proxy {
    template <typename T>
        requires (std::is_arithmetic_v<T> && !std::same_as<T, bool>)
    constexpr auto operator=(T value) const {
        schema_number number;
        if constexpr(std::is_floating_point_v<T>) {
            number = static_cast<double>(value);
        } else if constexpr(std::is_signed_v<T>) {
            number = static_cast<std::int64_t>(value);
        } else {
            number = static_cast<std::uint64_t>(value);
        }
        return meta::dsl::value_component<A, Member, schema_number>{number};
    }
};

struct tagged_proxy {
    constexpr auto operator=(bool value) const {
        return tagged_component{value};
    }
};

/// `presence` takes a presence_kind or, for `omit_if`, the predicate type; the
/// two forms are one attribute.
struct presence_proxy {
    constexpr auto operator=(presence_kind kind) const {
        return meta::dsl::value_component<attribute::presence,
                                          &field_spec::presence,
                                          std::optional<presence_kind>>{kind};
    }

    template <typename Pred>
    constexpr auto operator=(std::type_identity<Pred>) const {
        return omit_if_component<Pred>{};
    }
};

/// `rename_all` takes a Casing or a rename policy type, `type<Policy>`; the two forms are one
/// attribute.
struct rename_all_proxy {
    constexpr auto operator=(naming::Casing casing) const {
        return meta::dsl::value_component<attribute::rename_all,
                                          &struct_spec::rename_all,
                                          std::optional<naming::rename_fn>>{
            naming::casing_fn(casing)};
    }

    template <naming::renames Policy>
    constexpr auto operator=(std::type_identity<Policy>) const {
        return meta::dsl::value_component<attribute::rename_all,
                                          &struct_spec::rename_all,
                                          std::optional<naming::rename_fn>>{
            &naming::spell_with<Policy>};
    }
};

template <template <typename> typename Behavior>
struct representation_proxy {
    template <typename T>
    constexpr auto operator=(std::type_identity<T>) const {
        return representation_component<Behavior<T>>{};
    }
};

[[maybe_unused]] constexpr inline meta::dsl::
    value_proxy<attribute::rename, &field_spec::rename, std::string_view> rename{};
[[maybe_unused]] constexpr inline meta::dsl::
    value_proxy<attribute::description, &field_spec::description, std::string_view> description{};
[[maybe_unused]] constexpr inline meta::dsl::value_proxy<attribute::skip, &field_spec::skip, bool>
    skip{};
[[maybe_unused]] constexpr inline meta::dsl::
    value_proxy<attribute::flatten, &field_spec::flatten, bool> flatten{};
[[maybe_unused]] constexpr inline meta::dsl::name_list_proxy<attribute::alias, &field_spec::alias>
    alias{};
[[maybe_unused]] constexpr inline presence_proxy presence{};
[[maybe_unused]] constexpr inline representation_proxy<behavior::as> as{};
[[maybe_unused]] constexpr inline representation_proxy<behavior::with> with{};
[[maybe_unused]] constexpr inline meta::dsl::
    value_proxy<attribute::schema_default, &field_spec::schema_default, bool> schema_default{};
[[maybe_unused]] constexpr inline number_proxy<attribute::minimum, &field_spec::minimum> minimum{};
[[maybe_unused]] constexpr inline number_proxy<attribute::maximum, &field_spec::maximum> maximum{};
[[maybe_unused]] constexpr inline meta::dsl::name_list_proxy<attribute::choices,
                                                             &field_spec::choices> choices{};

[[maybe_unused]] constexpr inline rename_all_proxy rename_all{};
[[maybe_unused]] constexpr inline meta::dsl::value_proxy<attribute::deny_unknown_fields,
                                                         &struct_spec::deny_unknown_fields,
                                                         bool> deny_unknown_fields{};
[[maybe_unused]] constexpr inline meta::dsl::
    value_proxy<attribute::presence_all, &struct_spec::presence_all, std::optional<presence_kind>>
        presence_all{};
[[maybe_unused]] constexpr inline tagged_proxy tagged{};
[[maybe_unused]] constexpr inline meta::dsl::
    value_proxy<attribute::tag, &struct_spec::tag, std::string_view> tag{};
[[maybe_unused]] constexpr inline meta::dsl::
    value_proxy<attribute::content, &struct_spec::content, std::string_view> content{};
[[maybe_unused]] constexpr inline meta::dsl::name_list_proxy<attribute::tag_names,
                                                             &struct_spec::tag_names> tag_names{};

}  // namespace dsl

namespace detail {

/// What a spec says when no attribute sets it.
template <typename Spec>
constexpr inline Spec unset_spec{};

}  // namespace detail

/// The spec of type Spec, field_spec or struct_spec, an attrs tuple carries; one no attribute
/// set when it carries none.
template <typename Spec, typename Attrs>
constexpr const inline Spec& spec_of = [] -> const Spec& {
    if constexpr(std::is_void_v<meta::spec_attr_t<Spec, Attrs>>) {
        return detail::unset_spec<Spec>;
    } else {
        return meta::spec_attr_t<Spec, Attrs>::value;
    }
}();

/// The field spec field I of T carries, its type's annotation included (meta::field_attrs_t).
template <typename T, std::size_t I>
constexpr const inline field_spec& field_spec_of = spec_of<field_spec, meta::field_attrs_t<T, I>>;

namespace detail {

/// A presence_all as a template argument: 0 when unset, else the presence plus one.
constexpr std::uint8_t presence_code(std::optional<presence_kind> presence) {
    return presence ? static_cast<std::uint8_t>(std::to_underlying(*presence) + 1) : 0;
}

// The layers of a scoped format, one per struct attribute: each declares its
// member only when the attribute is set, so that Base's shows through
// otherwise.

template <typename Base, typename Rename>
struct rename_layer : Base {
    using rename_all = Rename;
};

template <typename Base>
struct rename_layer<Base, void> : Base {};

template <typename Base, bool DenyUnknown>
struct deny_layer : Base {
    constexpr static bool deny_unknown_fields = true;
};

template <typename Base>
struct deny_layer<Base, false> : Base {};

template <typename Base, std::uint8_t Presence>
struct presence_layer : Base {
    constexpr static std::optional<presence_kind> presence_all =
        static_cast<presence_kind>(Presence - 1);
};

template <typename Base>
struct presence_layer<Base, 0> : Base {};

/// The format a struct's own attributes make of Base, the format around the struct: Base
/// under a layer for each of them. Base stays the format the struct's members are read and
/// written under (members_format_t), and the one whose reprs apply.
template <typename Base, typename Rename, bool DenyUnknown, std::uint8_t Presence>
struct scoped_format :
    presence_layer<deny_layer<rename_layer<Base, Rename>, DenyUnknown>, Presence> {
    using base = Base;
};

/// The scope of a struct whose attributes say Rename, DenyUnknown and Presence, under
/// Base: Base scoped by those that differ from what Base says, or Base itself when none does,
/// so that a struct whose annotation restates the format shares the format's type_info, and
/// its $def.
template <typename Base, typename Rename, bool DenyUnknown, std::uint8_t Presence>
struct scope {
    using rename = std::conditional_t<
        std::is_same_v<Rename,
                       naming::rename_policy_of_t<&naming::spell_with<typename Base::rename_all>>>,
        void,
        Rename>;
    constexpr static std::uint8_t presence =
        Presence == presence_code(Base::presence_all) ? 0 : Presence;
    using type = std::conditional_t<std::is_void_v<rename> && !DenyUnknown && presence == 0,
                                    Base,
                                    scoped_format<Base, rename, DenyUnknown, presence>>;
};

/// A scope entered within another, as the type a repr declares is within its type's: one
/// scope of the outer one's Base, the inner rename or presence over the outer, deny_unknown
/// kept once either sets it, so that equal scopes are one type.
template <typename Base,
          typename R0,
          bool D0,
          std::uint8_t P0,
          typename Rename,
          bool DenyUnknown,
          std::uint8_t Presence>
struct scope<scoped_format<Base, R0, D0, P0>, Rename, DenyUnknown, Presence> :
    scope<Base,
          std::conditional_t<std::is_void_v<Rename>, R0, Rename>,
          D0 || DenyUnknown,
          Presence != 0 ? Presence : P0> {};

/// The rename policy Attrs' struct spec states, void when it states none.
template <typename Attrs>
consteval auto rename_in() {
    if constexpr(spec_of<struct_spec, Attrs>.rename_all.has_value()) {
        return std::type_identity<
            naming::rename_policy_of_t<*spec_of<struct_spec, Attrs>.rename_all>>{};
    } else {
        return std::type_identity<void>{};
    }
}

}  // namespace detail

/// The format a node of type T carrying Attrs, a field's attrs or its type's, is read and
/// written under: Format scoped by the struct attributes of Attrs (rename_all,
/// deny_unknown_fields, presence_all) when T is a reflectable struct, whose members they name
/// and mark, or an enum, whose names an enum_names repr spells by its rename_all; Format on
/// every other node. The dispatch and type_info both scope through it, so that type_info
/// describes the documents the codec reads and writes.
template <typename Format, typename T, typename Attrs>
using node_format_t = std::conditional_t<
    meta::reflectable_class<T> || std::is_enum_v<T>,
    typename detail::scope<Format,
                           typename decltype(detail::rename_in<Attrs>())::type,
                           spec_of<struct_spec, Attrs>.deny_unknown_fields,
                           detail::presence_code(spec_of<struct_spec, Attrs>.presence_all)>::type,
    Format>;

/// The format the values of a struct's fields are read and written under, Format being the
/// struct's scope: the format the scope was made of, as a struct's attributes name and mark its
/// own members alone.
template <typename Format>
struct members_format {
    using type = Format;
};

template <typename Base, typename Rename, bool DenyUnknown, std::uint8_t Presence>
struct members_format<detail::scoped_format<Base, Rename, DenyUnknown, Presence>> {
    using type = Base;
};

template <typename Format>
using members_format_t = typename members_format<Format>::type;

/// The presence a field of kind `kind` whose spec is `spec` has under Format: the spec's own,
/// else Format's presence_all (a struct's, scoped on the way, or the format's own), else
/// optional for a std::optional, a pointer or null, and required for the rest.
template <typename Format>
constexpr presence_kind effective_presence(const field_spec& spec, meta::type_kind kind) {
    if(spec.presence) {
        return *spec.presence;
    }
    if(Format::presence_all) {
        return *Format::presence_all;
    }
    using enum meta::type_kind;
    return kind == optional || kind == pointer || kind == null ? presence_kind::optional
                                                               : presence_kind::required;
}

}  // namespace kotatsu::codec
