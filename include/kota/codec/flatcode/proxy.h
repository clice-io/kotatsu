#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/meta/schema.h"
#include "kota/codec/codec.h"
#include "kota/codec/flatcode/deserializer.h"

namespace kota::codec::flatcode {

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
// TableView + slot-id abstraction. All reads use the buffer/offset
// accessors exposed by TableView rather than going through the
// expected-returning accessors of Deserializer (the proxy uses unchecked
// semantics, returning empty values on miss).
using backend = Deserializer<>;
using table_view_type = backend::TableView;
using slot_id = backend::slot_id;

using codec::detail::clean_t;
using codec::detail::remove_annotation_t;
using codec::detail::remove_optional_t;

template <typename T>
constexpr bool is_string_like_v = codec::str_like<T>;

template <typename T>
constexpr bool is_range_like_v = std::ranges::input_range<T> && !is_string_like_v<T>;

template <typename T>
constexpr bool is_scalar_v =
    codec::bool_like<T> || codec::int_like<T> || codec::uint_like<T> || codec::floating_like<T> ||
    codec::char_like<T> || std::is_enum_v<T> || std::same_as<T, std::byte>;

// Smart pointer stripping (unique_ptr / shared_ptr — applied after clean_t).
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
consteval std::size_t field_slot_count() {
    return meta::virtual_schema<Object>::fields.size();
}

// Map a pointer-to-member to its slot loop-index in virtual_schema::slots —
// which is the same index the codec uses to derive slot ids. Skipped fields
// are absent from the slot list, so accessing a skipped member returns the
// slot count (a "not-found" sentinel).
template <typename Object, typename Member>
auto field_index(Member Object::* member) -> std::size_t {
    static_assert(std::default_initializable<Object>,
                  "table_view member access requires default-constructible object type");

    Object sample{};
    const auto base = reinterpret_cast<std::uintptr_t>(std::addressof(sample));
    const auto field = reinterpret_cast<std::uintptr_t>(std::addressof(sample.*member));
    const auto offset = static_cast<std::size_t>(field - base);

    constexpr auto& fields = meta::virtual_schema<Object>::fields;
    for(std::size_t i = 0; i < fields.size(); ++i) {
        if(fields[i].offset == offset) {
            return i;
        }
    }
    return fields.size();
}

inline auto field_slot(std::size_t index) -> slot_id {
    auto r = backend::field_slot_id(index);
    return r.has_value() ? *r : slot_id{0};
}

inline auto variant_tag_slot() -> slot_id {
    return backend::variant_tag_slot_id();
}

inline auto variant_payload_slot(std::size_t index) -> slot_id {
    auto r = backend::variant_payload_slot_id(index);
    return r.has_value() ? *r : slot_id{0};
}

// Helper to detect map-like ranges (has key_type + mapped_type).
template <typename T>
constexpr bool is_map_range_v = [] {
    if constexpr(is_range_like_v<T>) {
        return kota::format_kind<T> == kota::range_format::map;
    } else {
        return false;
    }
}();

// --- return type resolution -------------------------------------------------

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
    std::enable_if_t<!is_string_like_v<T> && (is_scalar_v<T> || detail::is_inline_struct_v<T>)>> {
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
    std::enable_if_t<!is_string_like_v<T> && !is_scalar_v<T> && !detail::is_inline_struct_v<T> &&
                     !is_specialization_of<std::variant, T> && !is_pair_v<T> && !is_tuple_v<T> &&
                     !is_map_range_v<T> && is_range_like_v<T>>> {
    using type = array_view<clean_t<std::ranges::range_value_t<T>>>;
};

template <typename Member,
          typename CleanMember = deep_clean_t<Member>,
          bool IsRange = is_range_like_v<CleanMember>>
struct member_return_impl;

template <typename Member, typename CleanMember>
    requires is_map_range_v<CleanMember>
struct member_return_impl<Member, CleanMember, true> {
    using type = map_view_for_t<CleanMember>;
};

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

// --- low-level readers ------------------------------------------------------

// Read the absolute uint32 offset stored at a slot. Returns 0 on miss.
inline auto read_slot_offset(table_view_type view, slot_id sid) -> std::uint32_t {
    if(!view.valid() || !view.has(sid)) {
        return 0;
    }
    const auto abs_off = view.slot_absolute_offset(sid);
    if(abs_off == 0 || static_cast<std::size_t>(abs_off) + 4U > view.buffer_size()) {
        return 0;
    }
    std::uint32_t off = 0;
    std::memcpy(&off, view.buffer_base() + abs_off, 4);
    return off;
}

// Resolve a vector slot to (elements_ptr, count) in absolute buffer coords.
// Applies element alignment padding after the count prefix. Returns nulls on
// miss or out-of-bounds.
template <typename T>
inline auto resolve_vector_elements(table_view_type view, slot_id sid)
    -> std::pair<const std::uint8_t*, std::size_t> {
    const auto vec_off = read_slot_offset(view, sid);
    if(vec_off == 0) {
        return {nullptr, 0};
    }
    const auto buffer_size = view.buffer_size();
    if(vec_off + 4U > buffer_size) {
        return {nullptr, 0};
    }
    std::uint32_t count = 0;
    std::memcpy(&count, view.buffer_base() + vec_off, 4);

    std::size_t elements_off = static_cast<std::size_t>(vec_off) + 4U;
    constexpr std::size_t a = alignof(T);
    if constexpr(a > 1) {
        while(elements_off % a != 0U) {
            ++elements_off;
        }
    }
    if(elements_off + static_cast<std::size_t>(count) * sizeof(T) > buffer_size) {
        return {nullptr, 0};
    }
    return {view.buffer_base() + elements_off, static_cast<std::size_t>(count)};
}

// Shared helper: read a typed value from a TableView at a given slot id.
// Returns the appropriate proxy type. Uses unchecked semantics — missing or
// out-of-bounds slots yield a default-constructed return value.
template <typename T>
auto read_field(table_view_type view, slot_id field) -> field_return_type_t<T> {
    using return_t = field_return_type_t<T>;

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
        const auto off = read_slot_offset(view, field);
        if(off == 0 || off + 4U > view.buffer_size()) {
            return {};
        }
        std::uint32_t len = 0;
        std::memcpy(&len, view.buffer_base() + off, 4);
        if(off + 4U + len > view.buffer_size()) {
            return {};
        }
        return std::string_view(reinterpret_cast<const char*>(view.buffer_base() + off + 4U), len);
    } else if constexpr(detail::is_inline_struct_v<T>) {
        if(!view.valid() || !view.has(field)) {
            return T{};
        }
        const auto abs_off = view.slot_absolute_offset(field);
        if(static_cast<std::size_t>(abs_off) + sizeof(T) > view.buffer_size()) {
            return T{};
        }
        T value{};
        std::memcpy(&value, view.buffer_base() + abs_off, sizeof(T));
        return value;
    } else if constexpr(is_specialization_of<std::variant, T> || is_pair_v<T> || is_tuple_v<T>) {
        const auto off = read_slot_offset(view, field);
        if(off == 0) {
            return return_t{};
        }
        return return_t(table_view_type(view.buffer_base(), view.buffer_size(), off));
    } else if constexpr(is_map_range_v<T>) {
        const auto off = read_slot_offset(view, field);
        if(off == 0) {
            return return_t{};
        }
        return return_t(view.buffer_base(), view.buffer_size(), off);
    } else if constexpr(is_range_like_v<T>) {
        const auto off = read_slot_offset(view, field);
        if(off == 0) {
            return return_t{};
        }
        return return_t(view.buffer_base(), view.buffer_size(), off);
    } else {
        // reflectable struct -> table_view
        const auto off = read_slot_offset(view, field);
        if(off == 0) {
            return return_t{};
        }
        return return_t(table_view_type(view.buffer_base(), view.buffer_size(), off));
    }
}

}  // namespace proxy_detail

