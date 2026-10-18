#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/function_traits.h"
#include "kota/support/tuple_traits.h"
#include "kota/support/type_list.h"
#include "kota/meta/repr.h"
#include "kota/meta/schema.h"
#include "kota/meta/struct.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/fbs/type.h"

namespace kota::codec::fbs {

template <typename T>
class table_view;

template <typename T>
class array_view;

template <typename... Ts>
class variant_view;

template <typename... Ts>
class tuple_view;

template <typename K, typename V>
class map_view;

namespace proxy_detail {

using slot_id = voffset_t;

/// A nullable reference to a raw flatbuffers table.
class table_ref {
public:
    table_ref() = default;

    explicit table_ref(const Table* t) : table(t) {}

    auto valid() const -> bool {
        return table != nullptr;
    }

    auto has(slot_id sid) const -> bool {
        return table != nullptr && table->GetOptionalFieldOffset(sid) != 0;
    }

    template <typename T>
    auto get_scalar(slot_id sid) const -> T {
        return table->GetField<T>(sid, T{});
    }

    auto raw() const -> const Table* {
        return table;
    }

private:
    const Table* table = nullptr;
};

/// Substitute a type by its resolved representation: annotation behavior attrs
/// take precedence over the underlying type's meta::repr, and chained reprs
/// are followed under the fbs format tag (meta::resolved_repr_t); identity
/// when neither applies. Flatbuffers computes layout statically, so a dynamic
/// representation is rejected here.
template <typename T>
constexpr auto apply_repr_impl() {
    using repr_t = meta::resolved_repr_t<T, format>;
    static_assert(!std::is_same_v<repr_t, meta::dynamic>,
                  "flatbuffers computes layout statically; a meta::dynamic repr cannot be "
                  "used with the fbs backend");
    return std::type_identity<repr_t>{};
}

template <typename T>
using apply_repr_t = typename decltype(apply_repr_impl<T>())::type;

/// The fully resolved view type of a member or element: representation
/// substitution (behavior attrs, then chained reprs) and nullable-wrapper
/// peeling (optional, smart pointers) interleave until a fixpoint, in
/// whatever order they nest.
template <typename T>
constexpr auto deep_clean_impl() {
    using repr_t = apply_repr_t<T>;
    if constexpr(is_optional_v<repr_t>) {
        return deep_clean_impl<typename repr_t::value_type>();
    } else if constexpr(is_specialization_of<std::unique_ptr, repr_t> ||
                        is_specialization_of<std::shared_ptr, repr_t> ||
                        is_specialization_of<std::weak_ptr, repr_t>) {
        return deep_clean_impl<typename repr_t::element_type>();
    } else {
        return std::type_identity<repr_t>{};
    }
}

template <typename T>
using deep_clean_t = typename decltype(deep_clean_impl<T>())::type;

/// The kinds that travel as a fixed-width cell (scalar_cell_t).
consteval bool is_scalar_kind(meta::type_kind k) {
    using enum meta::type_kind;
    return k == boolean || meta::is_integer_kind(k) || meta::is_floating_kind(k) ||
           k == character || k == enumeration;
}

/// How a vector element is laid out in the buffer.
enum class element_layout : std::uint8_t {
    scalar,         ///< vector of scalar cells (scalar_cell_t).
    string,         ///< vector of string offsets.
    inline_struct,  ///< vector of the element struct itself (VectorOfStructs).
    boxed,          ///< per-element wrapper table, value at the table's first field.
    table,          ///< vector of table offsets to the element's own table.
};

/// The single classification every consumer of a vector's element layout
/// consults — encode collector choice (seq_encode_impl), decode reader
/// (VecReader), the lazy array_view and its pointer type — so they can never
/// diverge. Decided on the element's unpeeled resolved representation: boxed
/// elements travel as wrapper tables because nullable and null-like shapes
/// need a table for absence to still occupy a vector entry, and nested
/// containers and byte blobs have no direct vector-of-vectors representation.
/// Tuple-like elements are tables even when they could inline as structs:
/// they decode through per-slot table access (tuple_view, TableFieldReader).
template <typename Element>
consteval element_layout element_layout_of() {
    using repr_t = apply_repr_t<std::remove_cvref_t<Element>>;
    using enum meta::type_kind;
    constexpr auto k = meta::kind_of<repr_t>();
    if(k == null || k == optional || k == pointer || k == array || k == set || k == map ||
       k == bytes) {
        return element_layout::boxed;
    }
    if(k == string) {
        return element_layout::string;
    }
    if(is_scalar_kind(k)) {
        return element_layout::scalar;
    }
    if(k == structure && can_inline_struct_v<repr_t>) {
        return element_layout::inline_struct;
    }
    return element_layout::table;
}

/// The concrete cell type a scalar is written as.
template <typename T>
struct scalar_cell {
    using type = std::remove_cvref_t<T>;
};

template <>
struct scalar_cell<bool> {
    using type = std::uint8_t;
};

template <>
struct scalar_cell<char> {
    using type = std::int8_t;
};

template <>
struct scalar_cell<long double> {
    using type = double;
};

/// An enum's underlying integer's cell: an enum over bool or char travels
/// as the uint8 or int8 cell its underlying type does.
template <typename T>
    requires std::is_enum_v<T>
struct scalar_cell<T> {
    using type = typename scalar_cell<std::underlying_type_t<T>>::type;
};

template <typename T>
using scalar_cell_t = typename scalar_cell<std::remove_cvref_t<T>>::type;

/// Whether a vector's elements are stored as their view type itself, so the
/// buffer holds an array of them that array_view can hand out in place, and
/// whose alignment any buffer from to_bytes held in a std::vector meets: an
/// inline struct's image, unless the struct is aligned past what operator new
/// guarantees; and, on little-endian hosts, the scalars whose cell is their
/// own object representation — every integer and floating type but long
/// double (a double cell), plain char (an int8 cell, which char may alias)
/// and std::byte. A bool cell can hold any byte, and an enum's cell is its
/// underlying integer, so their elements are read one by one, as are those
/// of an over-aligned struct.
template <typename Element>
consteval bool stored_in_place() {
    using repr_t = apply_repr_t<std::remove_cvref_t<Element>>;
    constexpr auto layout = element_layout_of<Element>();
    if constexpr(layout == element_layout::scalar) {
        return std::endian::native == std::endian::little &&
               (std::same_as<repr_t, scalar_cell_t<repr_t>> || meta::char_like<repr_t> ||
                std::same_as<repr_t, std::byte>);
    } else if constexpr(layout == element_layout::inline_struct) {
        return alignof(repr_t) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__;
    } else {
        return false;
    }
}

/// Copies a scalar cell or an inline struct out of its image in the buffer.
/// from_bytes verifies vector elements for bounds only, so they are only as
/// aligned as the buffer's placement and the 4-byte length word before them
/// make them; the image is copied bytewise, never read through a T pointer.
template <typename T>
auto load_unaligned(const std::uint8_t* image) -> T {
    T value;
    std::memcpy(std::addressof(value), image, sizeof(T));
    return value;
}

/// How a value of view type T sits in a table slot. The one classification
/// both read_field and verify_field branch on, so the views never read a
/// slot the verifier did not check. Decided on the type's kind, as the codec
/// dispatch that wrote the slot decides it.
enum class field_layout : std::uint8_t {
    null,           ///< nothing: a null value leaves its slot absent.
    scalar,         ///< a fixed-width cell (scalar_cell_t) in the table.
    string,         ///< an offset to a string.
    inline_struct,  ///< the struct's image in the table.
    vector,         ///< an offset to a vector laid out per element_layout_of.
    map,            ///< an offset to a sorted vector of {key, value} entry tables.
    table,          ///< an offset to the table of a struct, tuple or variant.
};

template <typename T>
consteval field_layout field_layout_of() {
    using enum meta::type_kind;
    constexpr auto k = meta::kind_of<T>();
    if(k == null) {
        return field_layout::null;
    }
    if(is_scalar_kind(k)) {
        return field_layout::scalar;
    }
    if(k == string) {
        return field_layout::string;
    }
    if(k == bytes || k == array || k == set) {
        return field_layout::vector;
    }
    if(k == map) {
        return field_layout::map;
    }
    if(k == structure && can_inline_struct_v<T>) {
        return field_layout::inline_struct;
    }
    return field_layout::table;
}

template <typename Object>
using object_schema = meta::virtual_schema<Object, meta::format_config<format>>;

/// The view type of a struct field slot: the type meta's resolver gives its
/// raw type and attrs, the type the dispatch wrote. A tagged variant is that
/// variant itself; anything else still has its nullable wrappers peeled.
template <typename Slot>
consteval auto slot_view_impl() {
    using resolved = decltype(meta::detail::resolve_node<std::remove_cv_t<typename Slot::raw_type>,
                                                         typename Slot::attrs,
                                                         meta::format_config<format>>());
    if constexpr(std::is_same_v<typename resolved::tag_attrs, std::tuple<>>) {
        return std::type_identity<deep_clean_t<typename resolved::type>>{};
    } else {
        return std::type_identity<typename resolved::type>{};
    }
}

/// The view type of the I-th slot of Object's table.
template <typename Object, std::size_t I>
using slot_view_t =
    typename decltype(slot_view_impl<
                      type_list_element_t<I, typename object_schema<Object>::slots>>())::type;

template <typename Object, std::size_t I>
using field_value_t = std::remove_cv_t<meta::field_type<Object, I>>;

/// Whether Object's I-th field has a slot of its own, so the views address
/// it: a skipped field has none, and a flattened field's own fields take the
/// enclosing table's slots. Both are attrs of the field's type, so either
/// every member of a type is addressable or none is.
template <typename Object, std::size_t I>
constexpr bool addressable_field_v = !meta::detail::field_attr_flags<Object, I>::skipped &&
                                     !meta::detail::field_attr_flags<Object, I>::flattened;

/// The types of Object's addressable members, each once.
template <typename Object, typename Seq = std::make_index_sequence<meta::field_count<Object>()>>
struct member_types;

template <typename Object, std::size_t... Is>
struct member_types<Object, std::index_sequence<Is...>> {
    using type = type_list_unique_t<
        type_list_concat_t<std::conditional_t<addressable_field_v<Object, Is>,
                                              type_list<field_value_t<Object, Is>>,
                                              type_list<>>...>>;
};

template <typename Object>
using member_types_t = typename member_types<Object>::type;

/// The indices of Object's fields of type M, in declaration order.
template <typename Object, typename M>
consteval auto member_fields() {
    constexpr auto typed = []<std::size_t... Is>(std::index_sequence<Is...>) {
        return std::array<bool, sizeof...(Is)>{std::same_as<field_value_t<Object, Is>, M>...};
    }(std::make_index_sequence<meta::field_count<Object>()>{});
    std::array<std::size_t, std::ranges::count(typed, true)> fields{};
    std::size_t next = 0;
    for(std::size_t i = 0; i < typed.size(); ++i) {
        if(typed[i]) {
            fields[next++] = i;
        }
    }
    return fields;
}

/// The slots of Object's fields of type M: a field's slot is the count of the
/// slots the fields before it take — one each, none for a skipped one, a
/// flattened one's own count — as meta's schema lays them out.
template <typename Object, typename M>
consteval auto member_slots() {
    constexpr auto taken = []<std::size_t... Is>(std::index_sequence<Is...>) {
        return std::array<std::size_t, sizeof...(Is)>{
            type_list_size_v<typename meta::detail::single_field_slots<Object, Is>::type>...};
    }(std::make_index_sequence<meta::field_count<Object>()>{});
    auto slots = member_fields<Object, M>();
    for(auto& slot: slots) {
        slot = std::accumulate(taken.begin(), taken.begin() + slot, std::size_t{0});
    }
    return slots;
}

/// The typed flatbuffers vector pointer an element layout is read through;
/// boxed and table layouts share the table-offset vector shape.
template <typename Element, element_layout Layout = element_layout_of<Element>()>
struct element_vector_ptr {
    using type = const Vector<table_offset_t>*;
};

template <typename Element>
struct element_vector_ptr<Element, element_layout::scalar> {
    using type = const Vector<scalar_cell_t<apply_repr_t<Element>>>*;
};

template <typename Element>
struct element_vector_ptr<Element, element_layout::string> {
    using type = const Vector<string_offset_t>*;
};

template <typename Element>
struct element_vector_ptr<Element, element_layout::inline_struct> {
    using type = const Vector<const apply_repr_t<Element>*>*;
};

template <typename Element>
using element_vector_ptr_t = typename element_vector_ptr<Element>::type;

template <typename T>
struct variant_view_for;

template <typename... Ts>
struct variant_view_for<std::variant<Ts...>> {
    using type = variant_view<Ts...>;
};

template <typename T>
using variant_view_for_t = typename variant_view_for<T>::type;

template <typename T>
struct tuple_view_for;

template <typename... Ts>
struct tuple_view_for<std::tuple<Ts...>> {
    using type = tuple_view<Ts...>;
};

template <typename K, typename V>
struct tuple_view_for<std::pair<K, V>> {
    using type = tuple_view<K, V>;
};

template <typename T, std::size_t N>
struct tuple_view_for<std::array<T, N>> {
private:
    template <std::size_t... Is>
    static auto helper(std::index_sequence<Is...>)
        -> tuple_view<std::enable_if_t<(void(Is), true), T>...>;

public:
    using type = decltype(helper(std::make_index_sequence<N>{}));
};

template <typename T>
using tuple_view_for_t = typename tuple_view_for<T>::type;

template <typename T>
struct map_view_for;

template <typename T>
    requires requires {
        typename T::key_type;
        typename T::mapped_type;
    }
struct map_view_for<T> {
    using type = map_view<typename T::key_type, typename T::mapped_type>;
};

template <typename T>
using map_view_for_t = typename map_view_for<T>::type;

/// The type read_field returns for a value of view type T. Only the chosen
/// branch's alias is instantiated.
template <typename T>
consteval auto field_return_impl() {
    using enum field_layout;
    constexpr auto layout = field_layout_of<T>();
    if constexpr(layout == null || layout == scalar || layout == inline_struct) {
        return std::type_identity<T>{};
    } else if constexpr(layout == string) {
        return std::type_identity<std::string_view>{};
    } else if constexpr(layout == vector) {
        // The raw element type is kept: array_view itself distinguishes the
        // element layout (on the unpeeled representation) from the peeled
        // view type.
        return std::type_identity<array_view<std::remove_cvref_t<std::ranges::range_value_t<T>>>>{};
    } else if constexpr(layout == map) {
        return std::type_identity<map_view_for_t<T>>{};
    } else if constexpr(meta::kind_of<T>() == meta::type_kind::variant) {
        return std::type_identity<variant_view_for_t<T>>{};
    } else if constexpr(meta::kind_of<T>() == meta::type_kind::tuple) {
        return std::type_identity<tuple_view_for_t<T>>{};
    } else {
        return std::type_identity<table_view<T>>{};
    }
}

template <typename T>
using field_return_type_t = typename decltype(field_return_impl<T>())::type;

/// Unchecked read: an absent slot reads as the type's default.
template <typename T>
auto read_field(table_ref view, slot_id field) -> field_return_type_t<T> {
    using return_t = field_return_type_t<T>;
    using enum field_layout;
    constexpr auto layout = field_layout_of<T>();

    const auto* tbl = view.raw();

    if constexpr(layout == null) {
        return T();
    } else if constexpr(layout == scalar) {
        // Read as the cell the encoder wrote, then converted: a bool cell
        // holding 2 reads as true, as it decodes.
        return static_cast<T>(view.template get_scalar<scalar_cell_t<T>>(field));
    } else if constexpr(layout == string) {
        const auto* text = tbl->template GetPointer<const String*>(field);
        if(text == nullptr) {
            return {};
        }
        return std::string_view(text->data(), text->size());
    } else if constexpr(layout == inline_struct) {
        const auto* image = tbl->template GetStruct<const std::uint8_t*>(field);
        if(image == nullptr) {
            return T();
        }
        return load_unaligned<T>(image);
    } else if constexpr(layout == vector) {
        // array_view owns the element-layout classification; read with its
        // pointer type so the two can never diverge.
        return return_t(tbl->template GetPointer<typename return_t::vector_ptr_type>(field));
    } else if constexpr(layout == map) {
        return return_t(tbl->template GetPointer<const Vector<table_offset_t>*>(field));
    } else {
        return return_t(table_ref(tbl->template GetPointer<const Table*>(field)));
    }
}

/// The view type of Object's members of type M: they share their slot type.
template <typename Object, typename M>
using member_slot_view_t = slot_view_t<Object, member_slots<Object, M>()[0]>;

template <typename Object, typename M>
using member_view_t = field_return_type_t<member_slot_view_t<Object, M>>;

/// Reads a member of Object of type M from its slot; an invalid view reads
/// every member as its default.
template <typename Object, typename M>
auto read_member(table_ref view, std::size_t slot) -> member_view_t<Object, M> {
    if(!view.valid()) {
        return member_view_t<Object, M>();
    }
    return read_field<member_slot_view_t<Object, M>>(view, detail::field_slot(slot));
}

/// A member of Object of type M, located where it is named: the conversion
/// from &Object::member is evaluated at compile time, finding which of
/// Object's members of type M it points to by address, over an uninitialized
/// Object, so none is ever constructed. A member pointer that is not a
/// constant expression does not convert.
template <typename Object, typename M>
struct member_key {
    template <typename P>
        requires std::same_as<class_type_t<P>, Object> &&
                     std::same_as<std::remove_cv_t<member_type_t<P>>, M>
    consteval member_key(P member) : rank(rank_of(member)), slot(member_slots<Object, M>()[rank]) {}

