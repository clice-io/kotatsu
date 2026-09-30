#pragma once

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "kota/support/function_traits.h"

namespace kota {

/// The class and the signature of member function `V`, without its qualifiers.
template <auto V>
struct mem_fn {
    static_assert(std::is_member_function_pointer_v<decltype(V)>,
                  "V must be a member function pointer");

    using class_type = class_type_t<decltype(V)>;
    using function_type = typename function_traits<member_type_t<decltype(V)>>::function_type;
};

/// Whether `MemFn`, a mem_fn, is a member function of Class, whatever its cv-qualifiers.
template <typename Class, typename MemFn>
concept is_mem_fn_of = std::is_same_v<std::remove_cv_t<Class>, typename MemFn::class_type>;

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
        } else if constexpr(std::is_function_v<std::remove_reference_t<Class>>) {
            // A function of another signature, called through its own pointer type.
            using Pointer = std::remove_reference_t<Class>*;
            call = &call_function<Pointer>;
            bound.function = reinterpret_cast<void (*)()>(&invocable);
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
                 is_mem_fn_of<std::remove_reference_t<Class>, Mem>
    friend constexpr function_ref<typename Mem::function_type> bind_ref(Class&& obj);

    union Bound {
        const void* object;
        R (*fn)(Args...);
        /// A function pointer of another signature, cast back before the call.
        void (*function)();
    };

    using Call = R (*)(Bound, Args&&...);

    constexpr function_ref(Call call, Bound bound) noexcept : call(call), bound(bound) {}

    constexpr static R call_pointer(Bound bound, Args&&... args) {
        return bound.fn(std::forward<Args>(args)...);
    }

    /// Calls the function of pointer type Pointer that `bound` holds.
    template <typename Pointer>
    static R call_function(Bound bound, Args&&... args) {
        return std::invoke_r<R>(reinterpret_cast<Pointer>(bound.function),
                                std::forward<Args>(args)...);
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
class ErasedCallable {
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
        requires (!std::is_base_of_v<ErasedCallable, std::remove_cvref_t<Class>>) &&
                 std::is_invocable_r_v<R, target_t<std::decay_t<Class>>&, Args...>
    constexpr ErasedCallable(Class&& invocable) {
        // A function decays to its pointer, which lives inline.
        using T = std::decay_t<Class>;
        if constexpr(std::is_convertible_v<Class&&, R (*)(Args...)>) {
            buffer.fn = static_cast<R (*)(Args...)>(std::forward<Class>(invocable));
            ops = pointer_ops();
        } else if constexpr(sbo_eligible<T>) {
            if consteval {
                // Constant evaluation has no placement new: the callable goes on the heap.
                buffer.heap = new T(std::forward<Class>(invocable));
                ops = heap_ops<T>();
            } else {
                ::new (static_cast<void*>(buffer.bytes)) T(std::forward<Class>(invocable));
                ops = inline_ops<T>();
            }
        } else {
            buffer.heap = new T(std::forward<Class>(invocable));
            ops = heap_ops<T>();
        }
    }

    ErasedCallable(const ErasedCallable&) = delete;
    ErasedCallable& operator=(const ErasedCallable&) = delete;

    constexpr ErasedCallable(ErasedCallable&& other) noexcept :
        ops(std::exchange(other.ops, empty_ops())) {
        take_buffer(other);
    }

    constexpr ErasedCallable& operator=(ErasedCallable&& other) noexcept {
        if(this != &other) {
            destroy();
            ops = std::exchange(other.ops, empty_ops());
            take_buffer(other);
        }
        return *this;
    }

    constexpr ~ErasedCallable() {
        destroy();
    }

    constexpr R operator()(Args... args)
        requires (!Const) {
        return ops->call(buffer, std::forward<Args>(args)...);
    }

    constexpr R operator()(Args... args) const
        requires Const {
        return ops->call(buffer, std::forward<Args>(args)...);
    }

private:
    union Buffer {
        alignas(sbo_align) std::byte bytes[sbo_size];
        void* heap;
        R (*fn)(Args...);
    };

    using BufferRef = std::conditional_t<Const, const Buffer&, Buffer&>;

    /// How to call, move and destroy the kind of callable the buffer holds. None is null:
    /// GCC does not compare an address with null in constant evaluation when null checks are
    /// sanitized.
    struct Ops {
        R (*call)(BufferRef, Args&&...);
        /// Moves the callable from the second buffer into the first and destroys it in the
        /// second.
        void (*relocate)(Buffer&, Buffer&) noexcept;
        void (*destroy)(Buffer&) noexcept;
    };

    /// Moves a callable whose bytes are all there is to it: a function pointer, a pointer to
    /// one on the heap, or a trivially copyable one inline.
    constexpr static void relocate_bytes(Buffer& to, Buffer& from) noexcept {
        to = from;
    }

    constexpr static void destroy_nothing(Buffer&) noexcept {}

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
    constexpr static void destroy_heap(Buffer& buffer) noexcept {
        delete static_cast<T*>(buffer.heap);
    }

    template <typename T>
    constexpr static auto inline_relocate() noexcept -> void (*)(Buffer&, Buffer&) noexcept {
        if constexpr(std::is_trivially_copyable_v<T>) {
            return &relocate_bytes;
        } else {
            return &relocate_inline<T>;
        }
    }

    template <typename T>
    constexpr static auto inline_destroy() noexcept -> void (*)(Buffer&) noexcept {
        if constexpr(std::is_trivially_destructible_v<T>) {
            return &destroy_nothing;
        } else {
            return &destroy_inline<T>;
        }
    }

    // The ops of each kind of callable are function-local constants: MSVC leaves a static
    // data member template zero-filled for some local lambda types (warning C4268).

    [[noreturn]] static R call_empty(BufferRef, Args&&...) {
        assert(false && "Attempting to call an empty function object");
        std::abort();
    }

    /// The ops of a function moved from, which holds nothing.
    constexpr const static Ops* empty_ops() noexcept {
        constexpr static Ops ops = {&call_empty, &relocate_bytes, &destroy_nothing};
        return &ops;
    }

    constexpr const static Ops* pointer_ops() noexcept {
        constexpr static Ops ops = {&call_pointer, &relocate_bytes, &destroy_nothing};
        return &ops;
    }

    template <typename T>
    constexpr const static Ops* inline_ops() noexcept {
        constexpr static Ops ops = {&call_inline<T>, inline_relocate<T>(), inline_destroy<T>()};
        return &ops;
    }

    template <typename T>
    constexpr const static Ops* heap_ops() noexcept {
        constexpr static Ops ops = {&call_heap<T>, &relocate_bytes, &destroy_heap<T>};
        return &ops;
    }

    /// Takes the callable in `other`'s buffer, whose ops this function now has.
    constexpr void take_buffer(ErasedCallable& other) noexcept {
        ops->relocate(buffer, other.buffer);
    }

    constexpr void destroy() noexcept {
        ops->destroy(buffer);
    }

    Buffer buffer{};
    /// empty_ops() once moved from.
    const Ops* ops;
};

}  // namespace detail

/// An owning callable of signature R(Args...), move-only. A small callable whose move cannot
/// throw lives in the function itself; any other, and every one in constant evaluation, on
/// the heap.
template <typename R, typename... Args>
class function<R(Args...)> : public detail::ErasedCallable<false, R, Args...> {
public:
    using detail::ErasedCallable<false, R, Args...>::ErasedCallable;
};

/// Like function<R(Args...)>, for a callable called as const, which lets the function itself
/// be called as const.
template <typename R, typename... Args>
class function<R(Args...) const> : public detail::ErasedCallable<true, R, Args...> {
public:
    using detail::ErasedCallable<true, R, Args...>::ErasedCallable;
};

/// A function_ref calling member function `MemFnPointer` on `obj`, which must outlive it.
template <auto MemFnPointer, typename Class, typename Mem = mem_fn<MemFnPointer>>
    requires std::is_lvalue_reference_v<Class&&> &&
             is_mem_fn_of<std::remove_reference_t<Class>, Mem>
constexpr function_ref<typename Mem::function_type> bind_ref(Class&& obj) {
    using ref = function_ref<typename Mem::function_type>;
    using object_type = std::remove_reference_t<Class>;
    return ref(&ref::template call_member<MemFnPointer, object_type>,
               typename ref::Bound{.object = std::addressof(obj)});
}

/// A function owning `obj`, moved or copied in, that calls member function `MemFnPointer` on it.
template <auto MemFnPointer, typename Class, typename Mem = mem_fn<MemFnPointer>>
    requires is_mem_fn_of<std::remove_reference_t<Class>, Mem>
constexpr function<typename Mem::function_type> bind(Class&& obj) {
    return [object = std::forward<Class>(obj)]<typename... Args>(
               Args&&... args) mutable -> decltype(auto) {
        return std::invoke(MemFnPointer, object, std::forward<Args>(args)...);
    };
}

}  // namespace kota
