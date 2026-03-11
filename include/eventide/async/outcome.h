#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace eventide {

template <typename T, typename E = void, typename C = void>
class outcome;

/// Concept: error types that participate in structured concurrency.
/// The framework calls should_propagate() to decide whether an error
/// cancels sibling tasks and propagates upward through aggregates.
template <typename E>
concept structured_error = std::movable<E> && requires(const E& e) {
    { e.should_propagate() } -> std::convertible_to<bool>;
};

/// Concept: cancellation types that participate in structured concurrency.
template <typename C>
concept structured_cancel = std::movable<C> && requires(const C& c) {
    { c.reason() } -> std::convertible_to<std::string_view>;
};

/// Optional extension: error types with human-readable messages.
template <typename E>
concept descriptive_error = structured_error<E> && requires(const E& e) {
    { e.message() } -> std::convertible_to<std::string_view>;
};

/// Optional extension: error types that support retry semantics.
template <typename E>
concept retriable_error = structured_error<E> && requires(const E& e) {
    { e.is_retriable() } -> std::convertible_to<bool>;
};

// ============================================================================
// Box types: wrap values to disambiguate variant alternatives
// ============================================================================

namespace detail {

template <typename T>
struct ok_box {
    T value;
};

template <>
struct ok_box<void> {};

template <typename E>
struct err_box {
    E value;
};

template <typename C>
struct cancel_box {
    C value;
};

// Storage type selection via partial specialization

template <typename T, typename E, typename C>
struct outcome_storage {
    using type = std::variant<ok_box<T>, err_box<E>, cancel_box<C>>;
};

template <typename T, typename E>
struct outcome_storage<T, E, void> {
    using type = std::variant<ok_box<T>, err_box<E>>;
};

template <typename T, typename C>
struct outcome_storage<T, void, C> {
    using type = std::variant<ok_box<T>, cancel_box<C>>;
};

template <typename T>
struct outcome_storage<T, void, void> {
    using type = ok_box<T>;
};

template <typename T, typename E, typename C>
using outcome_storage_t = typename outcome_storage<T, E, C>::type;

}  // namespace detail

// ============================================================================
// Factory tag types for constructing outcomes
// ============================================================================

template <typename E>
struct outcome_error_t {
    E value;
};

template <typename C>
struct outcome_cancel_t {
    C value;
};

template <typename E>
outcome_error_t<std::decay_t<E>> outcome_error(E&& e) {
    return {std::forward<E>(e)};
}

template <typename C>
outcome_cancel_t<std::decay_t<C>> outcome_cancelled(C&& c) {
    return {std::forward<C>(c)};
}

/// Tag for constructing void-value outcomes: co_return outcome_value();
struct outcome_ok_tag {};

inline outcome_ok_tag outcome_value() {
    return {};
}

// ============================================================================
// outcome_traits: compile-time introspection
// ============================================================================

template <typename T>
struct outcome_traits;

template <typename T, typename E, typename C>
struct outcome_traits<outcome<T, E, C>> {
    using value_type = T;
    using error_type = E;
    using cancel_type = C;
    constexpr static bool has_error_channel = !std::is_void_v<E>;
    constexpr static bool has_cancel_channel = !std::is_void_v<C>;
};

template <typename T>
constexpr bool is_outcome_v = false;

template <typename T, typename E, typename C>
constexpr bool is_outcome_v<outcome<T, E, C>> = true;

template <typename T>
constexpr bool has_cancel_channel_v = false;

template <typename T, typename E, typename C>
constexpr bool has_cancel_channel_v<outcome<T, E, C>> = !std::is_void_v<C>;

template <typename T>
constexpr bool has_error_channel_v = false;

template <typename T, typename E, typename C>
constexpr bool has_error_channel_v<outcome<T, E, C>> = !std::is_void_v<E>;

// ============================================================================
// outcome<T, E, C>: primary template (full three-state)
// ============================================================================

template <typename T, typename E, typename C>
class outcome {
    using storage_type = detail::outcome_storage_t<T, E, C>;
    storage_type storage;

public:
    // --- Value construction ---

