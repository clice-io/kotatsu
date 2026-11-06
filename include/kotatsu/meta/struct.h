#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "name.h"
#include "kotatsu/support/config.h"
#include "kotatsu/support/type_list.h"

namespace kotatsu::meta::detail {

/// The most fields meta reflects a struct with, as many as binder.inl has specializations for
/// where the compiler has no structured binding packs, and where it has them too, so that a
/// struct is reflected alike whatever compiles it.
constexpr std::size_t max_fields = 72;

template <class T>
extern const T ext{};

template <class T>
union uninitialized {
    T value;
    char bytes[sizeof(T)];

    uninitialized() {}

    ~uninitialized() {}
};

#if KOTATSU_HAS_BINDING_PACKS

/// Whether a structured binding takes T apart: not when its members are spread over its bases,
/// which meta then does not reflect.
template <typename T>
concept decomposable = requires { __builtin_structured_binding_size(T); };

template <std::size_t N>
struct binder;

template <typename... Fields>
consteval bool holds_reference(type_list<Fields...>) {
    return (std::is_reference_v<Fields> || ...);
}

/// The fields of aggregate T, as many as a structured binding of it declares; none that meta
/// sees when they are more than max_fields, or when one is a reference, which has neither an
/// offset nor an address to name it by, as where the compiler has no binding packs.
template <typename T>
consteval std::size_t field_count() {
    constexpr std::size_t count = __builtin_structured_binding_size(T);
    if constexpr(count > max_fields) {
        return 0;
    } else if constexpr(holds_reference(decltype(binder<count>::types(std::declval<T&>())){})) {
        return 0;
    } else {
        return count;
    }
}

#else

template <typename T>
concept decomposable = true;

struct any {
    consteval any(std::size_t);

    template <typename T>
    consteval operator T() const {
        using F = T (*)();
        return F()();
    }
};

template <typename T, typename Seq>
constexpr bool initializable = false;

template <typename T, std::size_t... I>
constexpr bool initializable<T, std::index_sequence<I...>> = requires { T{any(I)...}; };

template <typename T, std::size_t N>
constexpr bool accepts = initializable<T, std::make_index_sequence<N>>;

/// The counts of initializers an aggregate accepts form one run: below it a member is left
/// without an initializer, above it the list overflows the aggregate. These walk that run, up
/// from zero to its start, then in doubling steps and a bisection to its end, the field count.
template <typename T, std::size_t N = 0>
consteval std::size_t run_start() {
    if constexpr(N > max_fields || accepts<T, N>) {
        return N;
    } else {
        return run_start<T, N + 1>();
    }
}

template <typename T, std::size_t Low, std::size_t High>
consteval std::size_t bisect_run_end() {
    constexpr std::size_t mid = Low + (High - Low) / 2;
    if constexpr(mid == Low) {
        return Low;
    } else if constexpr(accepts<T, mid>) {
        return bisect_run_end<T, mid, High>();
    } else {
        return bisect_run_end<T, Low, mid>();
    }
}

template <typename T, std::size_t Low, std::size_t Step>
consteval std::size_t run_end() {
    if constexpr(Low + Step > max_fields + 1) {
        return bisect_run_end<T, Low, max_fields + 2>();
    } else if constexpr(accepts<T, Low + Step>) {
        return run_end<T, Low + Step, Step * 2>();
    } else {
        return bisect_run_end<T, Low, Low + Step>();
    }
}

/// The fields of aggregate T; none that meta sees when no list of at most max_fields
/// initializers initializes it, or when it takes more, as an array member's elements count
/// one each.
template <typename T>
consteval std::size_t field_count() {
    constexpr std::size_t start = run_start<T>();
    if constexpr(start > max_fields) {
        return 0;
    } else {
        constexpr std::size_t count = run_end<T, start, 1>();
        return count > max_fields ? 0 : count;
    }
}

#endif

/// The offsets of the fields of T at `addresses`, members of ext<uninitialized<T>>.value, found
/// in one walk through its bytes: each search starts where the one before ended, and only a
/// field placed below it ([[no_unique_address]]) starts over from the first byte.
template <typename T, std::size_t N>
consteval std::array<std::size_t, N> locate(const std::array<const void*, N>& addresses) {
    const auto& unknown = ext<uninitialized<T>>;
    std::array<std::size_t, N> offsets{};
    std::size_t at = 0;
    for(std::size_t i = 0; i < N; ++i) {
        std::size_t found = sizeof(T);
        for(std::size_t b = at; b < sizeof(T) && found == sizeof(T); ++b) {
            if(addresses[i] == &unknown.bytes[b]) {
                found = b;
            }
        }
        for(std::size_t b = 0; b < at && found == sizeof(T); ++b) {
            if(addresses[i] == &unknown.bytes[b]) {
                found = b;
            }
        }
        if(found == sizeof(T)) {
            std::unreachable();
        }
        offsets[i] = at = found;
    }
    return offsets;
}

constexpr bool identifier_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;
}

