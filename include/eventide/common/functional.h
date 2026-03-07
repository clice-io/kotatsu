#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace eventide {

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
    requires std::is_same_v<Class, typename MemFn::ClassType>;
};

template <typename Sign>
class function_ref {
    static_assert(false, "Sign must be a function type");
};

template <typename R, typename... Args>
class function_ref<R(Args...)> {
public:
    using Sign = R(Args...);

    using Erased = union {
        void* ctx;
        Sign* fn;
    };

    function_ref(const function_ref&) = default;
    function_ref(function_ref&&) = default;

    function_ref& operator=(const function_ref&) = default;
    function_ref& operator=(function_ref&&) = default;

private:
    constexpr function_ref(R (*proxy)(const function_ref*, Args&...), Erased ctx) noexcept :
        proxy(proxy), erased(ctx) {}

    template <typename Class>
        requires std::is_lvalue_reference_v<Class&&> && std::is_invocable_r_v<R, Class, Args...>
    constexpr static function_ref make(Class&& invokable) {
        if constexpr(std::is_convertible_v<Class&&, Sign*>) {
            return function_ref(static_cast<Sign*>(std::forward<Class>(invokable)));
        } else {
            return function_ref(std::forward<Class>(invokable),
                                mem_fn<&std::remove_cvref_t<Class>::operator()>{});
        }
    }

    template <typename Class, typename MemFn>
        requires is_mem_fn_of<Class, MemFn>
    constexpr function_ref(Class* invokable, MemFn) noexcept :
        function_ref(
            [](const function_ref* self, Args&... args) -> R {
                return (static_cast<Class*>(self->erased.ctx)->*MemFn::get())(
                    static_cast<Args>(args)...);
            },
            Erased{.ctx = invokable}) {}

public:
    template <auto MemFnPointer, typename Class, typename Mem>
    friend function_ref<typename Mem::FunctionType> bind_ref(Class&& obj);

    constexpr function_ref(Sign* invokable) noexcept :
        function_ref(
            [](const function_ref* self, Args&... args) -> R {
                Sign* fn = self->erased.fn;
                return (*fn)(static_cast<Args>(args)...);
            },
            Erased{.fn = invokable}) {};

    template <typename Class, typename MemFn>
        requires std::is_lvalue_reference_v<Class&&>
    constexpr function_ref(Class&& invokable, MemFn) noexcept : function_ref(&invokable, MemFn{}) {}

    template <typename Class>
        requires (!std::is_same_v<std::remove_cvref_t<Class>, function_ref>)
    constexpr function_ref(Class&& invokable) noexcept :
        function_ref(make(std::forward<Class>(invokable))) {}