    template <typename U = T>
        requires (!std::is_void_v<T>) && std::constructible_from<T, U&&> &&
                 (!is_outcome_v<std::decay_t<U>> || std::same_as<std::decay_t<U>, T>)
    outcome(U&& value) : storage(detail::ok_box<T>{T(std::forward<U>(value))}) {}

    outcome()
        requires std::is_void_v<T>
        : storage(detail::ok_box<void>{}) {}

    // --- Error construction ---

    template <typename U>
        requires (!std::is_void_v<E>) && std::constructible_from<E, U>
    outcome(outcome_error_t<U> e) : storage(detail::err_box<E>{E(std::move(e.value))}) {}

    // --- Cancel construction ---

    template <typename U>
        requires (!std::is_void_v<C>) && std::constructible_from<C, U>
    outcome(outcome_cancel_t<U> c) : storage(detail::cancel_box<C>{C(std::move(c.value))}) {}

    // --- Void-value construction from tag ---

    outcome(outcome_ok_tag)
        requires std::is_void_v<T>
        : storage(detail::ok_box<void>{}) {}

    // --- State queries ---

    bool has_value() const noexcept {
        return std::holds_alternative<detail::ok_box<T>>(storage);
    }

    bool has_error() const noexcept
        requires (!std::is_void_v<E>)
    {
        return std::holds_alternative<detail::err_box<E>>(storage);
    }

    bool is_cancelled() const noexcept
        requires (!std::is_void_v<C>)
    {
        return std::holds_alternative<detail::cancel_box<C>>(storage);
    }

    explicit operator bool() const noexcept {
        return has_value();
    }

    // --- Value access ---

    auto& value() &
        requires (!std::is_void_v<T>)
    {
        assert(has_value());
        return std::get<detail::ok_box<T>>(storage).value;
    }

    const auto& value() const&
        requires (!std::is_void_v<T>)
    {
        assert(has_value());
        return std::get<detail::ok_box<T>>(storage).value;
    }

    auto&& value() &&
        requires(!std::is_void_v<T>) {
            assert(has_value());
            return std::move(std::get<detail::ok_box<T>>(storage).value);
        }

        auto& operator*() &
            requires (!std::is_void_v<T>)
    {
        return value();
    }

    const auto& operator*() const&
        requires (!std::is_void_v<T>)
    {
        return value();
    }

    auto&& operator*() && requires(!std::is_void_v<T>) { return std::move(*this).value(); }

                          auto* operator-> ()
                              requires (!std::is_void_v<T>)
    {
        return &value();
    }

    const auto* operator->() const
        requires (!std::is_void_v<T>)
    {
        return &value();
    }

    // --- Error access ---

    auto& error() &
        requires (!std::is_void_v<E>)
    {
        assert(has_error());
        return std::get<detail::err_box<E>>(storage).value;
    }

    const auto& error() const&
        requires (!std::is_void_v<E>)
    {
        assert(has_error());
        return std::get<detail::err_box<E>>(storage).value;
    }

    auto&& error() &&
        requires(!std::is_void_v<E>) {
            assert(has_error());
            return std::move(std::get<detail::err_box<E>>(storage).value);
        }

        // --- Cancel access ---

        auto& cancellation() &
            requires (!std::is_void_v<C>)
    {
        assert(is_cancelled());
        return std::get<detail::cancel_box<C>>(storage).value;
    }

    const auto& cancellation() const&
        requires (!std::is_void_v<C>)
    {
        assert(is_cancelled());
        return std::get<detail::cancel_box<C>>(storage).value;
    }

    auto&& cancellation() && requires(!std::is_void_v<C>) {
        assert(is_cancelled());
        return std::move(std::get<detail::cancel_box<C>>(storage).value);
    }
};

// ============================================================================
// outcome<T, void, void>: pure value specialization (no variant overhead)
// ============================================================================

