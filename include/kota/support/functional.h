#pragma once
#include <cassert>
#include <cstddef>
#include <functional>
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

template <typename Ret, typename Fn, typename... Args>
constexpr Ret invoke_ret(Fn&& fn, Args&&... args) {
    if constexpr(std::is_void_v<Ret>) {
        std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);
    } else {
        return std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);
    }
}

template <typename Sign>
class function_ref {
    static_assert(false, "Sign must be a function type");
};

template <typename R, typename... Args>
class function_ref<R(Args...)> {
public:
    using Sign = R(Args...);

    using Erased = union {
        const void* ctx;
        Sign* fn;
    };

    function_ref(const function_ref&) = default;
    function_ref(function_ref&&) = default;

    function_ref& operator=(const function_ref&) = default;
    function_ref& operator=(function_ref&&) = default;

private:
    constexpr function_ref(R (*proxy)(const function_ref*, Args&...), Erased ctx) noexcept :
        proxy{proxy}, erased{ctx} {}

    template <typename Class, typename MemFn, typename ClassType = std::remove_reference_t<Class>>
        requires std::is_lvalue_reference_v<Class&&> && is_mem_fn_of<ClassType, MemFn> &&
                 std::is_invocable_r_v<R, decltype(MemFn::get()), ClassType&, Args...>
    constexpr static function_ref make(Class&& invocable, MemFn) noexcept {
        return function_ref(
            [](const function_ref* self, Args&... args) -> R {
                auto& fn = *const_cast<ClassType*>(static_cast<const ClassType*>(self->erased.ctx));
                return invoke_ret<R>(MemFn::get(), fn, static_cast<Args&&>(args)...);
            },
            Erased{.ctx = &invocable});
    }

    constexpr static function_ref make(Sign* invocable) noexcept {
        return function_ref(
            [](const function_ref* self, Args&... args) -> R {
                Sign* fn = self->erased.fn;
                return (*fn)(static_cast<Args&&>(args)...);
            },
            Erased{.fn = invocable});
    }

    template <typename Class>
    constexpr static function_ref make(Class&& invocable) {
        if constexpr(std::is_convertible_v<Class&&, Sign*>) {
            return make(static_cast<Sign*>(std::forward<Class>(invocable)));
        } else {
            using ClassType = std::remove_reference_t<Class>;
            return function_ref(
                [](const function_ref* self, Args&... args) -> R {
                    auto& fn =
                        *const_cast<ClassType*>(static_cast<const ClassType*>(self->erased.ctx));
                    return invoke_ret<R>(fn, static_cast<Args&&>(args)...);
                },
                Erased{.ctx = &invocable});
        }
    }

public:
    template <auto MemFnPointer, typename Class, typename Mem>
        requires std::is_lvalue_reference_v<Class&&>
    friend constexpr function_ref<typename Mem::FunctionType> bind_ref(Class&& obj);

    constexpr function_ref(Sign* invocable) noexcept : function_ref(make(invocable)) {}

    template <typename Class>
        requires (!std::is_same_v<std::remove_cvref_t<Class>, function_ref>) &&
                 std::is_lvalue_reference_v<Class&&> && std::is_invocable_r_v<R, Class, Args...>
    constexpr function_ref(Class&& invocable) noexcept :
        function_ref(make(std::forward<Class>(invocable))) {}

    template <typename... CallArgs>
    constexpr R operator()(CallArgs&&... args) const {
        static_assert(
            requires(Sign* fn, CallArgs&&... call_args) {
                fn(std::forward<CallArgs>(call_args)...);
            },
            "invocable object must be callable with the given arguments");
        return proxy(this, args...);
    }

private:
    R (*proxy)(const function_ref*, Args&...);
    Erased erased;
};

template <typename Sign>
class function {
    static_assert(false, "Sign must be a function type");
};

template <typename R, typename... Args>
class function<R(Args...)> {
public:
    using Sign = R(Args...);

    using Erased = union {
        void* ctx;
        Sign* fn;
    };

    using Deleter = void(function*);

    constexpr static size_t sbo_size = 24;
    constexpr static size_t sbo_align = alignof(std::max_align_t);

    using Storage = union {
        alignas(sbo_align) std::byte sbo[sbo_size];
        Erased erased;
    };

    struct vtable {
        R (*proxy)(function*, Args&...);
        Deleter* deleter;
    };

    template <typename T>
    constexpr static bool sbo_eligible = sizeof(T) <= sbo_size && alignof(T) <= sbo_align;

