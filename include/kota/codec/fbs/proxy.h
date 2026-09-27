#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

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

template <typename T>
constexpr bool is_string_like_v = meta::str_like<T>;

template <typename T>
constexpr bool is_range_like_v = std::ranges::input_range<T> && !is_string_like_v<T>;

template <typename T>
constexpr bool is_tuple_like_v = meta::tuple_like<T>;

template <typename T>
constexpr bool is_scalar_v =
    meta::bool_like<T> || meta::int_like<T> || meta::uint_like<T> || meta::floating_like<T> ||
    meta::char_like<T> || std::is_enum_v<T> || std::same_as<T, std::byte>;

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
    constexpr auto k = meta::kind_of<repr_t>();
    if(k == meta::type_kind::null || k == meta::type_kind::optional ||
       k == meta::type_kind::pointer || k == meta::type_kind::array || k == meta::type_kind::set ||
       k == meta::type_kind::map || k == meta::type_kind::bytes) {
        return element_layout::boxed;
    }
    if(is_string_like_v<repr_t>) {
        return element_layout::string;
    }
    if(is_scalar_v<repr_t>) {
        return element_layout::scalar;
    }
    if(can_inline_struct_v<repr_t> && !is_tuple_like_v<repr_t>) {
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
struct scalar_cell<std::byte> {
    using type = std::uint8_t;
};

template <>
struct scalar_cell<long double> {
    using type = double;
};

template <typename T>
    requires std::is_enum_v<T>
struct scalar_cell<T> {
    using type = std::underlying_type_t<T>;
};

template <typename T>
using scalar_cell_t = typename scalar_cell<std::remove_cvref_t<T>>::type;

template <typename Object>
using object_schema = meta::virtual_schema<Object, meta::format_config<format>>;

template <typename Object>
consteval std::size_t field_slot_count() {
    return object_schema<Object>::fields.size();
}

// Computes the field slot index via union-wrapped uninitialized memory rather
// than a live Object, so it works for aggregates whose members have explicit
// default constructors.
template <typename Object, typename Member>
auto field_index(Member Object::* member) -> std::size_t {
    meta::detail::uninitialized<Object> uninit;
    const auto base = reinterpret_cast<std::uintptr_t>(std::addressof(uninit.value));
    const auto field = reinterpret_cast<std::uintptr_t>(std::addressof(uninit.value.*member));
    const auto offset = static_cast<std::size_t>(field - base);

    constexpr auto& fields = object_schema<Object>::fields;
    for(std::size_t i = 0; i < fields.size(); ++i) {
        if(fields[i].offset == offset) {
            return i;
        }
    }
    return fields.size();
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
constexpr bool is_map_range_v = [] {
    if constexpr(is_range_like_v<T>) {
        return kota::format_kind<T> == kota::range_format::map;
    } else {
        return false;
    }
}();

template <typename T, typename = void>
struct field_return_type;

template <typename T>
using field_return_type_t = typename field_return_type<T>::type;

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

// Uses partial specialization to avoid eagerly instantiating type aliases for non-matching
// branches.
template <typename T, typename>
struct field_return_type {
    using type = table_view<T>;
};

template <typename T>
struct field_return_type<T, std::enable_if_t<is_string_like_v<T>>> {
    using type = std::string_view;
};

template <typename T>
struct field_return_type<T,
                         std::enable_if_t<!is_string_like_v<T> && !is_tuple_like_v<T> &&
                                          (is_scalar_v<T> || can_inline_struct_v<T>)>> {
    using type = T;
};

template <typename T>
struct field_return_type<T, std::enable_if_t<is_specialization_of<std::variant, T>>> {
    using type = variant_view_for_t<T>;
};

template <typename T>
struct field_return_type<
    T,
    std::enable_if_t<!is_specialization_of<std::variant, T> && is_tuple_like_v<T>>> {
    using type = tuple_view_for_t<T>;
};

template <typename T>
struct field_return_type<T, std::enable_if_t<is_map_range_v<T>>> {
    using type = map_view_for_t<T>;
};

template <typename T>
struct field_return_type<
    T,
    std::enable_if_t<!is_string_like_v<T> && !is_scalar_v<T> && !can_inline_struct_v<T> &&
                     !is_specialization_of<std::variant, T> && !is_tuple_like_v<T> &&
                     !is_map_range_v<T> && is_range_like_v<T>>> {
    // The raw element type is kept: array_view itself distinguishes the
    // element layout (on the unpeeled representation) from the peeled view type.
    using type = array_view<std::remove_cvref_t<std::ranges::range_value_t<T>>>;
};

template <typename Member,
          typename CleanMember = deep_clean_t<Member>,
          bool IsRange = is_range_like_v<CleanMember> && !is_tuple_like_v<CleanMember>>
struct member_return_impl;

template <typename Member, typename CleanMember>
    requires is_map_range_v<CleanMember>
struct member_return_impl<Member, CleanMember, true> {
    using type = map_view_for_t<CleanMember>;
};

template <typename Member, typename CleanMember>
    requires (!is_map_range_v<CleanMember>)
struct member_return_impl<Member, CleanMember, true> {
    using type = array_view<std::remove_cvref_t<std::ranges::range_value_t<CleanMember>>>;
};

template <typename Member, typename CleanMember>
struct member_return_impl<Member, CleanMember, false> {
    using type = field_return_type_t<CleanMember>;
};

template <typename Member>
using member_return_t = typename member_return_impl<Member>::type;

// Unchecked read: returns {} on miss rather than reporting errors.
template <typename T>
auto read_field(table_ref view, slot_id field) -> field_return_type_t<T> {
    using return_t = field_return_type_t<T>;

    const auto* table = view.raw();

    if constexpr(std::same_as<T, std::byte>) {
        return std::byte{view.template get_scalar<std::uint8_t>(field)};
    } else if constexpr(std::is_enum_v<T>) {
        using cell_t = std::underlying_type_t<T>;
        return static_cast<T>(view.template get_scalar<cell_t>(field));
    } else if constexpr(meta::char_like<T>) {
        return static_cast<T>(view.template get_scalar<std::int8_t>(field));
    } else if constexpr(meta::bool_like<T> || meta::int_like<T> || meta::uint_like<T>) {
        return view.template get_scalar<T>(field);
    } else if constexpr(meta::floating_like<T>) {
        if constexpr(std::same_as<T, float> || std::same_as<T, double>) {
            return view.template get_scalar<T>(field);
        } else {
            return static_cast<T>(view.template get_scalar<double>(field));
        }
    } else if constexpr(is_string_like_v<T>) {
        const auto* text = table->template GetPointer<const String*>(field);
        if(text == nullptr) {
            return {};
        }
        return std::string_view(text->data(), text->size());
    } else if constexpr(is_specialization_of<std::variant, T> || is_tuple_like_v<T>) {
        // Table-shaped like the trailing else, but ordered before the range
        // branch: std::array is both tuple-like and range-like and must read
        // as a table.
        const auto* nested = table->template GetPointer<const Table*>(field);
        return return_t(table_ref(nested));
    } else if constexpr(can_inline_struct_v<T>) {
        const auto* value = table->template GetStruct<const T*>(field);
        if(value == nullptr) {
            return {};
        }
        return *value;
    } else if constexpr(is_map_range_v<T>) {
        const auto* vec = table->template GetPointer<const Vector<table_offset_t>*>(field);
        return return_t(vec);
    } else if constexpr(is_range_like_v<T>) {
        // array_view owns the element-layout classification; read with its
        // pointer type so the two can never diverge.
        const auto* value = table->template GetPointer<typename return_t::vector_ptr_type>(field);
        return return_t(value);
    } else {
        const auto* nested = table->template GetPointer<const Table*>(field);
        return return_t(table_ref(nested));
    }
}

// Upfront deep verification for the zero-copy views: walks the buffer along
// the exact classification the views read through (read_field's branches,
// element_layout, deep_clean_t), bounds-checking every offset, string,
// vector, and table against the verifier. A buffer that verifies can then be
// accessed through table_view and friends without any further checks — the
// views trust their pointers, so this walk is what makes from_bytes safe on
// corrupt, truncated, or malicious input.

/// Whether an inline struct's memcpy image contains bool fields, recursing
/// into nested inline structs.
template <typename T>
consteval bool has_bool_field() {
    if constexpr(meta::reflectable_class<T>) {
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return (has_bool_field<std::remove_cv_t<meta::field_type<T, I>>>() || ...);
        }(std::make_index_sequence<meta::field_count<T>()>{});
    } else {
        return std::same_as<T, bool>;
    }
}

template <typename T>
bool bool_bytes_valid(const std::byte* image) {
    if constexpr(meta::reflectable_class<T>) {
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return (bool_bytes_valid<std::remove_cv_t<meta::field_type<T, I>>>(
                        image + meta::field_offset<T>(I)) &&
                    ...);
        }(std::make_index_sequence<meta::field_count<T>()>{});
    } else if constexpr(std::same_as<T, bool>) {
        return std::to_integer<std::uint8_t>(*image) <= 1;
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
bool valid_inline_struct_bytes([[maybe_unused]] const T* ptr) {
    if constexpr(has_bool_field<T>()) {
        return ptr == nullptr || bool_bytes_valid<T>(reinterpret_cast<const std::byte*>(ptr));
    } else {
        return true;
    }
}

/// Verify the value of view type T at a field slot, mirroring read_field's
/// classification branch for branch. An absent slot always verifies (the
/// views read it as a default), as does a present-but-null nested table.
template <typename T>
bool verify_field(verifier_t& v, const Table* tbl, slot_id slot);

/// Verify a table-shaped value (variant, tuple-like, or reflectable struct)
/// rooted at `tbl`. VerifyTableStart also counts depth and table visits, so
/// cyclic offsets and alias amplification terminate.
template <typename T>
bool verify_table(verifier_t& v, const Table* tbl);

/// Verify one struct field slot: behavior attrs re-route the encoded type
/// exactly as meta's repr resolver does (with > as > enum_string), then the
/// resolved view type classifies the slot.
template <typename Slot>
bool verify_slot(verifier_t& v, const Table* tbl, slot_id slot) {
    using raw_t = std::remove_cv_t<typename Slot::raw_type>;
    using attrs_t = typename Slot::attrs;

    if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::with>) {
        using adapter = typename tuple_find_spec_t<attrs_t, meta::behavior::with>::adapter;
        return verify_field<deep_clean_t<meta::declared_repr_t<adapter>>>(v, tbl, slot);
    } else if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::as>) {
        using target = typename tuple_find_spec_t<attrs_t, meta::behavior::as>::target;
        return verify_field<deep_clean_t<target>>(v, tbl, slot);
    } else if constexpr(tuple_has_spec_v<attrs_t, meta::behavior::enum_string>) {
        return verify_field<std::string_view>(v, tbl, slot);
    } else {
        return verify_field<deep_clean_t<raw_t>>(v, tbl, slot);
    }
}

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
    } else if constexpr(is_tuple_like_v<T>) {
        detail::assert_slots_fit<std::tuple_size_v<T>>();
        ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return (
                verify_field<deep_clean_t<std::tuple_element_t<Is, T>>>(v, tbl, field_slot(Is)) &&
                ...);
        }(std::make_index_sequence<std::tuple_size_v<T>>{});
    } else {
        detail::assert_fields_reflected<T>();
        using slots = typename object_schema<T>::slots;
        detail::assert_slots_fit<type_list_size_v<slots>>();
        ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return (verify_slot<type_list_element_t<Is, slots>>(v, tbl, field_slot(Is)) && ...);
        }(std::make_index_sequence<type_list_size_v<slots>>{});
    }
    v.EndTable();
    return ok;
}

