#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

namespace kota {

/// Calls `function` when it goes out of scope, unless release() was called
/// first: for what a function undoes on every way out but the one where it
/// succeeds, a throw and the destruction of a suspended coroutine among
/// them. As std::experimental::scope_exit. `function` is called from the
/// destructor, so a throw from it ends the program.
template <typename F>
    requires std::invocable<F&> && std::is_nothrow_move_constructible_v<F>
class [[nodiscard]] ScopeExit {
public:
    constexpr explicit ScopeExit(F function) noexcept : function(std::move(function)) {}

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    constexpr ~ScopeExit() {
        if(active) {
            function();
        }
    }

    /// Keeps the function from being called.
    constexpr void release() noexcept {
        active = false;
    }

private:
    F function;

    bool active = true;
};

}  // namespace kota
