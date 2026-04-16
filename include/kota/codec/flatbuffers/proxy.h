#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
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

#include "kota/codec/flatbuffers/schema.h"
#include "kota/codec/serde.h"

#if __has_include(<flatbuffers/flatbuffers.h>)
#include <flatbuffers/flatbuffers.h>
#else
#error                                                                                             \
    "flatbuffers/flatbuffers.h not found. Enable KOTA_SERDE_ENABLE_FLATBUFFERS or add flatbuffers include paths."
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

constexpr ::flatbuffers::voffset_t first_field = 4;
constexpr ::flatbuffers::voffset_t field_step = 2;

using serde::detail::remove_annotation_t;
using serde::detail::remove_optional_t;
using serde::detail::clean_t;

template <typename T>
constexpr bool is_string_like_v = serde::str_like<T>;

template <typename T>
constexpr bool is_range_like_v = std::ranges::input_range<T> && !is_string_like_v<T>;

template <typename T>
constexpr bool is_scalar_v =
    serde::bool_like<T> || serde::int_like<T> || serde::uint_like<T> || serde::floating_like<T> ||
    serde::char_like<T> || std::is_enum_v<T> || std::same_as<T, std::byte>;

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

// Full cleaning: annotation -> optional -> smart_ptr
template <typename T>
using deep_clean_t = remove_smart_ptr_t<clean_t<T>>;

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
consteval auto field_offsets() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
        return std::array<std::size_t, sizeof...(I)>{refl::field_offset<Object>(I)...};
    }(std::make_index_sequence<refl::field_count<Object>()>{});
}

template <typename Object, typename Member>
auto field_index(Member Object::* member) -> std::size_t {
    static_assert(std::default_initializable<Object>,
                  "table_view member access requires default-constructible object type");

    Object sample{};
    const auto base = reinterpret_cast<std::uintptr_t>(std::addressof(sample));
    const auto field = reinterpret_cast<std::uintptr_t>(std::addressof(sample.*member));
    const auto offset = static_cast<std::size_t>(field - base);

    constexpr auto offsets = field_offsets<Object>();
    for(std::size_t i = 0; i < offsets.size(); ++i) {
        if(offsets[i] == offset) {
            return i;
        }
    }
    return offsets.size();
}

inline auto voffset(std::size_t index) -> ::flatbuffers::voffset_t {
    return static_cast<::flatbuffers::voffset_t>(static_cast<std::size_t>(first_field) +
                                                 index * static_cast<std::size_t>(field_step));
}

template <typename Element,
          typename CleanElement = clean_t<Element>,
          bool IsScalarLike = std::same_as<CleanElement, std::byte> ||
                              serde::bool_like<CleanElement> || serde::int_like<CleanElement> ||
                              serde::uint_like<CleanElement> ||
                              serde::floating_like<CleanElement> ||
                              serde::char_like<CleanElement> || std::is_enum_v<CleanElement>,
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