template <typename T>
class outcome<T, void, void> {
    detail::ok_box<T> storage;

public:
    template <typename U = T>
        requires (!std::is_void_v<T>) && std::constructible_from<T, U&&> &&
                 (!is_outcome_v<std::decay_t<U>> || std::same_as<std::decay_t<U>, T>)
    outcome(U&& value) : storage{T(std::forward<U>(value))} {}

    outcome()
        requires std::is_void_v<T>
    {}

    outcome(outcome_ok_tag)
        requires std::is_void_v<T>
    {}

    constexpr bool has_value() const noexcept {
        return true;
    }

    constexpr explicit operator bool() const noexcept {
        return true;
    }

    auto& value() &
        requires (!std::is_void_v<T>)
    {
        return storage.value;
    }

    const auto& value() const&
        requires (!std::is_void_v<T>)
    {
        return storage.value;
    }

    auto&& value() && requires(!std::is_void_v<T>) { return std::move(storage.value); }

        auto& operator*() &
            requires (!std::is_void_v<T>)
    {
        return value();
    }

    const auto& operator*() const&
        requires (!std::is_void_v<T>)
    {
        return value();
    }

    auto&& operator*() && requires(!std::is_void_v<T>) { return std::move(*this).value(); }

                          auto* operator-> ()
                              requires (!std::is_void_v<T>)
    {
        return &value();
    }

    const auto* operator->() const
        requires (!std::is_void_v<T>)
    {
        return &value();
    }
};

// ============================================================================
// erased_outcome: fat-pointer type erasure for the async_node layer
// ============================================================================

struct outcome_vtable {
    enum class kind : std::uint8_t { error, cancel };

    kind type;
    std::size_t size;
    std::size_t align;

    void (*destroy)(void* p) noexcept;
    void (*move_construct)(void* src, void* dst) noexcept;

    bool (*should_propagate)(const void* p) noexcept;
    std::string_view (*reason)(const void* p) noexcept;

    std::string_view (*message)(const void* p) noexcept;
    bool (*is_retriable)(const void* p) noexcept;
};

namespace detail {

template <structured_error E>
constexpr inline outcome_vtable error_vtable_for = {
    .type = outcome_vtable::kind::error,
    .size = sizeof(E),
    .align = alignof(E),
    .destroy = [](void* p) noexcept { static_cast<E*>(p)->~E(); },
    .move_construct = [](void* src,
                         void* dst) noexcept { new (dst) E(std::move(*static_cast<E*>(src))); },
    .should_propagate = [](const void* p) noexcept -> bool {
        return static_cast<const E*>(p)->should_propagate();
    },
    .reason = nullptr,
    .message =
        [] {
            if constexpr(descriptive_error<E>) {
                return +[](const void* p) noexcept -> std::string_view {
                    return static_cast<const E*>(p)->message();
                };
            } else {
                return static_cast<std::string_view (*)(const void*) noexcept>(nullptr);
            }
        }(),
    .is_retriable =
        [] {
            if constexpr(retriable_error<E>) {
                return +[](const void* p) noexcept -> bool {
                    return static_cast<const E*>(p)->is_retriable();
                };
            } else {
                return static_cast<bool (*)(const void*) noexcept>(nullptr);
            }
        }(),
};

template <structured_cancel C>
constexpr inline outcome_vtable cancel_vtable_for = {
    .type = outcome_vtable::kind::cancel,
    .size = sizeof(C),
    .align = alignof(C),
    .destroy = [](void* p) noexcept { static_cast<C*>(p)->~C(); },
    .move_construct = [](void* src,
                         void* dst) noexcept { new (dst) C(std::move(*static_cast<C*>(src))); },
    .should_propagate = [](const void*) noexcept -> bool { return true; },
    .reason = [](const void* p) noexcept -> std::string_view {
        return static_cast<const C*>(p)->reason();
    },
    .message = nullptr,
    .is_retriable = nullptr,
};

}  // namespace detail

/// Type-erased container for error/cancellation values.
/// Uses small-buffer optimization: values <= sbo_size live inline,
/// larger values are heap-allocated. Allocated on-demand (async_node
/// stores only a pointer to this).
class erased_outcome {
public:
    erased_outcome() noexcept = default;

