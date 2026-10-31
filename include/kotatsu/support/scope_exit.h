#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

namespace kotatsu {

/// Calls `action` when this goes, unless release() was called first: a
/// cleanup owed on every way out of a scope, a throw and the destruction of a
/// suspended coroutine among them, or, released once the scope has done its
/// work, one owed only when it fails. As std::experimental::scope_exit, but
/// neither moved nor made by a factory, and `action` must move without
/// throwing. `action` is called from the destructor, so a throw from it ends
/// the program.
template <typename F>
    requires std::invocable<F&> && std::is_nothrow_move_constructible_v<F>
class [[nodiscard]] ScopeExit {
public:
    constexpr explicit ScopeExit(F action) noexcept : action(std::move(action)) {}

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    constexpr ~ScopeExit() {
        if(active) {
            action();
        }
    }

    /// Keeps `action` from being called.
    constexpr void release() noexcept {
        active = false;
    }

private:
    F action;

    bool active = true;
};

}  // namespace kotatsu