/// The name of the data member whose address a pack_signature element prints: the identifier
/// it ends in, after the last `.` (clang, GCC) or `->` (MSVC), before the parenthesis and the
/// brace that close it.
constexpr std::string_view member_of(string_ref printed) {
    std::size_t end = printed.size();
    while(end > 0 && !identifier_char(printed[end - 1])) {
        --end;
    }
    std::size_t begin = end;
    while(begin > 0 && identifier_char(printed[begin - 1])) {
        --begin;
    }
    return printed.substr(begin, end - begin);
}

/// The names of the data members at the addresses Ptrs.
template <wrapper... Ptrs>
consteval std::array<std::string_view, sizeof...(Ptrs)> member_names() {
    const auto parts = split_pack<sizeof...(Ptrs)>(pack_signature<Ptrs...>());
    std::array<std::string_view, sizeof...(Ptrs)> names{};
    for(std::size_t i = 0; i < names.size(); ++i) {
        names[i] = member_of(parts[i]);
    }
    return names;
}

/// A reference among those pick takes, at place I.
template <std::size_t I, typename T>
struct ref_leaf {
    T& ref;
};

template <typename Seq, typename... Ts>
struct ref_pack;

template <std::size_t... Is, typename... Ts>
struct ref_pack<std::index_sequence<Is...>, Ts...> : ref_leaf<Is, Ts>... {};

template <std::size_t I, typename T>
constexpr T& leaf_ref(const ref_leaf<I, T>& leaf) {
    return leaf.ref;
}

/// The I-th of values, in one instantiation however many they are.
template <std::size_t I, typename... Ts>
constexpr auto& pick(Ts&... values) {
    return leaf_ref<I>(ref_pack<std::index_sequence_for<Ts...>, Ts...>{{values}...});
}

/// What meta knows of the fields of an aggregate with N of them, each from one structured
/// binding of all of them: apply hands them to a callable at once, types lists their declared
/// types, offsets and names read them from ext<uninitialized<T>>. Without structured binding
/// packs, binder.inl specializes it for every N up to max_fields.
template <std::size_t N>
struct binder;

template <>
struct binder<0> {
    template <typename Object, typename Fn>
    constexpr static decltype(auto) apply(Object&, Fn&& fn) {
        return fn();
    }

    template <typename T>
    static type_list<> types(T&);

    template <typename T>
    consteval static std::array<std::size_t, 0> offsets() {
        return {};
    }

    template <typename T>
    consteval static std::array<std::string_view, 0> names() {
        return {};
    }
};

#if KOTATSU_HAS_BINDING_PACKS

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++26-extensions"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc++26-extensions"
#endif

template <std::size_t N>
struct binder {
    template <typename Object, typename Fn>
    constexpr static decltype(auto) apply(Object& object, Fn&& fn) {
        auto& [... fields] = object;
        return fn(fields...);
    }

    template <typename T>
    static auto types(T& object) {
        [[maybe_unused]] auto& [... fields] = object;
        return type_list<decltype(fields)...>{};
    }

    template <typename T>
    consteval static auto offsets() {
        auto& [... fields] = ext<uninitialized<T>>.value;
        return locate<T>(std::array<const void*, N>{&fields...});
    }