    /// The member's place among Object's members of type M.
    std::size_t rank;
    /// The member's slot in Object's table.
    std::size_t slot;

private:
    template <typename P>
    static consteval std::size_t rank_of(P member) {
        const auto& unknown = meta::detail::ext<meta::detail::uninitialized<Object>>;
        const auto addresses = std::apply(
            [](auto... field) { return std::array<const void*, sizeof...(field)>{field...}; },
            meta::reflection<Object>::field_addrs(unknown.value));
        const void* address = std::addressof(unknown.value.*member);
        const auto fields = member_fields<Object, M>();
        return static_cast<std::size_t>(
            std::ranges::find(fields, address, [&](std::size_t i) { return addresses[i]; }) -
            fields.begin());
    }
};

/// table_view's access to Object's members of type M. table_view gathers one
/// per member type, and `view[&Object::member]` takes the one whose key the
/// member pointer converts to.
template <typename View, typename Object, typename M>
struct table_member {
    /// The member's view: a scalar or inline struct by value, a string as a
    /// string_view, a sequence as an array_view, a map as a map_view, a
    /// table-shaped member as its view. An absent member, or any member of
    /// an invalid view, reads as that view type's default.
    auto operator[](member_key<Object, M> key) const -> member_view_t<Object, M> {
        return read_member<Object, M>(table_ref(static_cast<const View&>(*this).raw()), key.slot);
    }

