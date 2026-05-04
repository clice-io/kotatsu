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
#include <vector>

#include "kota/meta/schema.h"
#include "kota/codec/detail/codec.h"
#include "kota/codec/flatbuffers/deserializer.h"
#include "kota/codec/flatbuffers/struct_layout.h"

#if __has_include(<flatbuffers/flatbuffers.h>)
#include "flatbuffers/flatbuffers.h"
#else
#error                                                                                             \
    "flatbuffers/flatbuffers.h not found. Enable KOTA_CODEC_ENABLE_FLATBUFFERS or add flatbuffers include paths."
#endif

namespace kota::codec::flatbuffers {

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

// Backend alias — the proxy layer operates through the Deserializer's
// TableView / slot-id abstraction rather than raw flatbuffers pointers.
using backend = Deserializer<>;
using table_view_type = backend::TableView;
using slot_id = backend::slot_id;

using codec::detail::remove_annotation_t;
using codec::detail::remove_optional_t;
using codec::detail::clean_t;

template <typename T>
constexpr bool is_string_like_v = codec::str_like<T>;

template <typename T>
constexpr bool is_range_like_v = std::ranges::input_range<T> && !is_string_like_v<T>;

template <typename T>
constexpr bool is_scalar_v =
    codec::bool_like<T> || codec::int_like<T> || codec::uint_like<T> || codec::floating_like<T> ||
    codec::char_like<T> || std::is_enum_v<T> || std::same_as<T, std::byte>;

// Smart pointer stripping: remove unique_ptr / shared_ptr wrappers (applied after clean_t)
template <typename T>
struct remove_smart_ptr {
    using type = T;
};

template <typename T, typename D>
struct remove_smart_ptr<std::unique_ptr<T, D>> {
    using type = typename remove_smart_ptr<T>::type;
};

template <typename T>
struct remove_smart_ptr<std::shared_ptr<T>> {
    using type = typename remove_smart_ptr<T>::type;
};

template <typename T>
using remove_smart_ptr_t = typename remove_smart_ptr<T>::type;

// decode_traits substitution: if the user specialized
// `codec::decode_traits<T>`, the proxy sees the declared `wire_type`
// rather than T itself. Ensures `root[&Struct::field]` and
// `map_view<K, V>` return views shaped by the wire type, keeping the lazy
// read path consistent with the arena decode path.
template <typename T>
struct apply_decode_traits {
    using type = T;
};

template <typename T>
    requires arena::has_decode_traits<backend, T>
struct apply_decode_traits<T> {
    using type = typename kota::codec::decode_traits<backend, T>::wire_type;
};

template <typename T>
using apply_decode_traits_t = typename apply_decode_traits<T>::type;

// Full cleaning: annotation -> optional -> smart_ptr -> decode_traits
template <typename T>
using deep_clean_t = apply_decode_traits_t<remove_smart_ptr_t<clean_t<T>>>;

template <typename T>
struct scalar_storage {
    using type = std::remove_cvref_t<T>;
};

template <>
struct scalar_storage<char> {
    using type = std::int8_t;
};

template <>
struct scalar_storage<std::byte> {
    using type = std::uint8_t;
};

template <>
struct scalar_storage<long double> {
    using type = double;
};

template <typename T>
    requires std::is_enum_v<T>
struct scalar_storage<T> {
    using type = std::underlying_type_t<T>;
};

template <typename T>
using scalar_storage_t = typename scalar_storage<std::remove_cvref_t<T>>::type;

template <typename Object>
consteval std::size_t field_slot_count() {
    return meta::virtual_schema<Object>::fields.size();
}

// Map a pointer-to-member to its slot loop-index in virtual_schema::slots —
// which is the same index the codec uses to derive voffsets. Skipped fields
// are absent from the slot list, so accessing a skipped member returns the
// slot count (a "not-found" sentinel).
//
// Computes offsets via a union-wrapped storage rather than a live Object,
// so it works for aggregates whose members have explicit default constructors.
template <typename Object, typename Member>
auto field_index(Member Object::* member) -> std::size_t {
    meta::detail::uninitialized<Object> storage;
    const auto base = reinterpret_cast<std::uintptr_t>(std::addressof(storage.value));
    const auto field = reinterpret_cast<std::uintptr_t>(std::addressof(storage.value.*member));
    const auto offset = static_cast<std::size_t>(field - base);

    constexpr auto& fields = meta::virtual_schema<Object>::fields;
    for(std::size_t i = 0; i < fields.size(); ++i) {
        if(fields[i].offset == offset) {
            return i;
        }
    }
    return fields.size();
}

// Slot-id helpers — delegate to the Deserializer's static helpers so the
// proxy and arena decode layer stay in sync. Errors are ignored here (the
// proxy uses unchecked reads and returns empty values on miss).
// Sentinel slot that FlatBuffers' GetOptionalFieldOffset treats as "absent"
// (any voffset >= vtable_size → returns 0).
constexpr inline slot_id invalid_slot = std::numeric_limits<slot_id>::max();

inline auto field_slot(std::size_t index) -> slot_id {
    auto r = backend::field_slot_id(index);
    return r.has_value() ? *r : invalid_slot;
}

inline auto variant_tag_slot() -> slot_id {
    return backend::variant_tag_slot_id();
}

inline auto variant_payload_slot(std::size_t index) -> slot_id {
    auto r = backend::variant_payload_slot_id(index);
    return r.has_value() ? *r : invalid_slot;
}

template <typename Element,
          typename CleanElement = clean_t<Element>,
          bool IsScalarLike = std::same_as<CleanElement, std::byte> ||
                              codec::bool_like<CleanElement> || codec::int_like<CleanElement> ||
                              codec::uint_like<CleanElement> ||
                              codec::floating_like<CleanElement> ||
                              codec::char_like<CleanElement> || std::is_enum_v<CleanElement>,
          bool IsString = is_string_like_v<CleanElement>,
          bool IsInlineStruct = can_inline_struct_v<CleanElement>>
struct array_vector_ptr_impl {
    using type = const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::Table>>*;
};

template <typename Element, typename CleanElement, bool IsString, bool IsInlineStruct>
struct array_vector_ptr_impl<Element, CleanElement, true, IsString, IsInlineStruct> {
    using type = const ::flatbuffers::Vector<scalar_storage_t<CleanElement>>*;
};

template <typename Element, typename CleanElement, bool IsInlineStruct>
struct array_vector_ptr_impl<Element, CleanElement, false, true, IsInlineStruct> {
    using type = const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::String>>*;
};

template <typename Element, typename CleanElement>
struct array_vector_ptr_impl<Element, CleanElement, false, false, true> {
    using type = const ::flatbuffers::Vector<const CleanElement*>*;
};

template <typename Element>
struct array_vector_ptr : array_vector_ptr_impl<Element> {};

template <typename Element>
using array_vector_ptr_t = typename array_vector_ptr<Element>::type;

// Helper to detect map-like ranges (has key_type + mapped_type)
template <typename T>
constexpr bool is_map_range_v = [] {
    if constexpr(is_range_like_v<T>) {
        return kota::format_kind<T> == kota::range_format::map;
    } else {
        return false;
    }
}();

// Forward declaration of the return type resolver for a given type T read from a flatbuffer field.
// Used by variant_view, tuple_view, and table_view to avoid duplicating dispatch logic.
template <typename T, typename = void>
struct field_return_type;

template <typename T>
using field_return_type_t = typename field_return_type<T>::type;

// Extract variant alternatives as a variant_view
template <typename T>
struct variant_view_for;

template <typename... Ts>
struct variant_view_for<std::variant<Ts...>> {
    using type = variant_view<Ts...>;
};

template <typename T>
using variant_view_for_t = typename variant_view_for<T>::type;

// Extract tuple elements as a tuple_view
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

template <typename T>
using tuple_view_for_t = typename tuple_view_for<T>::type;

// Extract map key/value for map_view
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

// field_return_type: determines the proxy return type for a given clean type T.
// Uses partial specialization to avoid eagerly instantiating type aliases for non-matching
// branches.
template <typename T, typename>
struct field_return_type {
    // Default: reflectable struct -> table_view
    using type = table_view<T>;
};

template <typename T>
struct field_return_type<T, std::enable_if_t<is_string_like_v<T>>> {
    using type = std::string_view;
};

template <typename T>
struct field_return_type<
    T,
    std::enable_if_t<!is_string_like_v<T> && (is_scalar_v<T> || can_inline_struct_v<T>)>> {
    using type = T;
};

template <typename T>
struct field_return_type<T, std::enable_if_t<is_specialization_of<std::variant, T>>> {
    using type = variant_view_for_t<T>;
};

template <typename T>
struct field_return_type<
    T,
    std::enable_if_t<!is_specialization_of<std::variant, T> && (is_pair_v<T> || is_tuple_v<T>)>> {
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
                     !is_specialization_of<std::variant, T> && !is_pair_v<T> && !is_tuple_v<T> &&
                     !is_map_range_v<T> && is_range_like_v<T>>> {
    using type = array_view<clean_t<std::ranges::range_value_t<T>>>;
};

template <typename Member,
          typename CleanMember = deep_clean_t<Member>,
          bool IsRange = is_range_like_v<CleanMember>>
struct member_return_impl;

// Map-like ranges get map_view
template <typename Member, typename CleanMember>
    requires is_map_range_v<CleanMember>
struct member_return_impl<Member, CleanMember, true> {
    using type = map_view_for_t<CleanMember>;
};

// Non-map ranges get array_view
template <typename Member, typename CleanMember>
    requires (!is_map_range_v<CleanMember>)
struct member_return_impl<Member, CleanMember, true> {
    using type = array_view<clean_t<std::ranges::range_value_t<CleanMember>>>;
};

template <typename Member, typename CleanMember>
struct member_return_impl<Member, CleanMember, false> {
    using type = field_return_type_t<CleanMember>;
};

template <typename Member>
struct member_return : member_return_impl<Member> {};

template <typename Member>
using member_return_t = typename member_return<Member>::type;

template <typename Element>
struct array_element_return {
private:
    using clean_element_t = deep_clean_t<Element>;

public:
    using type = field_return_type_t<clean_element_t>;
};

template <typename Element>
using array_element_return_t = typename array_element_return<Element>::type;

// Shared helper: read a typed value from a TableView at a given slot id.
// Returns the appropriate proxy type (scalar by value, string_view, table_view, etc.).
// Uses the underlying flatbuffers pointer (via view.raw()) for the actual read —
// the proxy deliberately uses unchecked semantics (returns {} on miss) rather than
// the Deserializer's expected-returning accessors.
template <typename T>
auto read_field(table_view_type view, slot_id field) -> field_return_type_t<T> {
    using return_t = field_return_type_t<T>;

    const auto* table = view.raw();

    if constexpr(std::same_as<T, std::byte>) {
        return std::byte{view.template get_scalar<std::uint8_t>(field)};
    } else if constexpr(std::is_enum_v<T>) {
        using storage_t = std::underlying_type_t<T>;
        return static_cast<T>(view.template get_scalar<storage_t>(field));
    } else if constexpr(codec::char_like<T>) {
        return static_cast<T>(view.template get_scalar<std::int8_t>(field));
    } else if constexpr(codec::bool_like<T> || codec::int_like<T> || codec::uint_like<T>) {
        return view.template get_scalar<T>(field);
    } else if constexpr(codec::floating_like<T>) {
        if constexpr(std::same_as<T, float> || std::same_as<T, double>) {
            return view.template get_scalar<T>(field);
        } else {
            return static_cast<T>(view.template get_scalar<double>(field));
        }
    } else if constexpr(is_string_like_v<T>) {
        const auto* text = table->template GetPointer<const ::flatbuffers::String*>(field);
        if(text == nullptr) {
            return {};
        }
        return std::string_view(text->data(), text->size());
    } else if constexpr(can_inline_struct_v<T>) {
        const auto* value = table->template GetStruct<const T*>(field);
        if(value == nullptr) {
            return {};
        }
        return *value;
    } else if constexpr(is_specialization_of<std::variant, T>) {
        const auto* nested = table->template GetPointer<const ::flatbuffers::Table*>(field);
        return return_t(table_view_type(nested));
    } else if constexpr(is_pair_v<T> || is_tuple_v<T>) {
        const auto* nested = table->template GetPointer<const ::flatbuffers::Table*>(field);
        return return_t(table_view_type(nested));
    } else if constexpr(is_map_range_v<T>) {
        const auto* vec = table->template GetPointer<
            const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::Table>>*>(field);
        return return_t(vec);
    } else if constexpr(is_range_like_v<T>) {
        using element_type = clean_t<std::ranges::range_value_t<T>>;
        using vector_ptr_t = array_vector_ptr_t<element_type>;
        const auto* value = table->template GetPointer<vector_ptr_t>(field);
        return return_t(value);
    } else {
        // reflectable struct -> table_view
        const auto* nested = table->template GetPointer<const ::flatbuffers::Table*>(field);
        return return_t(table_view_type(nested));
    }
}

}  // namespace proxy_detail

template <typename Element>
class array_view {
public:
    using element_type = proxy_detail::deep_clean_t<Element>;
    using value_type = proxy_detail::array_element_return_t<element_type>;
    using vector_ptr_type = proxy_detail::array_vector_ptr_t<element_type>;

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
        if(!valid() || index >= size()) {
            return value_type{};
        }