    function(const function&) = delete;

    constexpr function(function&& other) noexcept {
        this->vptr = std::exchange(other.vptr, nullptr);
        this->storage = std::exchange(other.storage, Storage{});
    }

    function& operator=(const function&) = delete;

    constexpr function& operator=(function&& other) noexcept {
        if(this == &other) {
            return *this;
        }
        this->~function();
        return *new (this) function(std::move(other));
    }

    constexpr ~function() {
        if(vptr && vptr->deleter) {
            vptr->deleter(this);
        }
    }

private:
    constexpr function(const vtable* vptr, Storage storage = {}) noexcept :
        storage{storage}, vptr{vptr} {}

    constexpr static function make(Sign* invocable) noexcept {
        constexpr static vtable vt = {
            [](function* self, Args&... args) -> R {
                Sign* fn = self->storage.erased.fn;
                return (*fn)(static_cast<Args&&>(args)...);
            },
            nullptr  // No-op deleter for raw function pointers
        };
        return function(&vt, Storage{.erased = Erased{.fn = invocable}});
    }

    template <typename Class, typename MemFn, typename ClassType = std::remove_cvref_t<Class>>
        requires sbo_eligible<ClassType> && is_mem_fn_of<ClassType, MemFn>
    constexpr static function make(Class&& invocable, MemFn) {
        if consteval {
            constexpr static vtable vt = {
                [](function* self, Args&... args) -> R {
                    return (static_cast<ClassType*>(self->storage.erased.ctx)->*MemFn::get())(
                        static_cast<Args&&>(args)...);
                },
                [](function* self) { delete static_cast<ClassType*>(self->storage.erased.ctx); }};

            return function(
                &vt,
                Storage{.erased = Erased{.ctx = new ClassType(std::forward<Class>(invocable))}});
        } else {
            constexpr static vtable vt = {
                [](function* self, Args&... args) -> R {
                    return (self->storage_as<ClassType>()->*MemFn::get())(
                        static_cast<Args&&>(args)...);
                },
                [](function* self) { self->storage_as<ClassType>()->~ClassType(); }};
            Storage storage{};
            new (storage.sbo) ClassType(std::forward<Class>(invocable));
            return function(&vt, storage);
        }
    }

    template <typename Class, typename MemFn, typename ClassType = std::remove_cvref_t<Class>>
        requires (!sbo_eligible<ClassType>) && is_mem_fn_of<ClassType, MemFn>
    constexpr static function make(Class&& invocable, MemFn) {
        constexpr static vtable vt = {
            [](function* self, Args&... args) -> R {
                return (static_cast<ClassType*>(self->storage.erased.ctx)->*MemFn::get())(
                    static_cast<Args&&>(args)...);
            },
            [](function* self) { delete static_cast<ClassType*>(self->storage.erased.ctx); }};

        return function(
            &vt,
            Storage{.erased = Erased{.ctx = new ClassType(std::forward<Class>(invocable))}});
    }

    template <typename Class>
    constexpr static function make(Class&& invocable) {
        if constexpr(std::is_convertible_v<Class&&, Sign*>) {
            return make(static_cast<Sign*>(std::forward<Class>(invocable)));
        } else {
            using ClassType = std::remove_cvref_t<Class>;
            if constexpr(sbo_eligible<ClassType>) {
                if consteval {
                    constexpr static vtable vt = {
                        [](function* self, Args&... args) -> R {
                            auto& fn = *static_cast<ClassType*>(self->storage.erased.ctx);
                            return invoke_ret<R>(fn, static_cast<Args&&>(args)...);
                        },
                        [](function* self) {
                            delete static_cast<ClassType*>(self->storage.erased.ctx);
                        }};

                    return function(&vt,
                                    Storage{.erased = Erased{.ctx = new ClassType(
                                                                 std::forward<Class>(invocable))}});
                } else {
                    constexpr static vtable vt = {
                        [](function* self, Args&... args) -> R {
                            auto& fn = *self->storage_as<ClassType>();
                            return invoke_ret<R>(fn, static_cast<Args&&>(args)...);
                        },
                        [](function* self) { self->storage_as<ClassType>()->~ClassType(); }};
                    Storage storage{};
                    new (storage.sbo) ClassType(std::forward<Class>(invocable));
                    return function(&vt, storage);
                }
            } else {
                constexpr static vtable vt = {
                    [](function* self, Args&... args) -> R {
                        auto& fn = *static_cast<ClassType*>(self->storage.erased.ctx);
                        return invoke_ret<R>(fn, static_cast<Args&&>(args)...);
                    },
                    [](function* self) {
                        delete static_cast<ClassType*>(self->storage.erased.ctx);
                    }};

                return function(&vt,
                                Storage{.erased = Erased{
                                            .ctx = new ClassType(std::forward<Class>(invocable))}});
            }
        }
    }

public:
    template <auto MemFnPointer, typename Class, typename Mem>
    friend constexpr function<typename Mem::FunctionType> bind(Class&& obj);