    /// Whether the buffer holds the member: false for an absent nullable,
    /// and on an invalid view.
    auto has(member_key<Object, M> key) const -> bool {
        return table_ref(static_cast<const View&>(*this).raw()).has(detail::field_slot(key.slot));
    }
};

template <typename View, typename Object, typename Types = member_types_t<Object>>
struct table_members;

template <typename View, typename Object, typename... Ms>
struct table_members<View, Object, type_list<Ms...>> : table_member<View, Object, Ms>... {
    using table_member<View, Object, Ms>::operator[]...;
    using table_member<View, Object, Ms>::has...;
};

/// bound_view's column of Object's members of type M, read once.
template <typename Object, typename M>
class bound_member {
    constexpr static auto slots = member_slots<Object, M>();

public:
    bound_member() = default;

    explicit bound_member(table_ref view) :
        column([&]<std::size_t... Rs>(std::index_sequence<Rs...>) {
            return std::array{read_member<Object, M>(view, slots[Rs])...};
        }(std::make_index_sequence<slots.size()>{})) {}

    auto operator[](member_key<Object, M> key) const -> const member_view_t<Object, M>& {
        return column[key.rank];
    }

private:
    std::array<member_view_t<Object, M>, slots.size()> column{};
};

template <typename Object, typename Types = member_types_t<Object>>
struct bound_members;

template <typename Object, typename... Ms>
struct bound_members<Object, type_list<Ms...>> : bound_member<Object, Ms>... {
    bound_members() = default;

