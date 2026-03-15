#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "eventide/reflection/struct.h"
#include "eventide/serde/serde/annotation.h"
#include "eventide/serde/schema/attrs.h"
#include "eventide/serde/serde/config.h"
#include "eventide/serde/schema/kind.h"
#include "eventide/serde/serde/spelling.h"

namespace eventide::serde::schema {

// ─── Type-erased schema view ────────────────────────────────────────────────

struct type_schema_view;

struct field_schema_view {
    std::string_view wire_name;
    type_kind kind;
    bool required;
    const type_schema_view* nested;
    std::span<const std::string_view> aliases;
};

struct type_schema_view {
    type_kind kind;
    scalar_kind scalar = scalar_kind::none;
    type_flags flags = type_flags::none;

    std::string_view type_name;
    std::size_t cpp_size = 0;
    std::size_t cpp_align = 0;

    std::span<const field_schema_view> fields;

    const type_schema_view* element = nullptr;

    const type_schema_view* key = nullptr;
    const type_schema_view* value = nullptr;

    std::span<const type_schema_view* const> tuple_elements;

    std::span<const type_schema_view* const> alternatives;

    tag_mode tagging = tag_mode::none;
    std::string_view tag_field;
    std::string_view content_field;
    std::span<const std::string_view> alt_names;
};

// ─── Legacy descriptor types ────────────────────────────────────────────────

struct field_descriptor {
    std::string_view name;
    type_kind kind;
    bool required;
};

template <std::size_t N>
struct type_descriptor {
    std::array<field_descriptor, N> fields{};

    constexpr std::size_t field_count() const {
        return N;
    }
};

namespace detail {

// ─── Annotation helpers ─────────────────────────────────────────────────────

template <typename T>
consteval auto unwrap_annotation_type() {
    using U = std::remove_cvref_t<T>;
    if constexpr(serde::annotated_type<U>) {
        return std::type_identity<typename U::annotated_type>{};
    } else {
        return std::type_identity<U>{};
    }
}

template <typename T>
using unwrapped_t = typename decltype(unwrap_annotation_type<T>())::type;

template <typename T, std::size_t I>
consteval bool field_has_flatten() {
    using field_t = refl::field_type<T, I>;
    if constexpr(!serde::has_attrs<field_t>) {
        return false;
    } else {
        return serde::detail::tuple_has_v<typename field_t::attrs, serde::schema::flatten>;
    }
}

template <typename T, std::size_t I>
consteval bool field_has_skip() {
    using field_t = refl::field_type<T, I>;
    if constexpr(!serde::has_attrs<field_t>) {
        return false;
    } else {
        return serde::detail::tuple_has_v<typename field_t::attrs, serde::schema::skip>;
    }
}

template <typename T, std::size_t I>
consteval bool field_has_explicit_rename() {
    using field_t = refl::field_type<T, I>;
    if constexpr(!serde::has_attrs<field_t>) {
        return false;
    } else {
        return serde::detail::tuple_any_of_v<typename field_t::attrs, serde::is_rename_attr>;
    }
}

// ─── Effective field count ──────────────────────────────────────────────────

template <typename T>
consteval std::size_t effective_field_count();

template <typename T, std::size_t I>
consteval std::size_t single_field_contribution() {
    if constexpr(field_has_skip<T, I>()) {
        return 0;
    } else if constexpr(field_has_flatten<T, I>()) {
        using field_t = refl::field_type<T, I>;
        using inner_t = unwrapped_t<field_t>;
        static_assert(refl::reflectable_class<inner_t>,
                      "schema::flatten requires a reflectable class");
        return effective_field_count<inner_t>();
    } else {
        return 1;
    }
}

template <typename T>
consteval std::size_t effective_field_count() {
    if constexpr(!refl::reflectable_class<std::remove_cvref_t<T>>) {
        return 0;
    } else {
        using U = std::remove_cvref_t<T>;
        constexpr auto N = refl::field_count<U>();
        if constexpr(N == 0) {
            return 0;
        } else {
            return []<std::size_t... Is>(std::index_sequence<Is...>) consteval {
                return (single_field_contribution<U, Is>() + ...);
            }(std::make_index_sequence<N>{});
        }
    }
}

// ─── Field resolution helpers ───────────────────────────────────────────────

template <typename T, std::size_t I>
consteval std::string_view resolve_field_name() {
    return serde::schema::canonical_field_name<T, I>();
}

template <typename T>
consteval type_kind unwrap_optional_kind() {
    using U = std::remove_cvref_t<T>;
    if constexpr(is_optional_v<U>) {
        return unwrap_optional_kind<typename U::value_type>();
    } else if constexpr(is_specialization_of<std::unique_ptr, U> ||
                        is_specialization_of<std::shared_ptr, U>) {
        return unwrap_optional_kind<typename U::element_type>();
    } else {
        return kind_of<U>();
    }
}

template <typename T, std::size_t I>
consteval type_kind resolve_field_kind() {
    using field_t = refl::field_type<T, I>;
    using value_t = unwrapped_t<field_t>;
    return unwrap_optional_kind<value_t>();
}

template <typename T, std::size_t I>
consteval bool resolve_field_required() {
    using field_t = refl::field_type<T, I>;
    return !is_optional_type<field_t>();
}

// ─── Constexpr rename (internal computation buffer) ─────────────────────────

struct name_buf {
    char data[64]{};
    std::uint8_t len = 0;