        if constexpr(std::same_as<element_type, std::byte>) {
            return std::byte{vector->Get(static_cast<::flatbuffers::uoffset_t>(index))};
        } else if constexpr(std::is_enum_v<element_type>) {
            using storage_t = proxy_detail::scalar_storage_t<element_type>;
            return static_cast<element_type>(
                static_cast<storage_t>(vector->Get(static_cast<::flatbuffers::uoffset_t>(index))));
        } else if constexpr(codec::char_like<element_type>) {
            return static_cast<char>(vector->Get(static_cast<::flatbuffers::uoffset_t>(index)));
        } else if constexpr(codec::bool_like<element_type> || codec::int_like<element_type> ||
                            codec::uint_like<element_type>) {
            return static_cast<element_type>(
                vector->Get(static_cast<::flatbuffers::uoffset_t>(index)));
        } else if constexpr(codec::floating_like<element_type>) {
            return static_cast<element_type>(
                vector->Get(static_cast<::flatbuffers::uoffset_t>(index)));
        } else if constexpr(proxy_detail::is_string_like_v<element_type>) {
            const auto* text = vector->GetAsString(static_cast<::flatbuffers::uoffset_t>(index));
            if(text == nullptr) {
                return {};
            }
            return std::string_view(text->data(), text->size());
        } else if constexpr(can_inline_struct_v<element_type>) {
            const auto* value = vector->Get(static_cast<::flatbuffers::uoffset_t>(index));
            if(value == nullptr) {
                return {};
            }
            return *value;
        } else {
            const auto* nested = vector->template GetAs<::flatbuffers::Table>(
                static_cast<::flatbuffers::uoffset_t>(index));
            return value_type(proxy_detail::table_view_type(nested));
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
    using view_type = proxy_detail::table_view_type;

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
            view.template get_scalar<std::uint32_t>(proxy_detail::variant_tag_slot()));
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