template <typename T>
bool verify_field(verifier_t& v, const Table* tbl, slot_id slot) {
    if constexpr(std::same_as<T, std::byte>) {
        return tbl->VerifyField<std::uint8_t>(v, slot, alignof(std::uint8_t));
    } else if constexpr(std::is_enum_v<T>) {
        using cell_t = std::underlying_type_t<T>;
        return tbl->VerifyField<cell_t>(v, slot, alignof(cell_t));
    } else if constexpr(meta::char_like<T>) {
        return tbl->VerifyField<std::int8_t>(v, slot, alignof(std::int8_t));
    } else if constexpr(meta::bool_like<T> || meta::int_like<T> || meta::uint_like<T>) {
        return tbl->VerifyField<T>(v, slot, alignof(T));
    } else if constexpr(meta::floating_like<T>) {
        using cell_t = scalar_cell_t<T>;
        return tbl->VerifyField<cell_t>(v, slot, alignof(cell_t));
    } else if constexpr(is_string_like_v<T>) {
        return tbl->VerifyOffset(v, slot) &&
               v.VerifyString(tbl->template GetPointer<const String*>(slot));
    } else if constexpr(is_specialization_of<std::variant, T> || is_tuple_like_v<T>) {
        // Table-shaped like the trailing else, but ordered before the range
        // branch to match read_field: std::array is both tuple-like and
        // range-like and travels as a table.
        if(!tbl->VerifyOffset(v, slot)) {
            return false;
        }
        const auto* nested = tbl->template GetPointer<const Table*>(slot);
        return nested == nullptr || verify_table<T>(v, nested);
    } else if constexpr(can_inline_struct_v<T>) {
        return tbl->VerifyField<T>(v, slot, alignof(T)) &&
               valid_inline_struct_bytes(tbl->template GetStruct<const T*>(slot));
    } else if constexpr(is_map_range_v<T>) {
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
    } else if constexpr(is_range_like_v<T>) {
        using element_t = std::remove_cvref_t<std::ranges::range_value_t<T>>;
        constexpr auto layout = element_layout_of<element_t>();
        using enum element_layout;

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
        if constexpr(layout == string) {
            return v.VerifyVectorOfStrings(vec);
        } else if constexpr(layout == boxed) {
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
        } else if constexpr(layout == table) {
            for(uoffset_t i = 0; i < vec->size(); ++i) {
                const auto* child = vec->template GetAs<Table>(i);
                if(!verify_table<deep_clean_t<element_t>>(v, child)) {
                    return false;
                }
            }
            return true;
        } else {
            // scalar / inline_struct: VerifyVector covered the element data.
            // An inline struct's bool bytes additionally need semantic
            // checking — in-bounds is not the same as a valid bool.
            if constexpr(layout == inline_struct) {
                for(uoffset_t i = 0; i < vec->size(); ++i) {
                    if(!valid_inline_struct_bytes(vec->Get(i))) {
                        return false;
                    }
                }
            }
            return true;
        }
    } else {
        // Reflectable structure — the table_view branch of read_field.
        if(!tbl->VerifyOffset(v, slot)) {
            return false;
        }
        const auto* nested = tbl->template GetPointer<const Table*>(slot);
        return nested == nullptr || verify_table<T>(v, nested);
    }
}