// === array_view ============================================================

template <typename Element>
class array_view {
public:
    using element_type = proxy_detail::deep_clean_t<Element>;
    using value_type = proxy_detail::array_element_return_t<element_type>;

    constexpr array_view() = default;

    array_view(const std::uint8_t* buffer_base,
               std::size_t buffer_size,
               std::uint32_t vec_offset) noexcept :
        base(buffer_base), base_size(buffer_size), vec_off(vec_offset) {}

    auto valid() const noexcept -> bool {
        return base != nullptr && vec_off != 0 &&
               static_cast<std::size_t>(vec_off) + 4U <= base_size;
    }

    explicit operator bool() const noexcept {
        return valid();
    }

    auto size() const noexcept -> std::size_t {
        if(!valid()) {
            return 0;
        }
        std::uint32_t count = 0;
        std::memcpy(&count, base + vec_off, 4);
        return count;
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
            return std::byte{read_inline_scalar<std::uint8_t>(index)};
        } else if constexpr(std::is_enum_v<element_type>) {
            using storage_t = proxy_detail::scalar_storage_t<element_type>;
            return static_cast<element_type>(read_inline_scalar<storage_t>(index));
        } else if constexpr(codec::char_like<element_type>) {
            return static_cast<element_type>(read_inline_scalar<std::int8_t>(index));
        } else if constexpr(codec::bool_like<element_type> || codec::int_like<element_type> ||
                            codec::uint_like<element_type>) {
            return read_inline_scalar<element_type>(index);
        } else if constexpr(codec::floating_like<element_type>) {
            if constexpr(std::same_as<element_type, float> || std::same_as<element_type, double>) {
                return read_inline_scalar<element_type>(index);
            } else {
                return static_cast<element_type>(read_inline_scalar<double>(index));
            }
        } else if constexpr(proxy_detail::is_string_like_v<element_type>) {
            const auto off = read_offset_element(index);
            if(off == 0 || off + 4U > base_size) {
                return {};
            }
            std::uint32_t len = 0;
            std::memcpy(&len, base + off, 4);
            if(off + 4U + len > base_size) {
                return {};
            }
            return std::string_view(reinterpret_cast<const char*>(base + off + 4U), len);
        } else if constexpr(detail::is_inline_struct_v<element_type>) {
            element_type value{};
            const auto* p = elements_ptr<element_type>() + index * sizeof(element_type);
            if(p == nullptr) {
                return {};
            }
            std::memcpy(&value, p, sizeof(element_type));
            return value;
        } else {
            // Default: nested table (includes variant / tuple / pair / reflectable).
            const auto off = read_offset_element(index);
            if(off == 0) {
                return value_type{};
            }
            return value_type(proxy_detail::table_view_type(base, base_size, off));
        }
    }