// Shared helper: read a typed value from a flatbuffers::Table at a given voffset.
// Returns the appropriate proxy type (scalar by value, string_view, table_view, etc.)
template <typename T>
auto read_field(const std::uint8_t* root,
                const ::flatbuffers::Table* table,
                ::flatbuffers::voffset_t field) -> field_return_type_t<T> {
    using return_t = field_return_type_t<T>;

    if constexpr(std::same_as<T, std::byte>) {
        return std::byte{table->GetField<std::uint8_t>(field, std::uint8_t{})};
    } else if constexpr(std::is_enum_v<T>) {
        using storage_t = std::underlying_type_t<T>;
        return static_cast<T>(table->GetField<storage_t>(field, storage_t{}));
    } else if constexpr(serde::char_like<T>) {
        return static_cast<T>(table->GetField<std::int8_t>(field, std::int8_t{}));
    } else if constexpr(serde::bool_like<T> || serde::int_like<T> || serde::uint_like<T>) {
        return table->GetField<T>(field, T{});
    } else if constexpr(serde::floating_like<T>) {
        if constexpr(std::same_as<T, float> || std::same_as<T, double>) {
            return table->GetField<T>(field, T{});
        } else {
            return static_cast<T>(table->GetField<double>(field, 0.0));
        }
    } else if constexpr(is_string_like_v<T>) {
        const auto* text = table->GetPointer<const ::flatbuffers::String*>(field);
        if(text == nullptr) {
            return {};
        }
        return std::string_view(text->data(), text->size());
    } else if constexpr(can_inline_struct_v<T>) {
        const auto* value = table->GetStruct<const T*>(field);
        if(value == nullptr) {
            return {};
        }
        return *value;
    } else if constexpr(is_specialization_of<std::variant, T>) {
        const auto* nested = table->GetPointer<const ::flatbuffers::Table*>(field);
        return return_t(root, nested);
    } else if constexpr(is_pair_v<T> || is_tuple_v<T>) {
        const auto* nested = table->GetPointer<const ::flatbuffers::Table*>(field);
        return return_t(root, nested);
    } else if constexpr(is_map_range_v<T>) {
        const auto* vec = table->GetPointer<
            const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::Table>>*>(field);
        return return_t(root, vec);
    } else if constexpr(is_range_like_v<T>) {
        using element_type = clean_t<std::ranges::range_value_t<T>>;
        using vector_ptr_t = array_vector_ptr_t<element_type>;
        const auto* value = table->GetPointer<vector_ptr_t>(field);
        return return_t(root, value);
    } else {
        // reflectable struct -> table_view
        const auto* nested = table->GetPointer<const ::flatbuffers::Table*>(field);
        return return_t(root, nested);
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

    constexpr array_view(const std::uint8_t* root, vector_ptr_type vector) noexcept :
        root(root), vector(vector) {}

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
        } else if constexpr(serde::char_like<element_type>) {
            return static_cast<char>(vector->Get(static_cast<::flatbuffers::uoffset_t>(index)));
        } else if constexpr(serde::bool_like<element_type> || serde::int_like<element_type> ||
                            serde::uint_like<element_type>) {
            return static_cast<element_type>(
                vector->Get(static_cast<::flatbuffers::uoffset_t>(index)));
        } else if constexpr(serde::floating_like<element_type>) {
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
            return value_type(root, nested);
        }
    }

    constexpr auto root_data() const noexcept -> const std::uint8_t* {
        return root;
    }

    constexpr auto raw() const noexcept -> vector_ptr_type {
        return vector;
    }

private:
    const std::uint8_t* root = nullptr;
    vector_ptr_type vector = nullptr;
};

template <typename... Ts>
class variant_view {
public:
    using table_type = ::flatbuffers::Table;

    constexpr variant_view() = default;

    constexpr variant_view(const std::uint8_t* root, const table_type* table) noexcept :
        root(root), table(table) {}

    constexpr auto valid() const noexcept -> bool {
        return table != nullptr;
    }

    constexpr explicit operator bool() const noexcept {
        return valid();
    }

    auto index() const -> std::size_t {
        if(!valid()) {
            return sizeof...(Ts);
        }
        return static_cast<std::size_t>(
            table->GetField<std::uint32_t>(proxy_detail::first_field, 0U));
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

        // variant field I is at voffset 6 + I*2 = first_field + (I+1) * field_step
        const auto field = proxy_detail::voffset(I + 1);
        return proxy_detail::read_field<clean_alt_t>(root, table, field);
    }

    constexpr auto root_data() const noexcept -> const std::uint8_t* {
        return root;
    }

    constexpr auto raw() const noexcept -> const table_type* {
        return table;
    }

private:
    const std::uint8_t* root = nullptr;
    const table_type* table = nullptr;
};

template <typename... Ts>
class tuple_view {
public:
    using table_type = ::flatbuffers::Table;

    constexpr tuple_view() = default;

    constexpr tuple_view(const std::uint8_t* root, const table_type* table) noexcept :
        root(root), table(table) {}

    constexpr auto valid() const noexcept -> bool {
        return table != nullptr;
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

        const auto field = proxy_detail::voffset(I);
        return proxy_detail::read_field<clean_element_t>(root, table, field);
    }

    constexpr auto root_data() const noexcept -> const std::uint8_t* {
        return root;
    }

    constexpr auto raw() const noexcept -> const table_type* {
        return table;
    }

private:
    const std::uint8_t* root = nullptr;
    const table_type* table = nullptr;
};

template <typename K, typename V>
class map_view {
public:
    using table_type = ::flatbuffers::Table;
    using vector_type = const ::flatbuffers::Vector<::flatbuffers::Offset<::flatbuffers::Table>>*;

    constexpr map_view() = default;

