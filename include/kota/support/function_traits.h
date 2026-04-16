#pragma once

#include <concepts>
#include <memory>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace kota {

/// check if T is a function pointer type
template <typename T>
constexpr inline bool is_function_pointer_v = std::is_function_v<std::remove_pointer_t<T>>;

/// check if T is a functor type(has a unique operator())
template <typename T, typename U = void>
constexpr bool is_functor_v = false;

template <typename T>
constexpr inline bool is_functor_v<T, std::void_t<decltype(&T::operator())>> = true;

template <typename T, typename Tuple>
struct tuple_push_front;

template <typename T, typename... Ts>
struct tuple_push_front<T, std::tuple<Ts...>> {
    using type = std::tuple<T, Ts...>;
};

template <typename T, typename Tuple>
using tuple_push_front_t = typename tuple_push_front<T, Tuple>::type;

// traits for function types
template <typename Fn>
struct function_traits;

#define FUNCTION_TRAITS_SPECIALIZE(...)                                                            \
    template <typename R, typename... Args>                                                        \
    struct function_traits<R(Args...) __VA_ARGS__> {                                               \
        using return_type = R;                                                                     \
        using args_type = std::tuple<Args...>;                                                     \
        constexpr static std::size_t args_count = sizeof...(Args);                                 \
    };

FUNCTION_TRAITS_SPECIALIZE()
FUNCTION_TRAITS_SPECIALIZE(&)
FUNCTION_TRAITS_SPECIALIZE(const)
FUNCTION_TRAITS_SPECIALIZE(const&)
FUNCTION_TRAITS_SPECIALIZE(noexcept)
FUNCTION_TRAITS_SPECIALIZE(& noexcept)
FUNCTION_TRAITS_SPECIALIZE(const noexcept)
FUNCTION_TRAITS_SPECIALIZE(const& noexcept)

#undef FUNCTION_TRAITS_SPECIALIZE

template <typename T>
using function_return_t = typename function_traits<T>::return_type;

template <typename T>
using function_args_t = typename function_traits<T>::args_type;

template <typename T>
constexpr std::size_t function_args_count = function_traits<T>::args_count;

// traits for member pointers
template <typename T>
struct member_traits;

template <typename M, typename C>
struct member_traits<M C::*> {
    using member_type = M;
    using class_type = C;
};

template <typename T>
using member_type_t = typename member_traits<T>::member_type;

template <typename T>
using class_type_t = typename member_traits<T>::class_type;

// some traits for distinguishing between function pointers, member function pointers and
// functors
template <typename T, typename SFINAE = void>
struct callable_traits;

template <typename T>
struct callable_traits<T, std::enable_if_t<std::is_member_function_pointer_v<T>>> {
    using args_type = tuple_push_front_t<class_type_t<T>&, function_args_t<member_type_t<T>>>;
    using return_type = function_return_t<member_type_t<T>>;
};

template <typename T>
struct callable_traits<T, std::enable_if_t<is_function_pointer_v<T>>> {
    using args_type = function_args_t<std::remove_pointer_t<T>>;
    using return_type = function_return_t<std::remove_pointer_t<T>>;
};

template <typename T>
struct callable_traits<T, std::enable_if_t<is_functor_v<T>>> {
    using args_type = function_args_t<member_type_t<decltype(&T::operator())>>;
    using return_type = function_return_t<member_type_t<decltype(&T::operator())>>;
};

template <typename Callable>
using callable_args_t = typename callable_traits<Callable>::args_type;

template <typename Callable>
using callable_return_t = typename callable_traits<Callable>::return_type;

template <typename Callable>
constexpr std::size_t callable_args_count_v = std::tuple_size_v<callable_args_t<Callable>>;

}  // namespace kota