    template <typename... CallArgs>
    constexpr R operator()(CallArgs&&... args) const {
        static_assert(
            requires(Sign* fn, CallArgs&&... args) { fn(std::forward<CallArgs>(args)...); },
            "Invokable object must be callable with the given arguments");
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
    constexpr static uint64_t sbo_magic = 0x0721'BEEF'CAFE;

    using Storage = union {
        alignas(sbo_align) std::byte sbo[sbo_size];

        struct {
            uint64_t magic;
            Deleter* deleter;
            Erased erased;
        };
    };

    template <typename T>
    constexpr static bool sbo_eligible =
        sizeof(T) <= sbo_size && alignof(T) <= sbo_align && std::is_trivially_copyable_v<T>;

    function(const function&) = delete;

    function(function&& other) noexcept {
        this->proxy = std::exchange(other.proxy, nullptr);
        if(other.storage.magic == sbo_magic) {
            this->storage.magic = std::exchange(other.storage.magic, 0);
            this->storage.deleter = std::exchange(other.storage.deleter, nullptr);
            this->storage.erased = std::exchange(other.storage.erased, Erased{});
        } else {
            std::memcpy(this->storage.sbo, other.storage.sbo, sizeof(this->storage.sbo));
        }
    }

    function& operator=(const function&) = delete;

    function& operator=(function&& other) noexcept {
        if(this == &other) {
            return *this;
        }
        this->~function();
        return *new (this) function(std::move(other));
    }

    ~function() {
        if(this->storage.magic == sbo_magic && this->storage.deleter) {
            this->storage.deleter(this);
        }
    }

private:
    constexpr function(R (*proxy)(const function*, Args&...)) noexcept : proxy(proxy), storage() {}

    constexpr function(R (*proxy)(const function*, Args&...), Storage storage) noexcept :
        proxy(proxy), storage(storage) {}

    template <typename Class>
    constexpr static function make(Class&& invokable) {
        if constexpr(std::is_convertible_v<Class&&, Sign*>) {
            return function(static_cast<Sign*>(std::forward<Class>(invokable)));
        } else {
            return function(std::forward<Class>(invokable),
                            mem_fn<&std::remove_cvref_t<Class>::operator()>{});
        }
    }

    template <typename Class, typename MemFn, typename ClassType = std::remove_cvref_t<Class>>
        requires sbo_eligible<ClassType> && is_mem_fn_of<ClassType, MemFn>
    constexpr function(Class&& invokable, MemFn) noexcept :
        function([](const function* self, Args&... args) -> R {
            return (self->storage_as<ClassType>()->*MemFn::get())(static_cast<Args>(args)...);
        }) {
        std::construct_at(reinterpret_cast<ClassType*>(this->storage.sbo),
                          std::forward<Class>(invokable));
    }

    template <typename Class, typename MemFn, typename ClassType = std::remove_cvref_t<Class>>
        requires (!sbo_eligible<ClassType>) && is_mem_fn_of<ClassType, MemFn>
    constexpr function(Class&& invokable, MemFn) noexcept :
        function(
            [](const function* self, Args&... args) -> R {
                return (static_cast<ClassType*>(self->storage.erased.ctx)->*MemFn::get())(
                    static_cast<Args>(args)...);
    },
            Storage{.magic = sbo_magic,
                    .deleter =
                        [](function* self) {
                            delete static_cast<ClassType*>(self->storage.erased.ctx);
                        },
                    .erased = Erased{
                        .ctx = new ClassType(std::forward<Class>(invokable)),
                    }}) {}

public:
    template <auto MemFnPointer, typename Class, typename Mem>
    friend function<typename Mem::FunctionType> bind(Class&& obj);

    constexpr function(Sign* invokable) noexcept :
        function(
            [](const function* self, Args&... args) -> R {
                Sign* fn = self->storage.erased.fn;
                return (*fn)(static_cast<Args>(args)...);
            },
            Storage{.magic = sbo_magic,
                    .deleter =
                        [](function* self) {
                            // No-op deleter for raw function pointers
                        },
                    .erased = Erased{.fn = invokable}}) {};

    template <typename Class>
        requires (!std::is_same_v<std::remove_cvref_t<Class>, function>) &&
                 std::is_invocable_r_v<R, Class, Args...>
    constexpr function(Class&& invokable) noexcept :
        function(make(std::forward<Class>(invokable))) {}

    template <typename... CallArgs>
    constexpr R operator()(CallArgs&&... args) const {
        static_assert(
            requires(Sign* fn, CallArgs&&... args) { fn(std::forward<CallArgs>(args)...); },
            "Invokable object must be callable with the given arguments");
        return proxy(this, args...);
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
    R (*proxy)(const function*, Args&...);
};

template <auto MemFnPointer, typename Class, typename Mem = mem_fn<MemFnPointer>>
function_ref<typename Mem::FunctionType> bind_ref(Class&& obj) {
    return function_ref<typename Mem::FunctionType>(std::forward<Class>(obj), Mem{});
}

template <auto MemFnPointer, typename Class, typename Mem = mem_fn<MemFnPointer>>
function<typename Mem::FunctionType> bind(Class&& obj) {
    return function<typename Mem::FunctionType>(std::forward<Class>(obj), Mem{});
}

}  // namespace eventide