    explicit bound_members(table_ref view) : bound_member<Object, Ms>(view)... {}

    using bound_member<Object, Ms>::operator[]...;
};

// Upfront deep verification for the zero-copy views: walks the buffer along
// the exact classification the views read through (field_layout_of,
// element_layout_of, slot_view_t), bounds-checking every offset, string,
// vector, and table against the verifier. A buffer that verifies can then be
// accessed through table_view and friends without any further checks — the
// views trust their pointers, so this walk is what makes from_bytes safe on
// corrupt, truncated, or malicious input.

/// Whether a field of an inline struct is a bool, or an enum over one.
template <typename T>
consteval bool is_bool_image() {
    if constexpr(std::is_enum_v<T>) {
        return std::same_as<std::underlying_type_t<T>, bool>;
    } else {
        return std::same_as<T, bool>;
    }
}

/// Whether an inline struct's memcpy image contains bool fields, recursing
/// into nested inline structs.
template <typename T>
consteval bool has_bool_field() {
    if constexpr(meta::reflectable_class<T>) {
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return (has_bool_field<std::remove_cv_t<meta::field_type<T, I>>>() || ...);
        }(std::make_index_sequence<meta::field_count<T>()>{});
    } else {
        return is_bool_image<T>();
    }
}

template <typename T>
bool bool_bytes_valid(const std::uint8_t* image) {
    if constexpr(meta::reflectable_class<T>) {
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return (bool_bytes_valid<std::remove_cv_t<meta::field_type<T, I>>>(
                        image + meta::field_offset<T>(I)) &&
                    ...);
        }(std::make_index_sequence<meta::field_count<T>()>{});
    } else if constexpr(is_bool_image<T>()) {
        return *image <= 1;
    } else {
        return true;
    }
}

/// Size/alignment verification admits any byte image for an inline struct,
/// but not every image is a valid T: a bool member's byte must be 0 or 1 —
/// evaluating any other representation is undefined behavior, and both
/// decode paths copy the whole object out of the buffer. Checks every bool
/// byte through the raw image (never through a T lvalue, which would be the
/// very UB being prevented); null — an absent field — verifies trivially.
template <typename T>
bool valid_inline_struct_bytes([[maybe_unused]] const std::uint8_t* image) {
    if constexpr(has_bool_field<T>()) {
        return image == nullptr || bool_bytes_valid<T>(image);
    } else {
        return true;
    }
}

/// Verify the value of view type T at a field slot, along the field_layout
/// read_field reads it by. An absent slot always verifies (the views read it
/// as a default), as does a present-but-null nested table.
template <typename T>
bool verify_field(verifier_t& v, const Table* tbl, slot_id slot);

/// Verify a table-shaped value (variant, tuple-like, or reflectable struct)
/// rooted at `tbl`. VerifyTableStart also counts depth and table visits, so
/// cyclic offsets and alias amplification terminate.
template <typename T>
bool verify_table(verifier_t& v, const Table* tbl);