/// The canonical ordering of encoded map entries: the encoder sorts by it
/// and map_view::find_entry binary-searches with it. Reflected structs
/// compare field-wise in declaration order (recursing); everything else
/// through operator<, so strings stay lexicographic and heterogeneous
/// scalar lookups keep working.
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

template <typename Element>
class array_view {
    // The element layout is decided on the unpeeled resolved representation;
    // nullable peeling applies only to the view returned for each element.
    constexpr static auto layout = proxy_detail::element_layout_of<Element>();

public:
    using element_type = proxy_detail::deep_clean_t<Element>;
    using value_type = proxy_detail::field_return_type_t<element_type>;
    using vector_ptr_type = proxy_detail::element_vector_ptr_t<Element>;

    constexpr array_view() = default;

    constexpr explicit array_view(vector_ptr_type vector) noexcept : vector(vector) {}

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

    auto operator[](std::size_t index) const -> value_type {
        return at(index);
    }

    auto at(std::size_t index) const -> value_type {
        using enum proxy_detail::element_layout;

        if(!valid() || index >= size()) {
            return value_type{};
        }

        if constexpr(layout == boxed) {
            const auto* wrapper = vector->template GetAs<Table>(static_cast<uoffset_t>(index));
            return proxy_detail::read_field<element_type>(proxy_detail::table_ref(wrapper),
                                                          detail::first_field);
        } else if constexpr(layout == scalar) {
            if constexpr(std::same_as<element_type, std::byte>) {
                return std::byte{vector->Get(static_cast<uoffset_t>(index))};
            } else if constexpr(std::is_enum_v<element_type>) {
                using cell_t = proxy_detail::scalar_cell_t<element_type>;
                return static_cast<element_type>(
                    static_cast<cell_t>(vector->Get(static_cast<uoffset_t>(index))));
            } else if constexpr(meta::char_like<element_type>) {
                return static_cast<char>(vector->Get(static_cast<uoffset_t>(index)));
            } else {
                return static_cast<element_type>(vector->Get(static_cast<uoffset_t>(index)));
            }
        } else if constexpr(layout == string) {
            const auto* text = vector->GetAsString(static_cast<uoffset_t>(index));
            if(text == nullptr) {
                return {};
            }
            return std::string_view(text->data(), text->size());
        } else if constexpr(layout == inline_struct) {
            const auto* value = vector->Get(static_cast<uoffset_t>(index));
            if(value == nullptr) {
                return {};
            }
            return *value;
        } else {
            const auto* nested = vector->template GetAs<Table>(static_cast<uoffset_t>(index));
            return value_type(proxy_detail::table_ref(nested));
        }
    }