    static constexpr name_buf from(std::string_view s) {
        name_buf b;
        auto n = s.size() < 63 ? s.size() : 63;
        for(std::size_t i = 0; i < n; i++) b.data[i] = s[i];
        b.len = static_cast<std::uint8_t>(n);
        return b;
    }

    constexpr std::string_view view() const { return {data, len}; }
    constexpr void push_back(char c) { data[len++] = c; }
    constexpr char back() const { return data[len - 1]; }
    constexpr bool empty() const { return len == 0; }
};

constexpr name_buf normalize_to_lower_snake_cx(std::string_view text) {
    using namespace spelling::detail;

    name_buf out;
    for(std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if(is_ascii_alnum(c)) {
            if(is_ascii_upper(c)) {
                const bool prev_alnum = i > 0 && is_ascii_alnum(text[i - 1]);
                const bool prev_lower_digit =
                    i > 0 && (is_ascii_lower(text[i - 1]) || is_ascii_digit(text[i - 1]));
                const bool next_lower = i + 1 < text.size() && is_ascii_lower(text[i + 1]);
                if(!out.empty() && out.back() != '_' && prev_alnum &&
                   (prev_lower_digit || next_lower)) {
                    out.push_back('_');
                }
                out.push_back(ascii_lower(c));
            } else {
                out.push_back(ascii_lower(c));
            }
        } else if(!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }

    std::size_t start = 0;
    while(start < out.len && out.data[start] == '_') start++;
    std::size_t end = out.len;
    while(end > start && out.data[end - 1] == '_') end--;

    if(start > 0 || end < out.len) {
        name_buf trimmed;
        for(std::size_t i = start; i < end; i++) trimmed.push_back(out.data[i]);
        return trimmed;
    }
    return out;
}

constexpr name_buf snake_to_camel_cx(std::string_view text, bool upper_first) {
    using namespace spelling::detail;
    auto snake = normalize_to_lower_snake_cx(text);
    name_buf out;
    bool capitalize_next = upper_first;
    bool seen_output = false;
    for(std::size_t i = 0; i < snake.len; i++) {
        char c = snake.data[i];
        if(c == '_') {
            capitalize_next = true;
            continue;
        }
        if(capitalize_next && is_ascii_alpha(c)) {
            out.push_back(ascii_upper(c));
        } else if(!seen_output) {
            out.push_back(upper_first ? ascii_upper(c) : ascii_lower(c));
        } else {
            out.push_back(c);
        }
        capitalize_next = false;
        seen_output = true;
    }
    return out;
}

constexpr name_buf snake_to_upper_cx(std::string_view text) {
    using namespace spelling::detail;
    auto snake = normalize_to_lower_snake_cx(text);
    for(std::size_t i = 0; i < snake.len; i++) {
        snake.data[i] = ascii_upper(snake.data[i]);
    }
    return snake;
}

template <typename Policy>
constexpr name_buf apply_rename_cx(std::string_view input) {
    if constexpr(std::same_as<Policy, spelling::rename_policy::identity>) {
        return name_buf::from(input);
    } else if constexpr(std::same_as<Policy, spelling::rename_policy::lower_snake>) {
        return normalize_to_lower_snake_cx(input);
    } else if constexpr(std::same_as<Policy, spelling::rename_policy::lower_camel>) {
        return snake_to_camel_cx(input, false);
    } else if constexpr(std::same_as<Policy, spelling::rename_policy::upper_camel>) {
        return snake_to_camel_cx(input, true);
    } else if constexpr(std::same_as<Policy, spelling::rename_policy::upper_snake>) {
        return snake_to_upper_cx(input);
    } else {
        return name_buf::from(input);
    }
}

// ─── Exact-length wire name storage ─────────────────────────────────────────
// Two-pass consteval: first compute length via name_buf, then store in exact array.

template <typename T, std::size_t I, typename Policy>
struct wire_name_static {
    static consteval std::size_t length() {
        return apply_rename_cx<Policy>(serde::schema::canonical_field_name<T, I>()).len;
    }

    static constexpr auto data = [] consteval {
        constexpr auto L = length();
        auto nb = apply_rename_cx<Policy>(serde::schema::canonical_field_name<T, I>());
        std::array<char, L> buf{};
        for(std::size_t i = 0; i < L; i++) buf[i] = nb.data[i];
        return buf;
    }();

    static constexpr std::string_view view{data.data(), length()};
};

/// Resolve wire name for field I of T with Config. Returns string_view with static lifetime.
template <typename T, std::size_t I, typename Config>
consteval std::string_view resolve_wire_name() {
    if constexpr(field_has_explicit_rename<T, I>()) {
        return serde::schema::canonical_field_name<T, I>();
    } else if constexpr(!requires { typename Config::field_rename; }) {
        return serde::schema::canonical_field_name<T, I>();
    } else if constexpr(std::same_as<typename Config::field_rename,
                                     spelling::rename_policy::identity>) {
        return serde::schema::canonical_field_name<T, I>();
    } else {
        return wire_name_static<T, I, typename Config::field_rename>::view;
    }
}

/// Resolve aliases as a span for field I of T.
template <typename T, std::size_t I>
consteval std::span<const std::string_view> resolve_aliases_span() {
    if constexpr(!serde::schema::detail::field_has_alias<T, I>()) {
        return {};
    } else {
        using field_t = refl::field_type<T, I>;
        using attrs_t = typename field_t::attrs;
        using alias_attr = serde::detail::tuple_find_t<attrs_t, serde::is_alias_attr>;
        return std::span<const std::string_view>(alias_attr::names);
    }
}

// ─── schema_instance: static storage for type-erased schema ─────────────────

template <typename T, typename Config>
struct schema_instance;

template <typename T, typename Config>
consteval const type_schema_view* resolve_element_schema() {
    using U = std::remove_cvref_t<T>;
    if constexpr(is_optional_v<U>) {
        return &schema_instance<typename U::value_type, Config>::view;
    } else if constexpr(is_specialization_of<std::unique_ptr, U> ||
                        is_specialization_of<std::shared_ptr, U>) {
        // Not eagerly resolved to avoid circular dependency on recursive types
        // like TreeNode { vector<unique_ptr<TreeNode>> children; }.
        return nullptr;
    } else if constexpr(serde::is_pair_v<U> || serde::is_tuple_v<U>) {
        return nullptr;
    } else if constexpr(std::ranges::input_range<U> && !serde::str_like<U> &&
                        !serde::bytes_like<U>) {
        constexpr auto fmt = format_kind<U>;
        if constexpr(fmt == range_format::map) {
            return nullptr;
        } else {
            using elem_t = std::ranges::range_value_t<U>;
            return &schema_instance<elem_t, Config>::view;
        }
    } else {
        return nullptr;
    }
}

template <typename T, typename Config>
consteval const type_schema_view* resolve_key_schema() {
    using U = std::remove_cvref_t<T>;
    if constexpr(std::ranges::input_range<U>) {
        if constexpr(format_kind<U> == range_format::map) {
            return &schema_instance<typename U::key_type, Config>::view;
        } else {
            return nullptr;
        }
    } else {
        return nullptr;
    }
}

template <typename T, typename Config>
consteval const type_schema_view* resolve_value_schema() {
    using U = std::remove_cvref_t<T>;
    if constexpr(std::ranges::input_range<U>) {
        if constexpr(format_kind<U> == range_format::map) {
            return &schema_instance<typename U::mapped_type, Config>::view;
        } else {
            return nullptr;
        }
    } else {
        return nullptr;
    }
}

// ─── Variant alternative storage ────────────────────────────────────────────

template <typename T, typename Config>
struct variant_alt_storage {
    static constexpr std::array<const type_schema_view*, 0> data = {};
};

template <typename Config, typename... Ts>
struct variant_alt_storage<std::variant<Ts...>, Config> {
    static constexpr std::array<const type_schema_view*, sizeof...(Ts)> data = {
        &schema_instance<Ts, Config>::view...};
};

template <typename Config, typename Inner, typename... Attrs>
    requires is_specialization_of<std::variant, std::remove_cvref_t<Inner>>
struct variant_alt_storage<serde::annotation<Inner, Attrs...>, Config>
    : variant_alt_storage<std::remove_cvref_t<Inner>, Config> {};

// ─── Variant tag info resolution ────────────────────────────────────────────

template <typename T>
consteval tag_mode resolve_tag_mode() {
    using U = std::remove_cvref_t<T>;
    if constexpr(!serde::annotated_type<U>) {
        return tag_mode::none;
    } else {
        using inner = std::remove_cvref_t<typename U::annotated_type>;
        using attrs = typename U::attrs;
        if constexpr(!is_specialization_of<std::variant, inner>) {
            return tag_mode::none;
        } else if constexpr(!serde::detail::tuple_any_of_v<attrs, serde::is_tagged_attr>) {
            return tag_mode::none;
        } else {
            using tag_attr = serde::detail::tuple_find_t<attrs, serde::is_tagged_attr>;
            constexpr auto n = tag_attr::field_names.size();
            if constexpr(n == 0) return tag_mode::external;
            else if constexpr(n == 1) return tag_mode::internal;
            else return tag_mode::adjacent;
        }
    }
}

template <typename T>
consteval std::string_view resolve_tag_field() {
    using U = std::remove_cvref_t<T>;
    if constexpr(!serde::annotated_type<U>) {
        return {};
    } else {
        using attrs = typename U::attrs;
        if constexpr(!serde::detail::tuple_any_of_v<attrs, serde::is_tagged_attr>) {
            return {};
        } else {
            using tag_attr = serde::detail::tuple_find_t<attrs, serde::is_tagged_attr>;
            if constexpr(tag_attr::field_names.size() >= 1) {
                return tag_attr::field_names[0];
            } else {
                return {};
            }
        }
    }
}

template <typename T>
consteval std::string_view resolve_content_field() {
    using U = std::remove_cvref_t<T>;
    if constexpr(!serde::annotated_type<U>) {
        return {};
    } else {
        using attrs = typename U::attrs;
        if constexpr(!serde::detail::tuple_any_of_v<attrs, serde::is_tagged_attr>) {
            return {};
        } else {
            using tag_attr = serde::detail::tuple_find_t<attrs, serde::is_tagged_attr>;
            if constexpr(tag_attr::field_names.size() >= 2) {
                return tag_attr::field_names[1];
            } else {
                return {};
            }
        }
    }
}

template <typename T>
struct alt_names_storage {
    static constexpr std::array<std::string_view, 0> data = {};
};

template <typename TagAttr, typename Variant>
struct alt_names_from_variant;

template <typename TagAttr, typename... Ts>
struct alt_names_from_variant<TagAttr, std::variant<Ts...>> {
    static constexpr auto data = []() {
        if constexpr(TagAttr::has_custom_names) {
            static_assert(TagAttr::tag_names.size() == sizeof...(Ts),
                          "tagged: number of custom names must match variant alternatives");
            return TagAttr::tag_names;
        } else {
            return std::array<std::string_view, sizeof...(Ts)>{refl::type_name<Ts>()...};
        }
    }();
};

template <typename T>
    requires serde::annotated_type<std::remove_cvref_t<T>> &&
             is_specialization_of<std::variant, std::remove_cvref_t<typename std::remove_cvref_t<T>::annotated_type>> &&
             serde::detail::tuple_any_of_v<typename std::remove_cvref_t<T>::attrs, serde::is_tagged_attr>
struct alt_names_storage<T> {
    using U = std::remove_cvref_t<T>;
    using inner = std::remove_cvref_t<typename U::annotated_type>;
    using tag_attr = serde::detail::tuple_find_t<typename U::attrs, serde::is_tagged_attr>;

    static constexpr auto& data = alt_names_from_variant<tag_attr, inner>::data;
};

// ─── Tuple element storage ──────────────────────────────────────────────────

template <typename T, typename Config>
struct tuple_elem_storage {
    static constexpr std::array<const type_schema_view*, 0> data = {};
};

template <typename Config, typename... Ts>
struct tuple_elem_storage<std::tuple<Ts...>, Config> {
    static constexpr std::array<const type_schema_view*, sizeof...(Ts)> data = {
        &schema_instance<Ts, Config>::view...};
};

template <typename Config, typename T, typename U>
struct tuple_elem_storage<std::pair<T, U>, Config> {
    static constexpr std::array<const type_schema_view*, 2> data = {
        &schema_instance<T, Config>::view, &schema_instance<U, Config>::view};
};

template <typename T, typename Config, std::size_t N>
consteval void fill_schema_fields(std::array<field_schema_view, N>& arr, std::size_t& idx);

template <typename T>
consteval auto effective_value_type() {
    if constexpr(is_optional_v<T>) {
        return std::type_identity<typename T::value_type>{};
    } else {
        return std::type_identity<T>{};
    }
}

template <typename T>
using effective_value_t = typename decltype(effective_value_type<T>())::type;

template <typename T, typename Config, std::size_t I, std::size_t N>
consteval void fill_one_schema_field(std::array<field_schema_view, N>& arr, std::size_t& idx) {
    if constexpr(field_has_skip<T, I>()) {
        // skip
    } else if constexpr(field_has_flatten<T, I>()) {
        using field_t = refl::field_type<T, I>;
        using inner_t = unwrapped_t<field_t>;
        fill_schema_fields<inner_t, Config>(arr, idx);
    } else {
        using field_t = refl::field_type<T, I>;
        using value_t = unwrapped_t<field_t>;
        using eff_t = effective_value_t<value_t>;

        arr[idx++] = field_schema_view{
            .wire_name = resolve_wire_name<T, I, Config>(),
            .kind = resolve_field_kind<T, I>(),
            .required = resolve_field_required<T, I>(),
            .nested = &schema_instance<eff_t, Config>::view,
            .aliases = resolve_aliases_span<T, I>(),
        };
    }
}

template <typename T, typename Config, std::size_t N>
consteval void fill_schema_fields(std::array<field_schema_view, N>& arr, std::size_t& idx) {
    using U = std::remove_cvref_t<T>;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        (fill_one_schema_field<U, Config, Is>(arr, idx), ...);
    }(std::make_index_sequence<refl::field_count<U>()>{});
}

template <typename T, typename Config>
consteval auto build_schema_fields() {
    constexpr auto N = effective_field_count<T>();
    std::array<field_schema_view, N> arr{};
    if constexpr(N > 0) {
        std::size_t idx = 0;
        fill_schema_fields<T, Config>(arr, idx);
    }
    return arr;
}

template <typename T, typename Config>
struct schema_instance {
    static constexpr auto fields_storage = build_schema_fields<T, Config>();
    static constexpr auto& alt_storage = variant_alt_storage<std::remove_cvref_t<T>, Config>::data;
    static constexpr auto& tup_storage = tuple_elem_storage<std::remove_cvref_t<T>, Config>::data;
    static constexpr auto& tag_names_storage = alt_names_storage<std::remove_cvref_t<T>>::data;

