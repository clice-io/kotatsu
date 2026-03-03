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
#include <type_traits>
#include <utility>
#include <vector>

#include "eventide/serde/flatbuffers/binary/schema.h"
#include "eventide/serde/serde.h"

#if __has_include(<flatbuffers/flatbuffers.h>)
#include <flatbuffers/flatbuffers.h>
#else
#error                                                                                             \
    "flatbuffers/flatbuffers.h not found. Enable EVENTIDE_SERDE_ENABLE_FLATBUFFERS or add flatbuffers include paths."
#endif

namespace eventide::serde::flatbuffers::binary {

template <typename T>
class table_view;

template <typename T>
class array_view;

namespace proxy_detail {

constexpr ::flatbuffers::voffset_t first_field = 4;
constexpr ::flatbuffers::voffset_t field_step = 2;

template <typename T>
struct remove_annotation {
    using type = std::remove_cvref_t<T>;
};

template <typename T>
    requires requires { typename std::remove_cvref_t<T>::annotated_type; }
struct remove_annotation<T> {
    using type = std::remove_cvref_t<typename std::remove_cvref_t<T>::annotated_type>;
};

template <typename T>
using remove_annotation_t = typename remove_annotation<T>::type;

template <typename T>
struct remove_optional {
    using type = std::remove_cvref_t<T>;
};

template <typename T>
struct remove_optional<std::optional<T>> {
    using type = std::remove_cvref_t<T>;
};

template <typename T>
using remove_optional_t = typename remove_optional<std::remove_cvref_t<T>>::type;

template <typename T>
using clean_t = remove_optional_t<remove_annotation_t<T>>;

template <typename T>
consteval bool has_annotated_fields() {
    using U = std::remove_cvref_t<T>;
    if constexpr(!refl::reflectable_class<U>) {
        return false;
    } else {
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return (serde::annotated_type<refl::field_type<U, I>> || ...);
        }(std::make_index_sequence<refl::field_count<U>()>{});
    }
}

template <typename T>
constexpr bool can_inline_struct_v =
    refl::reflectable_class<T> && is_schema_struct_v<T> && !has_annotated_fields<T>();

template <typename T>
constexpr bool is_string_like_v = serde::str_like<T>;

template <typename T>
constexpr bool is_range_like_v = std::ranges::input_range<T> && !is_string_like_v<T>;

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

template <typename Member,
          typename CleanMember = clean_t<Member>,
          bool IsRange = is_range_like_v<CleanMember>>
struct member_return_impl;

template <typename Member, typename CleanMember>
struct member_return_impl<Member, CleanMember, true> {
    using type = array_view<clean_t<std::ranges::range_value_t<CleanMember>>>;
};

template <typename Member, typename CleanMember>
struct member_return_impl<Member, CleanMember, false> {
    using type = std::conditional_t<
        is_string_like_v<CleanMember>,
        std::string_view,
        std::conditional_t<serde::bool_like<CleanMember> || serde::int_like<CleanMember> ||
                               serde::uint_like<CleanMember> || serde::floating_like<CleanMember> ||
                               serde::char_like<CleanMember> || std::is_enum_v<CleanMember> ||
                               can_inline_struct_v<CleanMember>,
                           CleanMember,
                           table_view<CleanMember>>>;
};

template <typename Member>
struct member_return : member_return_impl<Member> {};

template <typename Member>
using member_return_t = typename member_return<Member>::type;

template <typename Element>
struct array_element_return {
private:
    using clean_element_t = clean_t<Element>;

public:
    using type = std::conditional_t<
        is_string_like_v<clean_element_t>,
        std::string_view,
        std::conditional_t<
            serde::bool_like<clean_element_t> || serde::int_like<clean_element_t> ||
                serde::uint_like<clean_element_t> || serde::floating_like<clean_element_t> ||
                serde::char_like<clean_element_t> || std::is_enum_v<clean_element_t> ||
                std::same_as<clean_element_t, std::byte> || can_inline_struct_v<clean_element_t>,
            clean_element_t,
            table_view<clean_element_t>>>;
};

template <typename Element>
using array_element_return_t = typename array_element_return<Element>::type;

}  // namespace proxy_detail

template <typename Element>
class array_view {
public:
    using element_type = proxy_detail::clean_t<Element>;
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
        } else if constexpr(proxy_detail::can_inline_struct_v<element_type>) {
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
        using member_type = proxy_detail::clean_t<Member>;
        using return_t = proxy_detail::member_return_t<Member>;

        if(!valid()) {
            return return_t{};
        }

        const auto index = proxy_detail::field_index(member);
        if(index >= refl::field_count<object_type>()) {
            return return_t{};
        }

        const auto field = proxy_detail::voffset(index);

        if constexpr(proxy_detail::is_range_like_v<member_type>) {
            using element_type = proxy_detail::clean_t<std::ranges::range_value_t<member_type>>;
            using vector_ptr_t = proxy_detail::array_vector_ptr_t<element_type>;
            const auto* value = table->GetPointer<vector_ptr_t>(field);
            return return_t(root, value);
        } else if constexpr(proxy_detail::is_string_like_v<member_type>) {
            const auto* text = table->GetPointer<const ::flatbuffers::String*>(field);
            if(text == nullptr) {
                return {};
            }
            return std::string_view(text->data(), text->size());
        } else if constexpr(std::is_enum_v<member_type>) {
            using storage_t = std::underlying_type_t<member_type>;
            const auto value = table->GetField<storage_t>(field, storage_t{});
            return static_cast<member_type>(value);
        } else if constexpr(serde::char_like<member_type>) {
            const auto value = table->GetField<std::int8_t>(field, std::int8_t{});
            return static_cast<member_type>(value);
        } else if constexpr(serde::bool_like<member_type> || serde::int_like<member_type> ||
                            serde::uint_like<member_type>) {
            return table->GetField<member_type>(field, member_type{});
        } else if constexpr(serde::floating_like<member_type>) {
            if constexpr(std::same_as<member_type, float> || std::same_as<member_type, double>) {
                return table->GetField<member_type>(field, member_type{});
            } else {
                const auto value = table->GetField<double>(field, 0.0);
                return static_cast<member_type>(value);
            }
        } else if constexpr(proxy_detail::can_inline_struct_v<member_type>) {
            const auto* value = table->GetStruct<const member_type*>(field);
            if(value == nullptr) {
                return {};
            }
            return *value;
        } else {
            const auto* nested = table->GetPointer<const table_type*>(field);
            return return_t(root, nested);
        }
    }

private:
    const std::uint8_t* root = nullptr;
    const table_type* table = nullptr;
};

}  // namespace eventide::serde::flatbuffers::binary