template <typename T>
bool verify_table(verifier_t& v, const Table* tbl) {
    if(!tbl->VerifyTableStart(v)) {
        return false;
    }
    using detail::field_slot;
    bool ok;
    if constexpr(is_specialization_of<std::variant, T>) {
        detail::assert_slots_fit<std::variant_size_v<T> + 1>();
        // variant_view::get<I>() is reachable for every alternative
        // regardless of the stored tag, so every payload slot must verify.
        ok = tbl->VerifyField<std::uint32_t>(v, field_slot(0), alignof(std::uint32_t)) &&
             [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                 return (verify_field<deep_clean_t<std::variant_alternative_t<Is, T>>>(
                             v,
                             tbl,
                             field_slot(Is + 1)) &&
                         ...);
             }(std::make_index_sequence<std::variant_size_v<T>>{});
    } else if constexpr(meta::tuple_like<T>) {
        detail::assert_slots_fit<std::tuple_size_v<T>>();
        ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return (
                verify_field<deep_clean_t<std::tuple_element_t<Is, T>>>(v, tbl, field_slot(Is)) &&
                ...);
        }(std::make_index_sequence<std::tuple_size_v<T>>{});
    } else {
        detail::assert_fields_reflected<T>();
        constexpr auto slot_count = type_list_size_v<typename object_schema<T>::slots>;
        detail::assert_slots_fit<slot_count>();
        ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return (verify_field<slot_view_t<T, Is>>(v, tbl, field_slot(Is)) && ...);
        }(std::make_index_sequence<slot_count>{});
    }
    v.EndTable();
    return ok;
}

template <typename T>
bool verify_field(verifier_t& v, const Table* tbl, slot_id slot) {
    using enum field_layout;
    constexpr auto layout = field_layout_of<T>();

    if constexpr(layout == null) {
        // read_field reads nothing from the slot.
        return true;
    } else if constexpr(layout == scalar) {
        using cell_t = scalar_cell_t<T>;
        return tbl->VerifyField<cell_t>(v, slot, alignof(cell_t));
    } else if constexpr(layout == string) {
        return tbl->VerifyOffset(v, slot) &&
               v.VerifyString(tbl->template GetPointer<const String*>(slot));
    } else if constexpr(layout == inline_struct) {
        return tbl->VerifyField<T>(v, slot, alignof(T)) &&
               valid_inline_struct_bytes<T>(tbl->template GetStruct<const std::uint8_t*>(slot));
    } else if constexpr(layout == map) {
        using clean_key_t = deep_clean_t<typename T::key_type>;
        using clean_mapped_t = deep_clean_t<typename T::mapped_type>;
        if(!tbl->VerifyOffset(v, slot)) {
            return false;
        }
        const auto* vec = tbl->template GetPointer<const Vector<table_offset_t>*>(slot);
        if(!v.VerifyVector(vec)) {
            return false;
        }
        if(vec == nullptr) {
            return true;
        }
        for(uoffset_t i = 0; i < vec->size(); ++i) {
            const auto* entry = vec->template GetAs<Table>(i);
            if(!entry->VerifyTableStart(v)) {
                return false;
            }
            const bool ok = verify_field<clean_key_t>(v, entry, detail::field_slot(0)) &&
                            verify_field<clean_mapped_t>(v, entry, detail::field_slot(1));
            v.EndTable();
            if(!ok) {
                return false;
            }
        }
        return true;
    } else if constexpr(layout == vector) {
        using element_t = std::remove_cvref_t<std::ranges::range_value_t<T>>;
        constexpr auto elements = element_layout_of<element_t>();

        if(!tbl->VerifyOffset(v, slot)) {
            return false;
        }
        const auto* vec = tbl->template GetPointer<element_vector_ptr_t<element_t>>(slot);
        if(!v.VerifyVector(vec)) {
            return false;
        }
        if(vec == nullptr) {
            return true;
        }
        if constexpr(elements == element_layout::string) {
            return v.VerifyVectorOfStrings(vec);
        } else if constexpr(elements == element_layout::boxed) {
            for(uoffset_t i = 0; i < vec->size(); ++i) {
                const auto* wrapper = vec->template GetAs<Table>(i);
                if(!wrapper->VerifyTableStart(v)) {
                    return false;
                }
                const bool ok =
                    verify_field<deep_clean_t<element_t>>(v, wrapper, detail::first_field);
                v.EndTable();
                if(!ok) {
                    return false;
                }
            }
            return true;
        } else if constexpr(elements == element_layout::table) {
            for(uoffset_t i = 0; i < vec->size(); ++i) {
                const auto* child = vec->template GetAs<Table>(i);
                if(!verify_table<deep_clean_t<element_t>>(v, child)) {
                    return false;
                }
            }
            return true;
        } else {
            // scalar / inline_struct: VerifyVector bounded the element data
            // but checked only the length word's alignment, and only relative
            // to the buffer's start. array_view reads elements stored in place
            // through pointers to them, so their data must sit aligned at its
            // actual address; the others it copies out.
            const auto* data = vec->Data();
            if constexpr(stored_in_place<element_t>()) {
                if(reinterpret_cast<std::uintptr_t>(data) % alignof(apply_repr_t<element_t>) != 0) {
                    return false;
                }
            }
            // An inline struct's bool bytes additionally need semantic
            // checking — in-bounds is not the same as a valid bool.
            if constexpr(elements == element_layout::inline_struct) {
                using struct_t = apply_repr_t<element_t>;
                for(uoffset_t i = 0; i < vec->size(); ++i) {
                    if(!valid_inline_struct_bytes<struct_t>(data + i * sizeof(struct_t))) {
                        return false;
                    }
                }
            }
            return true;
        }
    } else {
        if(!tbl->VerifyOffset(v, slot)) {
            return false;
        }
        const auto* nested = tbl->template GetPointer<const Table*>(slot);
        return nested == nullptr || verify_table<T>(v, nested);
    }
}

/// A scalar key as map ordering compares it: a char as the int8 cell it is
/// stored as, so the order does not hang on the platform's char signedness,
/// and an enum as its underlying value.
template <typename T>
constexpr auto ordering_cell(T value) {
    if constexpr(std::is_enum_v<T>) {
        return ordering_cell(std::to_underlying(value));
    } else if constexpr(meta::char_like<T>) {
        return static_cast<std::int8_t>(value);
    } else {
        return value;
    }
}

