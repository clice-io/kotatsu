#pragma once

#include <concepts>
#include <coroutine>
#include <type_traits>
#include <utility>

#include "kota/async/runtime/frame.h"

namespace kota::detail {

template <typename T>
decltype(auto) get_awaiter(T&& value) {
    if constexpr(requires { std::forward<T>(value).operator co_await(); }) {
        return std::forward<T>(value).operator co_await();
    } else if constexpr(requires { operator co_await(std::forward<T>(value)); }) {
        return operator co_await(std::forward<T>(value));
    } else {
        return std::forward<T>(value);
    }
}

template <typename T>
using awaiter_t = decltype(get_awaiter(std::declval<T>()));

struct await_probe_promise : async_node {
    await_probe_promise() : async_node(NodeKind::Task) {}
};

using await_probe_handle = std::coroutine_handle<await_probe_promise>;

template <typename T>
concept awaiter = requires(T&& value, await_probe_handle handle) {
    { std::forward<T>(value).await_ready() } -> std::convertible_to<bool>;
    std::forward<T>(value).await_suspend(handle);
    std::forward<T>(value).await_resume();
};

template <typename T>
concept awaitable =
    requires(T&& value) { get_awaiter(std::forward<T>(value)); } && awaiter<awaiter_t<T>>;

template <typename T>
using await_result_t = decltype(std::declval<awaiter_t<T>>().await_resume());

}  // namespace kota::detail