    template <typename T>
    consteval static auto names() {
        auto& [... fields] = ext<uninitialized<T>>.value;
        return member_names<wrapper{&fields}...>();
    }
};

#if defined(__clang__)
#pragma clang diagnostic pop
#else
#pragma GCC diagnostic pop
#endif

#else

#define KOTATSU_BINDER_NAME(I) f##I
#define KOTATSU_BINDER_TYPE(I) decltype(f##I)
#define KOTATSU_BINDER_ADDRESS(I) &f##I
#define KOTATSU_BINDER_WRAPPED(I)                                                                  \
    wrapper {                                                                                      \
        &f##I                                                                                      \
    }

#if KOTATSU_COMPILER_MSVC
// MSVC takes no address of a structured binding as a template argument: names() reads each
// field's address from a function of its own.
template <typename T, std::size_t I>
consteval auto field_address() {
    return binder<field_count<T>()>::apply(ext<uninitialized<T>>.value, [](const auto&... fields) {
        return &pick<I>(fields...);
    });
}

template <typename T, std::size_t... Is>
consteval auto names_by_address(std::index_sequence<Is...>) {
    return member_names<wrapper{field_address<T, Is>()}...>();
}

#define KOTATSU_BINDER_NAMES(N)                                                                    \
    template <typename T>                                                                          \
    consteval static auto names() {                                                                \
        return names_by_address<T>(std::make_index_sequence<N>{});                                 \
    }