    constexpr auto raw() const noexcept -> vector_ptr_type {
        return vector;
    }

private:
    vector_ptr_type vector = nullptr;
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

template <typename T>
class table_view {
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
    /// flatbuffers' maximum buffer size (just under 2 GiB).
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

    template <typename Member>
        requires meta::reflectable_class<object_type>
    auto has(Member object_type::* member) const -> bool {
        if(!valid()) {
            return false;
        }

        const auto index = proxy_detail::field_index(member);
        if(index >= proxy_detail::field_slot_count<object_type>()) {
            return false;
        }
        return view.has(detail::field_slot(index));
    }

    template <typename Member>
        requires meta::reflectable_class<object_type>
    auto operator[](Member object_type::* member) const -> proxy_detail::member_return_t<Member> {
        return (*this)(member);
    }

    template <typename Member>
        requires meta::reflectable_class<object_type>
    auto operator()(Member object_type::* member) const -> proxy_detail::member_return_t<Member> {
        using member_type = proxy_detail::deep_clean_t<Member>;
        using return_t = proxy_detail::member_return_t<Member>;

        if(!valid()) {
            return return_t{};
        }

        const auto index = proxy_detail::field_index(member);
        if(index >= proxy_detail::field_slot_count<object_type>()) {
            return return_t{};
        }

        return proxy_detail::read_field<member_type>(view, detail::field_slot(index));
    }

private:
    static consteval void assert_viewable() {
        static_assert(std::is_same_v<proxy_detail::apply_repr_t<object_type>, object_type>,
                      "table_view reads T's own table layout; a type whose fbs representation "
                      "differs from itself cannot be viewed — decode it with from_bytes instead");
    }

    view_type view;
};

}  // namespace kota::codec::fbs