    template <typename Class>
        requires (!std::is_same_v<std::remove_cvref_t<Class>, function>) &&
                 std::is_invocable_r_v<R, Class, Args...>
    constexpr function(Class&& invocable) : function(make(std::forward<Class>(invocable))) {}

    template <typename... CallArgs>
    constexpr R operator()(CallArgs&&... args) {
        static_assert(
            requires(Sign* fn, CallArgs&&... call_args) {
                fn(std::forward<CallArgs>(call_args)...);
            },
            "invocable object must be callable with the given arguments");
        assert(vptr && "Attempting to call an empty function object");
        return vptr->proxy(this, args...);
    }

private:
    template <typename Class>
    const Class* storage_as() const {
        return std::launder(reinterpret_cast<const Class*>(this->storage.sbo));
    }

    template <typename Class>
    Class* storage_as() {
        return std::launder(reinterpret_cast<Class*>(this->storage.sbo));
    }

    Storage storage;
    const vtable* vptr;
};

template <typename R, typename... Args>
class function<R(Args...) const> {
public:
    using Sign = R(Args...);

    using Erased = union {
        const void* ctx;
        Sign* fn;
    };

    using Deleter = void(function*);

    constexpr static size_t sbo_size = 24;
    constexpr static size_t sbo_align = alignof(std::max_align_t);

    using Storage = union {
        alignas(sbo_align) std::byte sbo[sbo_size];
        Erased erased;
    };

    struct vtable {
        R (*proxy)(const function*, Args&...);
        Deleter* deleter;
    };

    template <typename T>
    constexpr static bool sbo_eligible = sizeof(T) <= sbo_size && alignof(T) <= sbo_align;

    function(const function&) = delete;

    constexpr function(function&& other) noexcept {
        this->vptr = std::exchange(other.vptr, nullptr);
        this->storage = std::exchange(other.storage, Storage{});
    }

    function& operator=(const function&) = delete;

    constexpr function& operator=(function&& other) noexcept {
        if(this == &other) {
            return *this;
        }
        this->~function();
        return *new (this) function(std::move(other));
    }

    constexpr ~function() {
        if(vptr && vptr->deleter) {
            vptr->deleter(this);
        }
    }

private:
    constexpr function(const vtable* vptr, Storage storage = {}) noexcept :
        storage{storage}, vptr{vptr} {}

    constexpr static function make(Sign* invocable) noexcept {
        constexpr static vtable vt = {
            [](const function* self, Args&... args) -> R {
                Sign* fn = self->storage.erased.fn;
                return (*fn)(static_cast<Args&&>(args)...);
            },
            nullptr  // No-op deleter for raw function pointers
        };
        return function(&vt, Storage{.erased = Erased{.fn = invocable}});
    }

    template <typename Class, typename MemFn, typename ClassType = std::remove_cvref_t<Class>>
        requires sbo_eligible<ClassType> && is_mem_fn_of<ClassType, MemFn> &&
                 std::is_invocable_r_v<R, decltype(MemFn::get()), const ClassType&, Args...>
    constexpr static function make(Class&& invocable, MemFn) {
        if consteval {
            constexpr static vtable vt = {
                [](const function* self, Args&... args) -> R {
                    return (static_cast<const ClassType*>(self->storage.erased.ctx)->*MemFn::get())(
                        static_cast<Args&&>(args)...);
                },
                [](function* self) {
                    delete static_cast<const ClassType*>(self->storage.erased.ctx);
                }};

            return function(
                &vt,
                Storage{.erased = Erased{.ctx = new ClassType(std::forward<Class>(invocable))}});
        } else {
            constexpr static vtable vt = {
                [](const function* self, Args&... args) -> R {
                    return (self->storage_as<ClassType>()->*MemFn::get())(
                        static_cast<Args&&>(args)...);
                },
                [](function* self) { self->storage_as<ClassType>()->~ClassType(); }};
            Storage storage{};
            new (storage.sbo) ClassType(std::forward<Class>(invocable));
            return function(&vt, storage);
        }
    }