private:
    // Pointer to the aligned element array for inline/scalar storage.
    template <typename T>
    auto elements_ptr() const -> const std::uint8_t* {
        if(!valid()) {
            return nullptr;
        }
        std::size_t off = static_cast<std::size_t>(vec_off) + 4U;
        constexpr std::size_t a = alignof(T);
        if constexpr(a > 1) {
            while(off % a != 0U) {
                ++off;
            }
        }
        if(off + size() * sizeof(T) > base_size) {
            return nullptr;
        }
        return base + off;
    }

    template <typename T>
    auto read_inline_scalar(std::size_t index) const -> T {
        const auto* p = elements_ptr<T>();
        if(p == nullptr) {
            return T{};
        }
        T value{};
        std::memcpy(&value, p + index * sizeof(T), sizeof(T));
        return value;
    }

    // For string/table vectors: element at position N is a uint32 offset at
    // `vec_off + 4 + index*4` (no extra alignment padding — offsets are uint32).
    auto read_offset_element(std::size_t index) const -> std::uint32_t {
        const std::size_t off = static_cast<std::size_t>(vec_off) + 4U + index * 4U;
        if(off + 4U > base_size) {
            return 0;
        }
        std::uint32_t result = 0;
        std::memcpy(&result, base + off, 4);
        return result;
    }

    const std::uint8_t* base = nullptr;
    std::size_t base_size = 0;
    std::uint32_t vec_off = 0;
};

// === variant_view ==========================================================

template <typename... Ts>
class variant_view {
public:
    using view_type = proxy_detail::table_view_type;

    constexpr variant_view() = default;

    constexpr explicit variant_view(view_type view) noexcept : view(view) {}

    auto valid() const noexcept -> bool {
        return view.valid();
    }

    explicit operator bool() const noexcept {
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

    auto raw() const noexcept -> view_type {
        return view;
    }

private:
    view_type view;
};

// === tuple_view ============================================================

template <typename... Ts>
class tuple_view {
public:
    using view_type = proxy_detail::table_view_type;