    ~erased_outcome() noexcept {
        reset();
    }

    erased_outcome(erased_outcome&& other) noexcept : vptr(other.vptr) {
        if(vptr) {
            if(other.is_inline()) {
                vptr->move_construct(other.buffer, buffer);
            } else {
                *reinterpret_cast<void**>(buffer) = *reinterpret_cast<void**>(other.buffer);
            }
            other.vptr = nullptr;
        }
    }

    erased_outcome& operator=(erased_outcome&& other) noexcept {
        if(this != &other) {
            reset();
            vptr = other.vptr;
            if(vptr) {
                if(other.is_inline()) {
                    vptr->move_construct(other.buffer, buffer);
                } else {
                    *reinterpret_cast<void**>(buffer) = *reinterpret_cast<void**>(other.buffer);
                }
                other.vptr = nullptr;
            }
        }
        return *this;
    }

    erased_outcome(const erased_outcome&) = delete;
    erased_outcome& operator=(const erased_outcome&) = delete;

    template <structured_error E>
    static erased_outcome from_error(E&& e) {
        erased_outcome out;
        out.emplace(&detail::error_vtable_for<std::decay_t<E>>, std::forward<E>(e));
        return out;
    }

    template <structured_cancel C>
    static erased_outcome from_cancel(C&& c) {
        erased_outcome out;
        out.emplace(&detail::cancel_vtable_for<std::decay_t<C>>, std::forward<C>(c));
        return out;
    }

    bool empty() const noexcept {
        return vptr == nullptr;
    }

    bool has_error() const noexcept {
        return vptr && vptr->type == outcome_vtable::kind::error;
    }

    bool is_cancelled() const noexcept {
        return vptr && vptr->type == outcome_vtable::kind::cancel;
    }

    bool should_propagate() const noexcept {
        return vptr && vptr->should_propagate(data());
    }

    std::string_view reason() const noexcept {
        return vptr && vptr->reason ? vptr->reason(data()) : "";
    }

    std::string_view message() const noexcept {
        return vptr && vptr->message ? vptr->message(data()) : "";
    }

    bool is_retriable() const noexcept {
        return vptr && vptr->is_retriable && vptr->is_retriable(data());
    }

    template <typename T>
    T* as() noexcept {
        return static_cast<T*>(data());
    }

    template <typename T>
    const T* as() const noexcept {
        return static_cast<const T*>(data());
    }

    void reset() noexcept {
        if(!vptr) {
            return;
        }

        if(is_inline()) {
            vptr->destroy(buffer);
        } else {
            auto* p = *reinterpret_cast<void**>(buffer);
            vptr->destroy(p);
            ::operator delete(p, std::align_val_t{vptr->align});
        }
        vptr = nullptr;
    }

private:
    constexpr static std::size_t sbo_size = sizeof(void*) * 3;

    const outcome_vtable* vptr = nullptr;
    alignas(std::max_align_t) std::byte buffer[sbo_size] = {};

    bool is_inline() const noexcept {
        return vptr && vptr->size <= sbo_size && vptr->align <= alignof(std::max_align_t);
    }

    void* data() noexcept {
        return is_inline() ? static_cast<void*>(buffer) : *reinterpret_cast<void**>(buffer);
    }

    const void* data() const noexcept {
        return is_inline() ? static_cast<const void*>(buffer)
                           : *reinterpret_cast<const void* const*>(buffer);
    }

    template <typename T>
    void emplace(const outcome_vtable* v, T&& value) {
        using D = std::decay_t<T>;
        vptr = v;
        if(v->size <= sbo_size && v->align <= alignof(std::max_align_t)) {
            new (buffer) D(std::forward<T>(value));
        } else {
            auto* p = ::operator new(v->size, std::align_val_t{v->align});
            new (p) D(std::forward<T>(value));
            *reinterpret_cast<void**>(buffer) = p;
        }
    }
};

}  // namespace eventide