    static constexpr type_schema_view view = {
        .kind = kind_of<T>(),
        .scalar = scalar_kind_of<T>(),
        .flags = type_flags_of<T>(),
        .type_name = refl::type_name<std::remove_cvref_t<T>>(),
        .cpp_size = sizeof(std::remove_cvref_t<T>),
        .cpp_align = alignof(std::remove_cvref_t<T>),
        .fields = std::span<const field_schema_view>(fields_storage),
        .element = resolve_element_schema<T, Config>(),
        .key = resolve_key_schema<T, Config>(),
        .value = resolve_value_schema<T, Config>(),
        .tuple_elements = std::span<const type_schema_view* const>(tup_storage),
        .alternatives = std::span<const type_schema_view* const>(alt_storage),
        .tagging = resolve_tag_mode<T>(),
        .tag_field = resolve_tag_field<T>(),
        .content_field = resolve_content_field<T>(),
        .alt_names = std::span<const std::string_view>(tag_names_storage),
    };
};

}  // namespace detail

/// Get the type-erased schema view for type T under Config.
template <typename T, typename Config = config::default_config>
constexpr const type_schema_view* get_schema() {
    return &detail::schema_instance<T, Config>::view;
}

// ─── Legacy describe() API ──────────────────────────────────────────────────

namespace detail {

template <typename T, std::size_t N>
consteval void fill_fields(std::array<field_descriptor, N>& arr, std::size_t& idx);

template <typename T, std::size_t I, std::size_t N>
consteval void fill_one_field(std::array<field_descriptor, N>& arr, std::size_t& idx) {
    if constexpr(field_has_skip<T, I>()) {
        // skip
    } else if constexpr(field_has_flatten<T, I>()) {
        using field_t = refl::field_type<T, I>;
        using inner_t = unwrapped_t<field_t>;
        fill_fields<inner_t>(arr, idx);
    } else {
        arr[idx++] = field_descriptor{
            .name = resolve_field_name<T, I>(),
            .kind = resolve_field_kind<T, I>(),
            .required = resolve_field_required<T, I>(),
        };
    }
}

template <typename T, std::size_t N>
consteval void fill_fields(std::array<field_descriptor, N>& arr, std::size_t& idx) {
    using U = std::remove_cvref_t<T>;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) consteval {
        (fill_one_field<U, Is>(arr, idx), ...);
    }(std::make_index_sequence<refl::field_count<U>()>{});
}

}  // namespace detail

template <typename T>
    requires refl::reflectable_class<std::remove_cvref_t<T>>
consteval auto describe() {
    constexpr auto N = detail::effective_field_count<T>();
    type_descriptor<N> desc{};
    if constexpr(N > 0) {
        std::size_t idx = 0;
        detail::fill_fields<T>(desc.fields, idx);
    }
    return desc;
}

template <typename T>
    requires refl::reflectable_class<std::remove_cvref_t<T>>
struct schema_of {
    static constexpr auto value = describe<T>();
};

// Public schema API forwarding from detail
using detail::resolve_tag_mode;
using detail::resolve_tag_field;
using detail::resolve_content_field;

}  // namespace eventide::serde::schema
