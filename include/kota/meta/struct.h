#pragma once

#include <array>
#include <string_view>
#include <tuple>
#include <utility>

#include "name.h"

namespace kota::meta::detail {

template <class T>
extern const T ext{};

template <class T>
union uninitialized {
    T value;
    char bytes[sizeof(T)];

    uninitialized() {}

    ~uninitialized() {}
};

struct any {
    consteval any(std::size_t);

    template <typename T>
    consteval operator T() const {
        using F = T (*)();
        return F()();
    }
};

template <typename T, std::size_t N>
consteval auto test() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
        return requires { T{any(I)...}; };
    }(std::make_index_sequence<N>{});
}

template <typename T, std::size_t N = 0>
consteval auto field_count() {
    if constexpr(N > 72) {
        return std::size_t(0);
    } else if constexpr(test<T, N>() && !test<T, N + 1>()) {
        return N;
    } else {
        return field_count<T, N + 1>();
    }
}

}  // namespace kota::meta::detail

namespace kota::meta {

template <typename T>
struct reflection;

template <typename Object>
    requires std::is_aggregate_v<Object>
struct reflection<Object> {
    constexpr inline static auto field_count = meta::detail::field_count<Object>();

    constexpr static auto field_addrs([[maybe_unused]] auto&& object) {
        if constexpr(field_count == 0) {
            return std::tuple{};
        }
#define REFL_BINDING_UNWRAP(...) __VA_ARGS__
#define REFL_BINDING_CASE(COUNT, BINDINGS, ADDRS)                                                  \
    else if constexpr(field_count == COUNT) {                                                      \
        auto&& [REFL_BINDING_UNWRAP BINDINGS] = object;                                            \
        return std::tuple{REFL_BINDING_UNWRAP ADDRS};                                              \
    }
#include "binding.inl"
#undef REFL_BINDING_CASE
#undef REFL_BINDING_UNWRAP
        else {
            static_assert(field_count <= 72, "please try to increase the supported member count");
        }
    }

    constexpr inline static std::array field_names = []<std::size_t... Is>(
                                                         std::index_sequence<Is...>) {
        if constexpr(field_count == 0) {
            return std::array<std::string_view, 1>{"PLACEHOLDER"};
        } else {
            constexpr auto addrs = field_addrs(detail::ext<detail::uninitialized<Object>>.value);
            return std::array{meta::pointer_name<detail::wrapper{std::get<Is>(addrs)}>()...};
        }
    }(std::make_index_sequence<field_count>{});
};

template <typename Object>
consteval std::size_t field_count() {
    return reflection<Object>::field_count;
}

template <typename Object>
constexpr auto field_refs(Object&& object) {
    auto field_addrs = reflection<std::remove_cvref_t<Object>>::field_addrs(object);
    return std::apply(
        [](auto... args) {
            if constexpr(std::is_lvalue_reference_v<Object&&>) {
                return std::tuple<
                    std::add_lvalue_reference_t<std::remove_pointer_t<decltype(args)>>...>(
                    *args...);
            } else {
                return std::tuple<
                    std::add_rvalue_reference_t<std::remove_pointer_t<decltype(args)>>...>(
                    std::move(*args)...);
            }
        },
        field_addrs);
}

template <typename Object>
consteval const auto& field_names() {
    return reflection<Object>::field_names;
}

template <typename Object, std::size_t I>
using field_type = std::remove_pointer_t<std::tuple_element_t<
    I,
    decltype(reflection<Object>::field_addrs(detail::ext<detail::uninitialized<Object>>.value))>>;

template <std::size_t I, typename Object>
constexpr auto field_addr_of(Object&& object) {
    auto addrs = reflection<std::remove_cvref_t<Object>>::field_addrs(object);
    return std::get<I>(addrs);
}

template <std::size_t I, typename Object>
constexpr auto& field_of(Object&& object) {
    return *field_addr_of<I>(object);
}

template <std::size_t I, typename Object>
consteval std::string_view field_name() {
    return field_names<Object>()[I];
}

template <class Object>
consteval auto field_offset(std::size_t index) noexcept -> std::size_t {
    constexpr std::size_t count = reflection<Object>::field_count;
    if(index >= count) {
        std::unreachable();
    }

    const auto& unknown = detail::ext<detail::uninitialized<Object>>;
    auto typed_addrs = reflection<Object>::field_addrs(unknown.value);
    std::array<const void*, count> addrs{};
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        ((addrs[Is] = static_cast<const void*>(std::get<Is>(typed_addrs))), ...);
    }(std::make_index_sequence<count>{});

    const void* address = addrs[index];
    for(std::size_t i = 0; i < sizeof(unknown.bytes); ++i) {
        if(address == &unknown.bytes[i]) {
            return i;
        }
    }
    std::unreachable();
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

    using type = field_type<Object, I>;

    constexpr auto&& value() {
        return field_of<I>(object);
    }

    constexpr static std::size_t index() {
        return I;
    }

    constexpr static std::string_view name() {
        return field_name<I, Object>();
    }

    constexpr static std::size_t offset() {
        return field_offset<Object>(I);
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

}  // namespace kota::meta