        return proxy_detail::read_field<clean_alt_t>(view, proxy_detail::variant_payload_slot(I));
    }

    constexpr auto raw() const noexcept -> const ::flatbuffers::Table* {
        return view.raw();
    }

private:
    view_type view;
};

template <typename... Ts>
class tuple_view {
public:
    using view_type = proxy_detail::table_view_type;

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

        return proxy_detail::read_field<clean_element_t>(view, proxy_detail::field_slot(I));
    }

    constexpr auto raw() const noexcept -> const ::flatbuffers::Table* {
        return view.raw();
    }

private:
    view_type view;
};

template <typename K, typename V>
class map_view {
public:
    using vector_type = const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::Table>>*;

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
        const auto* entry = vector->template GetAs<::flatbuffers::Table>(
            static_cast<::flatbuffers::uoffset_t>(index));
        return tuple_view<K, V>(proxy_detail::table_view_type(entry));
    }

    template <typename U = K>
        requires std::totally_ordered_with<
            proxy_detail::field_return_type_t<proxy_detail::deep_clean_t<K>>,
            const U&>
    auto operator[](const U& key) const
        -> proxy_detail::field_return_type_t<proxy_detail::deep_clean_t<V>> {
        using clean_v = proxy_detail::deep_clean_t<V>;
        using value_return_t = proxy_detail::field_return_type_t<clean_v>;

        auto entry = find_entry(key);
        if(!entry.valid()) {
            return value_return_t{};
        }
        return proxy_detail::read_field<clean_v>(entry, proxy_detail::field_slot(1));
    }

    template <typename U = K>
        requires std::totally_ordered_with<
            proxy_detail::field_return_type_t<proxy_detail::deep_clean_t<K>>,
            const U&>
    auto find(const U& key) const -> std::optional<tuple_view<K, V>> {
        auto entry = find_entry(key);
        if(!entry.valid()) {
            return std::nullopt;
        }
        return tuple_view<K, V>(entry);
    }

    template <typename U = K>
        requires std::totally_ordered_with<
            proxy_detail::field_return_type_t<proxy_detail::deep_clean_t<K>>,
            const U&>
    auto contains(const U& key) const -> bool {
        return find_entry(key).valid();
    }

    constexpr auto raw() const noexcept -> vector_type {
        return vector;
    }

