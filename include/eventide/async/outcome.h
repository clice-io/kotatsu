#pragma once

#include <cassert>
#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <variant>

namespace eventide {

template <typename T, typename E = void, typename C = void>
class outcome;

template <typename T>
constexpr bool is_outcome_v = false;

template <typename T, typename E, typename C>
constexpr bool is_outcome_v<outcome<T, E, C>> = true;

/// Concept: error types that participate in structured concurrency.
/// The framework calls should_propagate() to decide whether an error
/// cancels sibling tasks and propagates upward through aggregates.
template <typename E>
concept structured_error = std::movable<E> && requires(const E& e) {
    { e.should_propagate() } -> std::convertible_to<bool>;
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

struct outcome_ok_tag {};

inline outcome_ok_tag outcome_value() {
    return {};
}

template <typename E>
struct outcome_error_t {
    E value;
};

template <typename E>
outcome_error_t<std::decay_t<E>> outcome_error(E&& e) {
    return {std::forward<E>(e)};
}

template <typename C>
struct outcome_cancel_t {
    C value;
};

template <typename C>
outcome_cancel_t<std::decay_t<C>> outcome_cancel(C&& c) {
    return {std::forward<C>(c)};
}

template <typename T, typename E, typename C>
class outcome {
public:
    using value_type = T;
    using error_type = E;
    using cancel_type = C;

    enum class State : std::uint8_t { ok, err, cancelled };

private:
    template <typename X>
    using member_t = std::conditional_t<std::is_void_v<X>, std::type_identity<void>, X>;

public:
    template <typename U = T>
        requires (!std::is_void_v<T>) && std::constructible_from<T, U&&> &&
                 (!is_outcome_v<std::decay_t<U>> || std::same_as<std::decay_t<U>, T>)
    outcome(U&& value) : variant(std::in_place_index<0>, T(std::forward<U>(value))) {}

    outcome()
        requires std::is_void_v<T>
        : variant(std::in_place_index<0>) {}

    template <typename U>
        requires (!std::is_void_v<E>) && std::constructible_from<E, U>
    outcome(outcome_error_t<U> e) : variant(std::in_place_index<1>, E(std::move(e.value))) {}

    template <typename U>
        requires (!std::is_void_v<C>) && std::constructible_from<C, U>
    outcome(outcome_cancel_t<U> c) : variant(std::in_place_index<2>, C(std::move(c.value))) {}

    outcome(outcome_ok_tag)
        requires std::is_void_v<T>
        : variant(std::in_place_index<0>) {}

    State state() const noexcept {
        return State(variant.index());
    }

    bool has_value() const noexcept {
        return variant.index() == 0;
    }

    bool has_error() const noexcept
        requires (!std::is_void_v<E>)
    {
        return variant.index() == 1;
    }

    bool is_cancelled() const noexcept
        requires (!std::is_void_v<C>)
    {
        return variant.index() == 2;
    }

    explicit operator bool() const noexcept {
        return has_value();
    }

    auto& value() &
        requires (!std::is_void_v<T>)
    {
        assert(has_value());
        return std::get<0>(variant);
    }

    const auto& value() const&
        requires (!std::is_void_v<T>)
    {
        assert(has_value());
        return std::get<0>(variant);
    }

    auto&& value() &&
        requires(!std::is_void_v<T>) {
            assert(has_value());
            return std::move(std::get<0>(variant));
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

    auto& error() &
        requires (!std::is_void_v<E>)
    {
        assert(has_error());
        return std::get<1>(variant);
    }

    const auto& error() const&
        requires (!std::is_void_v<E>)
    {
        assert(has_error());
        return std::get<1>(variant);
    }

    auto&& error() &&
        requires(!std::is_void_v<E>) {
            assert(has_error());
            return std::move(std::get<1>(variant));
        }

        auto& cancellation() &
            requires (!std::is_void_v<C>)
    {
        assert(is_cancelled());
        return std::get<2>(variant);
    }

    const auto& cancellation() const&
        requires (!std::is_void_v<C>)
    {
        assert(is_cancelled());
        return std::get<2>(variant);
    }

auto&& cancellation() &&
    requires(!std::is_void_v<C>) {
        assert(is_cancelled());
        return std::move(std::get<2>(variant));
    }

    private : std::variant<member_t<T>, member_t<E>, member_t<C>> variant;
};

template <typename T>
class outcome<T, void, void> {
    using stored_type = std::conditional_t<std::is_void_v<T>, std::type_identity<void>, T>;

public:
    using value_type = T;
    using error_type = void;
    using cancel_type = void;

    template <typename U = T>
        requires (!std::is_void_v<T>) && std::constructible_from<T, U&&> &&
                 (!is_outcome_v<std::decay_t<U>> || std::same_as<std::decay_t<U>, T>)
    outcome(U&& value) : data(T(std::forward<U>(value))) {}

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
        return data;
    }

    const auto& value() const&
        requires (!std::is_void_v<T>)
    {
        return data;
    }

    auto&& value() && requires(!std::is_void_v<T>) { return std::move(data); }

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

private:
    [[no_unique_address]]
    std::conditional_t<std::is_void_v<T>, std::type_identity<void>, T> data;
};

}  // namespace eventide