/// The canonical ordering of encoded map entries: the encoder sorts by it
/// and map_view::find_entry binary-searches with it. Reflected structs
/// compare field-wise in declaration order (recursing); chars and enums as
/// their ordering_cell; integers by value whatever their signedness;
/// everything else through operator<, so strings stay lexicographic and
/// heterogeneous lookups keep working.
template <typename T, typename U>
constexpr bool ordering_less(const T& a, const U& b) {
    if constexpr(meta::reflectable_class<T>) {
        static_assert(std::same_as<T, U>, "a reflected key compares against its own type");
        bool less = false;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            // The fold walks fields while they compare equal; the first
            // differing field decides.
            ([&] {
                const auto& lhs = meta::field_of<Is>(a);
                const auto& rhs = meta::field_of<Is>(b);
                if(ordering_less(lhs, rhs)) {
                    less = true;
                    return false;
                }
                return !ordering_less(rhs, lhs);
            }() &&
             ...);
        }(std::make_index_sequence<meta::field_count<T>()>{});
        return less;
    } else if constexpr(std::is_enum_v<T> || meta::char_like<T> || std::is_enum_v<U> ||
                        meta::char_like<U>) {
        return ordering_less(ordering_cell(a), ordering_cell(b));
    } else if constexpr((meta::int_like<T> || meta::uint_like<T>) &&
                        (meta::int_like<U> || meta::uint_like<U>)) {
        return std::cmp_less(a, b);
    } else {
        return a < b;
    }
}

/// Equality derived from ordering_less, so struct keys need no operator==.
/// A NaN float field ties with every value under this equality (it is never
/// less in either direction); NaN keys are garbage-in, exactly as they are
/// for scalar float keys, where the encoder's sort already cannot order them.
template <typename T, typename U>
constexpr bool ordering_equal(const T& a, const U& b) {
    if constexpr(meta::reflectable_class<T>) {
        return !ordering_less(a, b) && !ordering_less(b, a);
    } else if constexpr(std::is_enum_v<T> || meta::char_like<T> || std::is_enum_v<U> ||
                        meta::char_like<U>) {
        return ordering_equal(ordering_cell(a), ordering_cell(b));
    } else if constexpr((meta::int_like<T> || meta::uint_like<T>) &&
                        (meta::int_like<U> || meta::uint_like<U>)) {
        return std::cmp_equal(a, b);
    } else {
        return a == b;
    }
}

/// A key a map_view lookup accepts. Reflected keys compare field-wise via
/// ordering_less and take only their own type — user-supplied comparisons
/// are ignored on both the encode and the lookup side, so admitting
/// heterogeneous queries through them would promise an ordering the search
/// does not use. Every other key takes anything totally ordered with its
/// decoded form. can_inline_struct_v implies fields_reflected_v, so a key
/// past the reflection field limit — whose field walk would visit nothing
/// and tie every lookup — is rejected here as well as by the encoder.
template <typename K, typename U>
concept map_lookup_key =
    (meta::reflectable_class<deep_clean_t<K>> && can_inline_struct_v<deep_clean_t<K>> &&
     std::same_as<U, deep_clean_t<K>>) ||
    (!meta::reflectable_class<deep_clean_t<K>> &&
     std::totally_ordered_with<field_return_type_t<deep_clean_t<K>>, const U&>);

}  // namespace proxy_detail

/// A vector in the buffer, read element by element as value_type. Always a
/// random-access range; when elements are stored as value_type itself
/// (proxy_detail::stored_in_place) a contiguous one, with data() pointing at
/// them, so std::span and similar views build straight from it.
template <typename Element>
class array_view {
    // The element layout is decided on the unpeeled resolved representation;
    // nullable peeling applies only to the view returned for each element.
    constexpr static auto layout = proxy_detail::element_layout_of<Element>();
    constexpr static bool contiguous = proxy_detail::stored_in_place<Element>();

public:
    using element_type = proxy_detail::deep_clean_t<Element>;
    using value_type = proxy_detail::field_return_type_t<element_type>;
    using vector_ptr_type = proxy_detail::element_vector_ptr_t<Element>;

    /// Reads elements by value, for the layouts not stored as value_type.
    class index_iterator {
    public:
        using iterator_concept = std::random_access_iterator_tag;
        using value_type = array_view::value_type;
        using difference_type = std::ptrdiff_t;

        constexpr index_iterator() = default;

        auto operator*() const -> value_type {
            return array_view::element(vector, static_cast<std::size_t>(index));
        }

        auto operator[](difference_type n) const -> value_type {
            return *(*this + n);
        }

        constexpr auto operator++() -> index_iterator& {
            ++index;
            return *this;
        }

        constexpr auto operator++(int) -> index_iterator {
            auto old = *this;
            ++index;
            return old;
        }

        constexpr auto operator--() -> index_iterator& {
            --index;
            return *this;
        }

        constexpr auto operator--(int) -> index_iterator {
            auto old = *this;
            --index;
            return old;
        }

        constexpr auto operator+=(difference_type n) -> index_iterator& {
            index += n;
            return *this;
        }

        constexpr auto operator-=(difference_type n) -> index_iterator& {
            index -= n;
            return *this;
        }

        friend constexpr auto operator+(index_iterator it, difference_type n) -> index_iterator {
            return it += n;
        }

        friend constexpr auto operator+(difference_type n, index_iterator it) -> index_iterator {
            return it += n;
        }

        friend constexpr auto operator-(index_iterator it, difference_type n) -> index_iterator {
            return it -= n;
        }

        friend constexpr auto operator-(const index_iterator& lhs, const index_iterator& rhs)
            -> difference_type {
            return lhs.index - rhs.index;
        }

        friend constexpr bool operator==(const index_iterator&, const index_iterator&) = default;

        friend constexpr auto operator<=>(const index_iterator&, const index_iterator&) = default;

    private:
        friend class array_view;

        constexpr index_iterator(vector_ptr_type vector, difference_type index) :
            vector(vector), index(index) {}

        vector_ptr_type vector = nullptr;
        difference_type index = 0;
    };