private:
    template <typename U>
    auto find_entry(const U& key) const -> proxy_detail::table_view_type {
        using clean_k = proxy_detail::deep_clean_t<K>;

        if(!valid()) {
            return {};
        }

        std::size_t lo = 0;
        std::size_t hi = size();
        while(lo < hi) {
            auto mid = lo + (hi - lo) / 2;
            const auto* entry = vector->template GetAs<::flatbuffers::Table>(
                static_cast<::flatbuffers::uoffset_t>(mid));
            auto entry_key = proxy_detail::read_field<clean_k>(proxy_detail::table_view_type(entry),
                                                               proxy_detail::field_slot(0));
            if(entry_key < key) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        if(lo >= size()) {
            return {};
        }

        const auto* entry =
            vector->template GetAs<::flatbuffers::Table>(static_cast<::flatbuffers::uoffset_t>(lo));
        auto entry_view = proxy_detail::table_view_type(entry);
        auto entry_key = proxy_detail::read_field<clean_k>(entry_view, proxy_detail::field_slot(0));
        if(entry_key == key) {
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
    using view_type = proxy_detail::table_view_type;

    constexpr table_view() = default;

    constexpr explicit table_view(view_type view) noexcept : view(view) {}

    static auto from_bytes(std::span<const std::uint8_t> bytes) -> table_view {
        if(bytes.empty()) {
            return {};
        }
        return table_view(view_type(::flatbuffers::GetRoot<::flatbuffers::Table>(bytes.data())));
    }

    static auto from_bytes(std::span<const std::byte> bytes) -> table_view {
        if(bytes.empty()) {
            return {};
        }
        const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
        return table_view(view_type(::flatbuffers::GetRoot<::flatbuffers::Table>(data)));
    }

    constexpr auto valid() const noexcept -> bool {
        return view.valid();
    }

    constexpr explicit operator bool() const noexcept {
        return valid();
    }

    constexpr auto raw() const noexcept -> const ::flatbuffers::Table* {
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
        return view.has(proxy_detail::field_slot(index));
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

        return proxy_detail::read_field<member_type>(view, proxy_detail::field_slot(index));
    }

private:
    view_type view;
};

}  // namespace kota::codec::flatbuffers
