#pragma once

#include <cassert>
#include <concepts>
#include <exception>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/config.h"

namespace kota {

template <typename T, typename E = void, typename C = void>
class outcome;

template <typename T>
constexpr bool is_outcome_v = false;

template <typename T, typename E, typename C>
constexpr bool is_outcome_v<outcome<T, E, C>> = true;

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

/// Thrown by outcome::unwrap() when the outcome holds no value. what() is the
/// error's message() when the error type has one, or says that the outcome
/// holds an error or was cancelled.
class bad_outcome_access : public std::exception {
public:
    explicit bad_outcome_access(std::string message) : message(std::move(message)) {}

    const char* what() const noexcept override {
        return message.c_str();
    }

private:
    std::string message;
};

template <typename T, typename E, typename C>
class outcome {
public:
    using value_type = T;
    using error_type = E;
    using cancel_type = C;

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

    bool has_value() const noexcept {
        return variant.index() == 0;
    }

    bool has_error() const noexcept
        requires (!std::is_void_v<E>) {
        return variant.index() == 1;
    }

    bool is_cancelled() const noexcept
        requires (!std::is_void_v<C>) {
        return variant.index() == 2;
    }

    explicit operator bool() const noexcept {
        return has_value();
    }

    template <typename Self>
    auto&& value(this Self&& self)
        requires (!std::is_void_v<T>) {
        assert(self.has_value());
        return std::get<0>(std::forward<Self>(self).variant);
    }

    template <typename Self>
    auto&& operator*(this Self&& self)
        requires (!std::is_void_v<T>) {
        return std::forward<Self>(self).value();
    }

    template <typename Self>
    auto* operator->(this Self&& self)
        requires (!std::is_void_v<T>) {
        return &self.value();
    }

    template <typename Self>
    auto&& error(this Self&& self)
        requires (!std::is_void_v<E>) {
        assert(self.has_error());
        return std::get<1>(std::forward<Self>(self).variant);
    }

    template <typename Self>
    auto&& cancellation(this Self&& self)
        requires (!std::is_void_v<C>) {
        assert(self.is_cancelled());
        return std::get<2>(std::forward<Self>(self).variant);
    }

    /// The value, or a thrown bad_outcome_access when there is none. Aborts
    /// instead in builds without exceptions.
    template <typename Self>
    decltype(auto) unwrap(this Self&& self) {
        if(!self.has_value()) {
            KOTA_THROW(bad_outcome_access(self.failure()));
        }
        if constexpr(!std::is_void_v<T>) {
            return std::forward<Self>(self).value();
        }
    }

private:
    /// What unwrap() reports for an outcome without a value.
    std::string failure() const {
        if constexpr(!std::is_void_v<E>) {
            if(has_error()) {
                if constexpr(requires(const E& e) { std::string_view(e.message()); }) {
                    return std::string(std::string_view(error().message()));
                } else {
                    return "outcome holds an error";
                }
            }
        }
        return "outcome was cancelled";
    }

    std::variant<member_t<T>, member_t<E>, member_t<C>> variant;
};

template <typename T>
class outcome<T, void, void> {
public:
    using value_type = T;
    using error_type = void;
    using cancel_type = void;

    template <typename U = T>
        requires (!std::is_void_v<T>) && std::constructible_from<T, U&&> &&
                 (!is_outcome_v<std::decay_t<U>> || std::same_as<std::decay_t<U>, T>)
    outcome(U&& value) : data(T(std::forward<U>(value))) {}

    outcome()
        requires std::is_void_v<T> {}

    outcome(outcome_ok_tag)
        requires std::is_void_v<T> {}

    constexpr bool has_value() const noexcept {
        return true;
    }

    constexpr explicit operator bool() const noexcept {
        return true;
    }

    template <typename Self>
    auto&& value(this Self&& self)
        requires (!std::is_void_v<T>) {
        return std::forward<Self>(self).data;
    }

    template <typename Self>
    auto&& operator*(this Self&& self)
        requires (!std::is_void_v<T>) {
        return std::forward<Self>(self).data;
    }

    template <typename Self>
    auto* operator->(this Self&& self)
        requires (!std::is_void_v<T>) {
        return &self.data;
    }

    /// The value: an outcome without channels always holds one.
    template <typename Self>
    decltype(auto) unwrap(this Self&& self) {
        if constexpr(!std::is_void_v<T>) {
            return std::forward<Self>(self).value();
        }
    }

private:
    KOTA_NO_UNIQUE_ADDRESS
    std::conditional_t<std::is_void_v<T>, std::type_identity<void>, T> data;
};

}  // namespace kota
