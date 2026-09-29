#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace kota {

template <auto V, typename T = decltype(V)>
struct mem_fn {
    static_assert(std::is_member_function_pointer_v<T>, "V must be a member function pointer");
};

template <auto V, typename Class, typename Ret, typename... Args>
    requires std::is_member_function_pointer_v<decltype(V)>
struct mem_fn<V, Ret (Class::*)(Args...)> {
    using ClassType = Class;
    using ClassFunctionType = Ret (Class::*)(Args...);
    using FunctionType = Ret(Args...);

    constexpr static ClassFunctionType get() {
        return V;
    }
};

template <auto V, typename Class, typename Ret, typename... Args>
    requires std::is_member_function_pointer_v<decltype(V)>
struct mem_fn<V, Ret (Class::*)(Args...) const> {
    using ClassType = Class;
    using ClassFunctionType = Ret (Class::*)(Args...) const;
    using FunctionType = Ret(Args...);

    constexpr static ClassFunctionType get() {
        return V;
    }
};

template <typename Class, typename MemFn>
concept is_mem_fn_of = requires {
    typename MemFn::ClassType;
    requires std::is_same_v<std::remove_cv_t<Class>, typename MemFn::ClassType>;
};

template <typename Sign>
class function_ref {
    static_assert(false, "Sign must be a function type");
};

/// A reference to a callable: it neither owns nor copies it, so the callable must outlive
/// every call. A function pointer, including a lambda without captures that converts to one,
/// is kept by value instead.
template <typename R, typename... Args>
class function_ref<R(Args...)> {
public:
    constexpr function_ref(R (*fn)(Args...)) noexcept : call(&call_pointer), bound{.fn = fn} {}

    template <typename Class>
        requires (!std::is_same_v<std::remove_cvref_t<Class>, function_ref>) &&
                 std::is_lvalue_reference_v<Class&&> && std::is_invocable_r_v<R, Class, Args...>
    constexpr function_ref(Class&& invocable) noexcept {
        if constexpr(std::is_convertible_v<Class&&, R (*)(Args...)>) {
            call = &call_pointer;
            bound.fn = invocable;
        } else {
            call = &call_object<std::remove_reference_t<Class>>;
            bound.object = std::addressof(invocable);
        }
    }

    constexpr R operator()(Args... args) const {
        return call(bound, std::forward<Args>(args)...);
    }

private:
    template <auto MemFnPointer, typename Class, typename Mem>
        requires std::is_lvalue_reference_v<Class&&> &&
                 is_mem_fn_of<std::remove_cvref_t<Class>, Mem>
    friend constexpr function_ref<typename Mem::FunctionType> bind_ref(Class&& obj);

    union Bound {
        const void* object;
        R (*fn)(Args...);
    };

    using Call = R (*)(Bound, Args&&...);

    constexpr function_ref(Call call, Bound bound) noexcept : call(call), bound(bound) {}

    constexpr static R call_pointer(Bound bound, Args&&... args) {
        return bound.fn(std::forward<Args>(args)...);
    }

    /// Calls the object of type T, possibly const, that `bound` points to.
    template <typename T>
    constexpr static R call_object(Bound bound, Args&&... args) {
        auto& object = *static_cast<T*>(const_cast<void*>(bound.object));
        return std::invoke_r<R>(object, std::forward<Args>(args)...);
    }

    /// Calls member function `MemFnPointer` on the object of type T `bound` points to.
    template <auto MemFnPointer, typename T>
    constexpr static R call_member(Bound bound, Args&&... args) {
        auto& object = *static_cast<T*>(const_cast<void*>(bound.object));
        return std::invoke_r<R>(MemFnPointer, object, std::forward<Args>(args)...);
    }

    Call call;
    Bound bound;
};

template <typename Sign>
class function {
    static_assert(false, "Sign must be a function type");
};

namespace detail {

/// What `function<R(Args...)>` and `function<R(Args...) const>` share. `Const` says whether
/// the callable is called as const, and so whether calling the function is const.
template <bool Const, typename R, typename... Args>
class function_impl {
    template <typename T>
    using target_t = std::conditional_t<Const, const T, T>;

public:
    constexpr static std::size_t sbo_size = 24;
    constexpr static std::size_t sbo_align = alignof(std::max_align_t);

    /// Whether a callable of type T is kept in the function itself rather than on the heap:
    /// it fits, and moving it cannot throw, as moving the function cannot.
    template <typename T>
    constexpr static bool sbo_eligible =
        sizeof(T) <= sbo_size && alignof(T) <= sbo_align && std::is_nothrow_move_constructible_v<T>;

    template <typename Class>
        requires (!std::is_base_of_v<function_impl, std::remove_cvref_t<Class>>) &&
                 std::is_invocable_r_v<R, target_t<std::remove_cvref_t<Class>>&, Args...>
    constexpr function_impl(Class&& invocable) {
        using T = std::remove_cvref_t<Class>;
        if constexpr(std::is_convertible_v<Class&&, R (*)(Args...)>) {
            buffer.fn = static_cast<R (*)(Args...)>(std::forward<Class>(invocable));
            ops = &pointer_ops;
        } else if constexpr(sbo_eligible<T>) {
            ::new (static_cast<void*>(buffer.bytes)) T(std::forward<Class>(invocable));
            ops = &inline_ops<T>;
        } else {
            buffer.heap = new T(std::forward<Class>(invocable));
            ops = &heap_ops<T>;
        }
    }

    function_impl(const function_impl&) = delete;
    function_impl& operator=(const function_impl&) = delete;