    constexpr map_view(const std::uint8_t* root, vector_type vector) noexcept :
        root(root), vector(vector) {}

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
        return tuple_view<K, V>(root, entry);
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
        if(entry == nullptr) {
            return value_return_t{};
        }
        return proxy_detail::read_field<clean_v>(root, entry, proxy_detail::voffset(1));
    }

    template <typename U = K>
        requires std::totally_ordered_with<
            proxy_detail::field_return_type_t<proxy_detail::deep_clean_t<K>>,
            const U&>
    auto find(const U& key) const -> std::optional<tuple_view<K, V>> {
        auto entry = find_entry(key);
        if(entry == nullptr) {
            return std::nullopt;
        }
        return tuple_view<K, V>(root, entry);
    }

    template <typename U = K>
        requires std::totally_ordered_with<
            proxy_detail::field_return_type_t<proxy_detail::deep_clean_t<K>>,
            const U&>
    auto contains(const U& key) const -> bool {
        return find_entry(key) != nullptr;
    }

    constexpr auto root_data() const noexcept -> const std::uint8_t* {
        return root;
    }

    constexpr auto raw() const noexcept -> vector_type {
        return vector;
    }

private:
    template <typename U>
    auto find_entry(const U& key) const -> const ::flatbuffers::Table* {
        using clean_k = proxy_detail::deep_clean_t<K>;

        if(!valid()) {
            return nullptr;
        }

        std::size_t lo = 0;
        std::size_t hi = size();
        while(lo < hi) {
            auto mid = lo + (hi - lo) / 2;
            const auto* entry = vector->template GetAs<::flatbuffers::Table>(
                static_cast<::flatbuffers::uoffset_t>(mid));
            auto entry_key =
                proxy_detail::read_field<clean_k>(root, entry, proxy_detail::first_field);
            if(entry_key < key) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        if(lo >= size()) {
            return nullptr;
        }

        const auto* entry =
            vector->template GetAs<::flatbuffers::Table>(static_cast<::flatbuffers::uoffset_t>(lo));
        auto entry_key = proxy_detail::read_field<clean_k>(root, entry, proxy_detail::first_field);
        if(entry_key == key) {
            return entry;
        }
        return nullptr;
    }

    const std::uint8_t* root = nullptr;
    vector_type vector = nullptr;
};

template <typename T>
class table_view {
public:
    using object_type = std::remove_cvref_t<T>;
    using table_type = ::flatbuffers::Table;

    constexpr table_view() = default;

    constexpr table_view(const std::uint8_t* root, const table_type* table) noexcept :
        root(root), table(table) {}

    static auto from_bytes(std::span<const std::uint8_t> bytes) -> table_view {
        if(bytes.empty()) {
            return {};
        }
        return table_view(bytes.data(), ::flatbuffers::GetRoot<table_type>(bytes.data()));
    }

    static auto from_bytes(std::span<const std::byte> bytes) -> table_view {
        if(bytes.empty()) {
            return {};
        }
        const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
        return table_view(data, ::flatbuffers::GetRoot<table_type>(data));
    }

    constexpr auto valid() const noexcept -> bool {
        return table != nullptr;
    }

    constexpr explicit operator bool() const noexcept {
        return valid();
    }

    constexpr auto root_data() const noexcept -> const std::uint8_t* {
        return root;
    }

    constexpr auto raw() const noexcept -> const table_type* {
        return table;
    }

    template <typename Member>
        requires refl::reflectable_class<object_type>
    auto has(Member object_type::* member) const -> bool {
        if(!valid()) {
            return false;
        }

        const auto index = proxy_detail::field_index(member);
        if(index >= refl::field_count<object_type>()) {
            return false;
        }
        return table->GetOptionalFieldOffset(proxy_detail::voffset(index)) != 0;
    }

    template <typename Member>
        requires refl::reflectable_class<object_type>
    auto operator[](Member object_type::* member) const -> proxy_detail::member_return_t<Member> {
        return (*this)(member);
    }

    template <typename Member>
        requires refl::reflectable_class<object_type>
    auto operator()(Member object_type::* member) const -> proxy_detail::member_return_t<Member> {
        using member_type = proxy_detail::deep_clean_t<Member>;
        using return_t = proxy_detail::member_return_t<Member>;

        if(!valid()) {
            return return_t{};
        }

        const auto index = proxy_detail::field_index(member);
        if(index >= refl::field_count<object_type>()) {
            return return_t{};
        }

        const auto field = proxy_detail::voffset(index);
        return proxy_detail::read_field<member_type>(root, table, field);
    }

private:
    const std::uint8_t* root = nullptr;
    const table_type* table = nullptr;
};

}  // namespace kota::codec::flatbuffers