    using iterator = std::conditional_t<contiguous, const value_type*, index_iterator>;

    constexpr array_view() = default;

    explicit array_view(vector_ptr_type vector) noexcept :
        vector(vector), length(vector != nullptr ? vector->size() : 0U) {}

    constexpr auto valid() const noexcept -> bool {
        return vector != nullptr;
    }

    constexpr explicit operator bool() const noexcept {
        return valid();
    }

    constexpr auto size() const noexcept -> std::size_t {
        return length;
    }

    constexpr auto empty() const noexcept -> bool {
        return length == 0U;
    }

    /// The elements themselves; null for an absent vector.
    auto data() const noexcept -> const value_type*
        requires contiguous {
        return valid() ? reinterpret_cast<const value_type*>(vector->Data()) : nullptr;
    }

    auto begin() const -> iterator {
        if constexpr(contiguous) {
            return data();
        } else {
            return index_iterator(vector, 0);
        }
    }

    auto end() const -> iterator {
        if constexpr(contiguous) {
            return data() + length;
        } else {
            return index_iterator(vector, static_cast<std::ptrdiff_t>(length));
        }
    }

    auto operator[](std::size_t index) const -> value_type {
        return at(index);
    }

    /// The element at index; past the end, value_type's default.
    auto at(std::size_t index) const -> value_type {
        if(index >= length) {
            return value_type();
        }
        if constexpr(contiguous) {
            return data()[index];
        } else {
            return element(vector, index);
        }
    }

    constexpr auto raw() const noexcept -> vector_ptr_type {
        return vector;
    }

private:
    /// An element of a layout not stored as value_type.
    static auto element(vector_ptr_type vector, std::size_t index) -> value_type {
        using enum proxy_detail::element_layout;
        const auto offset = static_cast<uoffset_t>(index);

        if constexpr(layout == boxed) {
            const auto* wrapper = vector->template GetAs<Table>(offset);
            return proxy_detail::read_field<element_type>(proxy_detail::table_ref(wrapper),
                                                          detail::first_field);
        } else if constexpr(layout == scalar) {
            return static_cast<element_type>(vector->Get(offset));
        } else if constexpr(layout == inline_struct) {
            return proxy_detail::load_unaligned<value_type>(vector->Data() +
                                                            index * sizeof(value_type));
        } else if constexpr(layout == string) {
            const auto* text = vector->GetAsString(offset);
            return std::string_view(text->data(), text->size());
        } else {
            return value_type(proxy_detail::table_ref(vector->template GetAs<Table>(offset)));
        }
    }

    vector_ptr_type vector = nullptr;
    std::size_t length = 0;
};

template <typename... Ts>
class variant_view {
public:
    using view_type = proxy_detail::table_ref;

    constexpr variant_view() = default;

    constexpr explicit variant_view(view_type view) noexcept : view(view) {}

    constexpr auto valid() const noexcept -> bool {
        return view.valid();
    }

    constexpr explicit operator bool() const noexcept {
        return valid();
    }

    auto index() const -> std::size_t {
        if(!valid()) {
            return sizeof...(Ts);
        }
        return static_cast<std::size_t>(
            view.template get_scalar<std::uint32_t>(detail::field_slot(0)));
    }

    template <std::size_t I>
        requires (I < sizeof...(Ts))
    auto get() const -> proxy_detail::field_return_type_t<
        proxy_detail::deep_clean_t<std::variant_alternative_t<I, std::variant<Ts...>>>> {
        using alt_t = std::variant_alternative_t<I, std::variant<Ts...>>;
        using clean_alt_t = proxy_detail::deep_clean_t<alt_t>;
        using return_t = proxy_detail::field_return_type_t<clean_alt_t>;

        if(!valid()) {
            return return_t{};
        }

        return proxy_detail::read_field<clean_alt_t>(view, detail::field_slot(I + 1));
    }

    constexpr auto raw() const noexcept -> const Table* {
        return view.raw();
    }

private:
    view_type view;
};

template <typename... Ts>
class tuple_view {
public:
    using view_type = proxy_detail::table_ref;

    constexpr tuple_view() = default;

    constexpr explicit tuple_view(view_type view) noexcept : view(view) {}

    constexpr auto valid() const noexcept -> bool {
        return view.valid();
    }

    constexpr explicit operator bool() const noexcept {
        return valid();
    }

    template <std::size_t I>
        requires (I < sizeof...(Ts))
    auto get() const -> proxy_detail::field_return_type_t<
        proxy_detail::deep_clean_t<std::tuple_element_t<I, std::tuple<Ts...>>>> {
        using element_t = std::tuple_element_t<I, std::tuple<Ts...>>;
        using clean_element_t = proxy_detail::deep_clean_t<element_t>;
        using return_t = proxy_detail::field_return_type_t<clean_element_t>;

        if(!valid()) {
            return return_t{};
        }

        return proxy_detail::read_field<clean_element_t>(view, detail::field_slot(I));
    }

    constexpr auto raw() const noexcept -> const Table* {
        return view.raw();
    }

private:
    view_type view;
};

template <typename K, typename V>
class map_view {
public:
    using vector_type = const Vector<table_offset_t>*;

    constexpr map_view() = default;

    constexpr explicit map_view(vector_type vector) noexcept : vector(vector) {}

    constexpr auto valid() const noexcept -> bool {
        return vector != nullptr;
    }

    constexpr explicit operator bool() const noexcept {
        return valid();
    }

    auto size() const noexcept -> std::size_t {
        return valid() ? static_cast<std::size_t>(vector->size()) : 0U;
    }

    auto empty() const noexcept -> bool {
        return size() == 0U;
    }

    auto at(std::size_t index) const -> tuple_view<K, V> {
        if(!valid() || index >= size()) {
            return {};
        }
        const auto* entry = vector->template GetAs<Table>(static_cast<uoffset_t>(index));
        return tuple_view<K, V>(proxy_detail::table_ref(entry));
    }