    template <typename Class, typename MemFn, typename ClassType = std::remove_cvref_t<Class>>
        requires (!sbo_eligible<ClassType>) && is_mem_fn_of<ClassType, MemFn> &&
                 std::is_invocable_r_v<R, decltype(MemFn::get()), const ClassType&, Args...>
    constexpr static function make(Class&& invocable, MemFn) {
        constexpr static vtable vt = {
            [](const function* self, Args&... args) -> R {
                return (static_cast<const ClassType*>(self->storage.erased.ctx)->*MemFn::get())(
                    static_cast<Args&&>(args)...);
            },
            [](function* self) { delete static_cast<const ClassType*>(self->storage.erased.ctx); }};

        return function(
            &vt,
            Storage{.erased = Erased{.ctx = new ClassType(std::forward<Class>(invocable))}});
    }

    template <typename Class>
    constexpr static function make(Class&& invocable) {
        if constexpr(std::is_convertible_v<Class&&, Sign*>) {
            return make(static_cast<Sign*>(std::forward<Class>(invocable)));
        } else {
            using ClassType = std::remove_cvref_t<Class>;
            if constexpr(sbo_eligible<ClassType>) {
                if consteval {
                    constexpr static vtable vt = {
                        [](const function* self, Args&... args) -> R {
                            auto& fn = *static_cast<const ClassType*>(self->storage.erased.ctx);
                            return invoke_ret<R>(fn, static_cast<Args&&>(args)...);
                        },
                        [](function* self) {
                            delete static_cast<const ClassType*>(self->storage.erased.ctx);
                        }};

                    return function(&vt,
                                    Storage{.erased = Erased{.ctx = new ClassType(
                                                                 std::forward<Class>(invocable))}});
                } else {
                    constexpr static vtable vt = {
                        [](const function* self, Args&... args) -> R {
                            auto& fn = *self->storage_as<ClassType>();
                            return invoke_ret<R>(fn, static_cast<Args&&>(args)...);
                        },
                        [](function* self) { self->storage_as<ClassType>()->~ClassType(); }};
                    Storage storage{};
                    new (storage.sbo) ClassType(std::forward<Class>(invocable));
                    return function(&vt, storage);
                }
            } else {
                constexpr static vtable vt = {
                    [](const function* self, Args&... args) -> R {
                        auto& fn = *static_cast<const ClassType*>(self->storage.erased.ctx);
                        return invoke_ret<R>(fn, static_cast<Args&&>(args)...);
                    },
                    [](function* self) {
                        delete static_cast<const ClassType*>(self->storage.erased.ctx);
                    }};

                return function(&vt,
                                Storage{.erased = Erased{
                                            .ctx = new ClassType(std::forward<Class>(invocable))}});
            }
        }
    }

public:
    template <typename Class>
        requires (!std::is_same_v<std::remove_cvref_t<Class>, function>) &&
                 std::is_invocable_r_v<R, const std::remove_reference_t<Class>&, Args...>
    constexpr function(Class&& invocable) : function(make(std::forward<Class>(invocable))) {}

    template <typename... CallArgs>
    constexpr R operator()(CallArgs&&... args) const {
        static_assert(
            requires(Sign* fn, CallArgs&&... call_args) {
                fn(std::forward<CallArgs>(call_args)...);
            },
            "invocable object must be callable with the given arguments");
        assert(vptr && "Attempting to call an empty function object");
        return vptr->proxy(this, args...);
    }

private:
    template <typename Class>
    const Class* storage_as() const {
        return std::launder(reinterpret_cast<const Class*>(this->storage.sbo));
    }

    template <typename Class>
    Class* storage_as() {
        return std::launder(reinterpret_cast<Class*>(this->storage.sbo));
    }

    Storage storage;
    const vtable* vptr;
};

template <auto MemFnPointer, typename Class, typename Mem = mem_fn<MemFnPointer>>
    requires std::is_lvalue_reference_v<Class&&>
constexpr function_ref<typename Mem::FunctionType> bind_ref(Class&& obj) {
    return function_ref<typename Mem::FunctionType>::make(std::forward<Class>(obj), Mem{});
}

template <auto MemFnPointer, typename Class, typename Mem = mem_fn<MemFnPointer>>
constexpr function<typename Mem::FunctionType> bind(Class&& obj) {
    return function<typename Mem::FunctionType>::make(std::forward<Class>(obj), Mem{});
}

}  // namespace kota