    constexpr function_impl(function_impl&& other) noexcept :
        ops(std::exchange(other.ops, nullptr)) {
        take_buffer(other);
    }

    constexpr function_impl& operator=(function_impl&& other) noexcept {
        if(this != &other) {
            destroy();
            ops = std::exchange(other.ops, nullptr);
            take_buffer(other);
        }
        return *this;
    }

    constexpr ~function_impl() {
        destroy();
    }

    constexpr R operator()(Args... args)
        requires (!Const) {
        assert(ops && "Attempting to call an empty function object");
        return ops->call(buffer, std::forward<Args>(args)...);
    }

    constexpr R operator()(Args... args) const
        requires Const {
        assert(ops && "Attempting to call an empty function object");
        return ops->call(buffer, std::forward<Args>(args)...);
    }

private:
    union Buffer {
        alignas(sbo_align) std::byte bytes[sbo_size];
        void* heap;
        R (*fn)(Args...);
    };

    using BufferRef = std::conditional_t<Const, const Buffer&, Buffer&>;

    /// How to call, move and destroy the kind of callable the buffer holds.
    struct Ops {
        R (*call)(BufferRef, Args&&...);
        /// Moves the callable from the second buffer into the first and destroys it there;
        /// null when copying the buffer's bytes moves it.
        void (*relocate)(Buffer&, Buffer&) noexcept;
        /// Null when there is nothing to destroy.
        void (*destroy)(Buffer&) noexcept;
    };

    constexpr static R call_pointer(BufferRef buffer, Args&&... args) {
        return buffer.fn(std::forward<Args>(args)...);
    }

    template <typename T>
    constexpr static R call_inline(BufferRef buffer, Args&&... args) {
        auto& target = *std::launder(reinterpret_cast<target_t<T>*>(buffer.bytes));
        return std::invoke_r<R>(target, std::forward<Args>(args)...);
    }

    template <typename T>
    static void relocate_inline(Buffer& to, Buffer& from) noexcept {
        auto* source = std::launder(reinterpret_cast<T*>(from.bytes));
        ::new (static_cast<void*>(to.bytes)) T(std::move(*source));
        source->~T();
    }

    template <typename T>
    static void destroy_inline(Buffer& buffer) noexcept {
        std::launder(reinterpret_cast<T*>(buffer.bytes))->~T();
    }

    template <typename T>
    constexpr static R call_heap(BufferRef buffer, Args&&... args) {
        return std::invoke_r<R>(*static_cast<target_t<T>*>(buffer.heap),
                                std::forward<Args>(args)...);
    }

    template <typename T>
    static void destroy_heap(Buffer& buffer) noexcept {
        delete static_cast<T*>(buffer.heap);
    }

    constexpr static Ops pointer_ops = {&call_pointer, nullptr, nullptr};

    template <typename T>
    constexpr static Ops inline_ops = {
        &call_inline<T>,
        std::is_trivially_copyable_v<T> ? nullptr : &relocate_inline<T>,
        std::is_trivially_destructible_v<T> ? nullptr : &destroy_inline<T>,
    };

    template <typename T>
    constexpr static Ops heap_ops = {&call_heap<T>, nullptr, &destroy_heap<T>};

    /// Takes the callable in `other`'s buffer, whose ops this function now has.
    constexpr void take_buffer(function_impl& other) noexcept {
        if(ops != nullptr && ops->relocate != nullptr) {
            ops->relocate(buffer, other.buffer);
        } else {
            buffer = other.buffer;
        }
    }

    constexpr void destroy() noexcept {
        if(ops != nullptr && ops->destroy != nullptr) {
            ops->destroy(buffer);
        }
    }

    Buffer buffer{};
    /// Null once moved from.
    const Ops* ops;
};

}  // namespace detail

/// An owning callable of signature R(Args...), move-only. A small callable whose move cannot
/// throw lives inline; a larger one, on the heap.
template <typename R, typename... Args>
class function<R(Args...)> : public detail::function_impl<false, R, Args...> {
public:
    using detail::function_impl<false, R, Args...>::function_impl;
};

/// Like function<R(Args...)>, for a callable called as const, which lets the function itself
/// be called as const.
template <typename R, typename... Args>
class function<R(Args...) const> : public detail::function_impl<true, R, Args...> {
public:
    using detail::function_impl<true, R, Args...>::function_impl;
};

/// A function_ref calling member function `MemFnPointer` on `obj`, which must outlive it.
template <auto MemFnPointer, typename Class, typename Mem = mem_fn<MemFnPointer>>
    requires std::is_lvalue_reference_v<Class&&> && is_mem_fn_of<std::remove_cvref_t<Class>, Mem>
constexpr function_ref<typename Mem::FunctionType> bind_ref(Class&& obj) {
    using ref = function_ref<typename Mem::FunctionType>;
    using object_type = std::remove_reference_t<Class>;
    return ref(&ref::template call_member<MemFnPointer, object_type>,
               typename ref::Bound{.object = std::addressof(obj)});
}

/// A function owning `obj`, moved or copied in, that calls member function `MemFnPointer` on it.
template <auto MemFnPointer, typename Class, typename Mem = mem_fn<MemFnPointer>>
    requires is_mem_fn_of<std::remove_cvref_t<Class>, Mem>
constexpr function<typename Mem::FunctionType> bind(Class&& obj) {
    return [object = std::forward<Class>(obj)]<typename... Args>(Args&&... args) mutable {
        return std::invoke(MemFnPointer, object, std::forward<Args>(args)...);
    };
}

}  // namespace kota