    template <typename U = K>
        requires proxy_detail::map_lookup_key<K, U>
    auto operator[](const U& key) const
        -> proxy_detail::field_return_type_t<proxy_detail::deep_clean_t<V>> {
        using clean_v = proxy_detail::deep_clean_t<V>;
        using value_return_t = proxy_detail::field_return_type_t<clean_v>;

        auto entry = find_entry(key);
        if(!entry.valid()) {
            return value_return_t{};
        }
        return proxy_detail::read_field<clean_v>(entry, detail::field_slot(1));
    }

    template <typename U = K>
        requires proxy_detail::map_lookup_key<K, U>
    auto find(const U& key) const -> std::optional<tuple_view<K, V>> {
        auto entry = find_entry(key);
        if(!entry.valid()) {
            return std::nullopt;
        }
        return tuple_view<K, V>(entry);
    }

    template <typename U = K>
        requires proxy_detail::map_lookup_key<K, U>
    auto contains(const U& key) const -> bool {
        return find_entry(key).valid();
    }

    constexpr auto raw() const noexcept -> vector_type {
        return vector;
    }

private:
    template <typename U>
    auto find_entry(const U& key) const -> proxy_detail::table_ref {
        using clean_k = proxy_detail::deep_clean_t<K>;

        if(!valid()) {
            return {};
        }

        std::size_t lo = 0;
        std::size_t hi = size();
        while(lo < hi) {
            auto mid = lo + (hi - lo) / 2;
            const auto* entry = vector->template GetAs<Table>(static_cast<uoffset_t>(mid));
            auto entry_key = proxy_detail::read_field<clean_k>(proxy_detail::table_ref(entry),
                                                               detail::field_slot(0));
            if(proxy_detail::ordering_less(entry_key, key)) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        if(lo >= size()) {
            return {};
        }

        const auto* entry = vector->template GetAs<Table>(static_cast<uoffset_t>(lo));
        auto entry_view = proxy_detail::table_ref(entry);
        auto entry_key = proxy_detail::read_field<clean_k>(entry_view, detail::field_slot(0));
        if(proxy_detail::ordering_equal(entry_key, key)) {
            return entry_view;
        }
        return {};
    }

    vector_type vector = nullptr;
};

/// A table in the buffer. `view[&T::member]` reads a member and
/// `view.has(&T::member)` asks whether the buffer holds it, the member
/// located at compile time (proxy_detail::member_key); skipped and flattened
/// members have no slot of their own and are not addressable.
template <typename T>
class table_view : public proxy_detail::table_members<table_view<T>, std::remove_cvref_t<T>> {
public:
    using object_type = std::remove_cvref_t<T>;
    using view_type = proxy_detail::table_ref;

    constexpr table_view() = default;

    constexpr explicit table_view(view_type view) noexcept : view(view) {}

    /// Verifies the buffer against T's encoded shape before exposing it:
    /// the "EVTO" identifier, then every offset, string, vector, and table
    /// reachable through the views (including all variant payload slots).
    /// Accesses on the returned view are therefore in-bounds even for
    /// corrupt, truncated, or malicious input; a buffer that fails
    /// verification yields an invalid view. Table nesting deeper than the
    /// flatbuffers default of 64 is rejected, as are buffers at or above
    /// flatbuffers' maximum buffer size (just under 2 GiB). The views read
    /// the vectors whose elements are stored in place (integers, float,
    /// double, char, std::byte, inline structs aligned no more than operator
    /// new aligns) through pointers to them, so that element data must also
    /// sit aligned for the element at its actual address: to_bytes aligns it
    /// within the buffer, so a buffer held in a std::vector or a memory map
    /// passes; the same bytes copied to an odd address do not, though
    /// fbs::from_bytes still decodes them.
    static auto from_bytes(std::span<const std::byte> bytes) -> table_view {
        assert_viewable();
        auto opened = detail::open_root(bytes);
        if(!opened || !proxy_detail::verify_table<object_type>(opened->verifier, opened->root)) {
            return {};
        }
        return table_view(view_type(opened->root));
    }

    static auto from_bytes(std::span<const std::uint8_t> bytes) -> table_view {
        return from_bytes(std::as_bytes(bytes));
    }

    /// Wraps a buffer that already passed from_bytes verification, without
    /// re-verifying: the memory-map-once pattern verifies a blob when it is
    /// opened and constructs views per query. The caller owns that contract —
    /// on unverified bytes the view reads out of bounds.
    static auto from_verified_bytes(std::span<const std::uint8_t> bytes) -> table_view {
        assert_viewable();
        return table_view(view_type(::flatbuffers::GetRoot<Table>(bytes.data())));
    }

    static auto from_verified_bytes(std::span<const std::byte> bytes) -> table_view {
        const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
        return from_verified_bytes(std::span<const std::uint8_t>(data, bytes.size()));
    }

    constexpr auto valid() const noexcept -> bool {
        return view.valid();
    }

    constexpr explicit operator bool() const noexcept {
        return valid();
    }

    constexpr auto raw() const noexcept -> const Table* {
        return view.raw();
    }

private:
    static consteval void assert_viewable() {
        static_assert(std::is_same_v<proxy_detail::apply_repr_t<object_type>, object_type>,
                      "table_view reads T's own table layout; a type whose fbs representation "
                      "differs from itself cannot be viewed — decode it with from_bytes instead");
    }

    view_type view;
};

/// A table_view with every member read once: binding fetches each member's
/// view (a sequence's pointer and length, a scalar's value, a nested table's
/// view), and `bound[&T::member]` hands the cached one out with no lookup at
/// all. For a table read many times over, such as the columns of an index.
template <typename T>
class bound_view : public proxy_detail::bound_members<T> {
public:
    bound_view() = default;

    explicit bound_view(table_view<T> view) :
        proxy_detail::bound_members<T>(proxy_detail::table_ref(view.raw())) {}
};

}  // namespace kota::codec::fbs

namespace std::ranges {

/// An array_view's iterators and element pointers point into the buffer, not
/// into the view.
template <typename Element>
constexpr inline bool enable_borrowed_range<kota::codec::fbs::array_view<Element>> = true;

}  // namespace std::ranges