#else
#define KOTATSU_BINDER_NAMES(N)                                                                    \
    template <typename T>                                                                          \
    consteval static auto names() {                                                                \
        auto& [KOTATSU_BINDER_FIELDS_##N(KOTATSU_BINDER_NAME)] = ext<uninitialized<T>>.value;      \
        return member_names<KOTATSU_BINDER_FIELDS_##N(KOTATSU_BINDER_WRAPPED)>();                  \
    }
#endif

#define KOTATSU_BINDER(N)                                                                          \
    template <>                                                                                    \
    struct binder<N> {                                                                             \
        template <typename Object, typename Fn>                                                    \
        constexpr static decltype(auto) apply(Object& object, Fn&& fn) {                           \
            auto& [KOTATSU_BINDER_FIELDS_##N(KOTATSU_BINDER_NAME)] = object;                       \
            return fn(KOTATSU_BINDER_FIELDS_##N(KOTATSU_BINDER_NAME));                             \
        }                                                                                          \
                                                                                                   \
        template <typename T>                                                                      \
        static auto types(T& object) {                                                             \
            [[maybe_unused]] auto& [KOTATSU_BINDER_FIELDS_##N(KOTATSU_BINDER_NAME)] = object;      \
            return type_list<KOTATSU_BINDER_FIELDS_##N(KOTATSU_BINDER_TYPE)>{};                    \
        }                                                                                          \
                                                                                                   \
        template <typename T>                                                                      \
        consteval static auto offsets() {                                                          \
            auto& [KOTATSU_BINDER_FIELDS_##N(KOTATSU_BINDER_NAME)] = ext<uninitialized<T>>.value;  \
            return locate<T>(                                                                      \
                std::array<const void*, N>{KOTATSU_BINDER_FIELDS_##N(KOTATSU_BINDER_ADDRESS)});    \
        }                                                                                          \
                                                                                                   \
        KOTATSU_BINDER_NAMES(N)                                                                    \
    };

#include "binder.inl"

#undef KOTATSU_BINDER
#undef KOTATSU_BINDER_NAMES
#undef KOTATSU_BINDER_NAME
#undef KOTATSU_BINDER_TYPE
#undef KOTATSU_BINDER_ADDRESS
#undef KOTATSU_BINDER_WRAPPED

#endif

}  // namespace kotatsu::meta::detail

namespace kotatsu::meta {

template <typename T>
struct reflection;

template <typename Object>
    requires std::is_aggregate_v<Object> && detail::decomposable<Object>
struct reflection<Object> {
    constexpr inline static std::size_t field_count = meta::detail::field_count<Object>();
};

template <typename Object>
consteval std::size_t field_count() {
    return reflection<Object>::field_count;
}

namespace detail {

template <typename Object>
using binder_of = binder<reflection<Object>::field_count>;

template <typename Object>
using field_types_t = decltype(binder_of<Object>::types(std::declval<Object&>()));

// Variables of their own rather than members of reflection, so that what needs only the
// count, as reflectable_class does, evaluates neither.

template <typename Object>
constexpr inline auto field_names_v = binder_of<Object>::template names<Object>();

template <typename Object>
constexpr inline auto field_offsets_v = binder_of<Object>::template offsets<Object>();

}  // namespace detail

/// Hands every field of object to fn at once, as lvalues as const as object is, and returns
/// what fn does.
template <typename Object, typename Fn>
constexpr decltype(auto) apply_fields(Object& object, Fn&& fn) {
    return detail::binder_of<std::remove_const_t<Object>>::apply(object, std::forward<Fn>(fn));
}

template <typename Object>
constexpr auto field_refs(Object&& object) {
    return apply_fields(object, [](auto&... fields) {
        if constexpr(std::is_lvalue_reference_v<Object&&>) {
            return std::tuple<decltype(fields)...>(fields...);
        } else {
            return std::tuple<std::remove_reference_t<decltype(fields)>&&...>(std::move(fields)...);
        }
    });
}

/// The names of Object's fields, as declared.
template <typename Object>
consteval const auto& field_names() {
    return detail::field_names_v<Object>;
}

/// The declared type of field I of Object; of a reference member, the type it refers to.
template <typename Object, std::size_t I>
using field_type = std::remove_reference_t<type_list_element_t<I, detail::field_types_t<Object>>>;

template <std::size_t I, typename Object>
constexpr auto& field_of(Object&& object) {
    return apply_fields(object,
                        [](auto&... fields) -> auto& { return detail::pick<I>(fields...); });
}

template <std::size_t I, typename Object>
constexpr auto field_addr_of(Object&& object) {
    return std::addressof(field_of<I>(object));
}

template <std::size_t I, typename Object>
consteval std::string_view field_name() {
    return detail::field_names_v<Object>[I];
}

/// The offsets of Object's fields, in declaration order.
template <class Object>
consteval const auto& field_offsets() {
    return detail::field_offsets_v<Object>;
}

template <class Object>
consteval auto field_offset(std::size_t index) noexcept -> std::size_t {
    return detail::field_offsets_v<Object>[index];
}

template <typename C, typename M>
consteval auto field_offset(M C::* member) noexcept -> std::size_t {
    const auto& unknown = detail::ext<detail::uninitialized<C>>;
    const void* address = &(unknown.value.*member);
    for(std::size_t i = 0; i < sizeof(unknown.bytes); ++i) {
        if(address == &unknown.bytes[i]) {
            return i;
        }
    }
    std::unreachable();
}

template <std::size_t I, typename Object>
struct field {
    Object& object;

    using type = field_type<std::remove_const_t<Object>, I>;

    constexpr auto&& value() {
        return field_of<I>(object);
    }

    constexpr static std::size_t index() {
        return I;
    }

    constexpr static std::string_view name() {
        return field_name<I, std::remove_const_t<Object>>();
    }

    constexpr static std::size_t offset() {
        return field_offset<std::remove_const_t<Object>>(I);
    }
};

template <typename Object, typename Callback>
constexpr bool for_each(Object&& object, const Callback& callback) {
    using reflect = reflection<std::remove_cvref_t<Object>>;
    auto foldable = [&](auto field) {
        using R = decltype(callback(field));
        if constexpr(std::is_void_v<R>) {
            callback(field);
            return true;
        } else {
            return bool(callback(field));
        }
    };

    using T = std::remove_reference_t<Object>;
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (foldable(field<Is, T>{object}) && ...);
    }(std::make_index_sequence<reflect::field_count>());
}

template <typename T>
concept reflectable_class = requires {
    requires std::is_class_v<T>;
    reflection<T>::field_count;
};

}  // namespace kotatsu::meta