    constexpr tuple_view() = default;

    constexpr explicit tuple_view(view_type view) noexcept : view(view) {}

    auto valid() const noexcept -> bool {
        return view.valid();
    }

    explicit operator bool() const noexcept {
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

    auto raw() const noexcept -> view_type {
        return view;
    }

private:
    view_type view;
};

// === map_view ==============================================================

template <typename K, typename V>
class map_view {
public:
    constexpr map_view() = default;

    map_view(const std::uint8_t* buffer_base,
             std::size_t buffer_size,
             std::uint32_t vec_offset) noexcept :
        base(buffer_base), base_size(buffer_size), vec_off(vec_offset) {}

    auto valid() const noexcept -> bool {
        return base != nullptr && vec_off != 0 &&
               static_cast<std::size_t>(vec_off) + 4U <= base_size;
    }

    explicit operator bool() const noexcept {
        return valid();
    }

    auto size() const noexcept -> std::size_t {
        if(!valid()) {
            return 0;
        }
        std::uint32_t count = 0;
        std::memcpy(&count, base + vec_off, 4);
        return count;
    }

    auto empty() const noexcept -> bool {
        return size() == 0U;
    }

    auto at(std::size_t index) const -> tuple_view<K, V> {
        if(!valid() || index >= size()) {
            return {};
        }
        return tuple_view<K, V>(entry_view(index));
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

private:
    auto entry_view(std::size_t index) const -> proxy_detail::table_view_type {
        const std::size_t off_pos = static_cast<std::size_t>(vec_off) + 4U + index * 4U;
        if(off_pos + 4U > base_size) {
            return {};
        }
        std::uint32_t entry_off = 0;
        std::memcpy(&entry_off, base + off_pos, 4);
        if(entry_off == 0 || entry_off >= base_size) {
            return {};
        }
        return proxy_detail::table_view_type(base, base_size, entry_off);
    }

    template <typename U>
    auto find_entry(const U& key) const -> proxy_detail::table_view_type {
        using clean_k = proxy_detail::deep_clean_t<K>;

        if(!valid()) {
            return {};
        }

        const auto n = size();
        std::size_t lo = 0;
        std::size_t hi = n;
        while(lo < hi) {
            auto mid = lo + (hi - lo) / 2;
            auto entry = entry_view(mid);
            auto entry_key = proxy_detail::read_field<clean_k>(entry, proxy_detail::field_slot(0));
            if(entry_key < key) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        if(lo >= n) {
            return {};
        }

        auto entry = entry_view(lo);
        auto entry_key = proxy_detail::read_field<clean_k>(entry, proxy_detail::field_slot(0));
        if(entry_key == key) {
            return entry;
        }
        return {};
    }

    const std::uint8_t* base = nullptr;
    std::size_t base_size = 0;
    std::uint32_t vec_off = 0;
};

// === table_view ============================================================

template <typename T>
class table_view {
public:
    using object_type = std::remove_cvref_t<T>;
    using view_type = proxy_detail::table_view_type;

    constexpr table_view() = default;

    constexpr explicit table_view(view_type view) noexcept : view(view) {}

    // Parse a complete flatcode buffer and return a table_view of the root.
    static auto from_bytes(std::span<const std::uint8_t> bytes) -> table_view {
        if(bytes.size() < detail::header_size) {
            return {};
        }
        std::uint32_t magic = 0;
        std::memcpy(&magic, bytes.data(), 4);
        if(magic != detail::magic_evfc) {
            return {};
        }
        std::uint32_t root_off = 0;
        std::memcpy(&root_off, bytes.data() + 4, 4);
        if(root_off == 0 || root_off >= bytes.size()) {
            return {};
        }
        return table_view(view_type(bytes.data(), bytes.size(), root_off));
    }

    static auto from_bytes(std::span<const std::byte> bytes) -> table_view {
        return from_bytes(
            std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                          bytes.size()));
    }

    auto valid() const noexcept -> bool {
        return view.valid();
    }

    explicit operator bool() const noexcept {
        return valid();
    }

    auto raw() const noexcept -> view_type {
        return view;
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

}  // namespace kota::codec::flatcode
