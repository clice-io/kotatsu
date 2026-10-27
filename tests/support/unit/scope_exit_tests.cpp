#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/support/scope_exit.h"

namespace kota {

namespace {

/// How many times a guard's action has run once the guard's scope is left,
/// the guard released first when `released`.
constexpr int calls_after_scope(bool released) {
    int calls = 0;
    {
        ScopeExit count([&] { calls += 1; });
        if(released) {
            count.release();
        }
    }
    return calls;
}

/// Whether ScopeExit takes an F.
template <typename F>
concept accepts = requires { typename ScopeExit<F>; };

/// An action whose move may throw.
struct ThrowingMove {
    ThrowingMove() = default;

    ThrowingMove(ThrowingMove&&) noexcept(false) {}

    void operator()() const {}
};

ZEST_SUITE(support_scope_exit) {

ZEST_CASE(runs_once_its_scope_is_left) {
    int calls = 0;
    {
        ScopeExit count([&] { calls += 1; });
        ZEXPECT(calls == 0);
    }
    ZEXPECT(calls == 1);
}

// An action that can only be moved, and that changes what it holds.
ZEST_CASE(runs_an_action_it_can_only_move) {
    int seen = 0;
    {
        ScopeExit count([held = std::make_unique<int>(1), &seen]() mutable { seen = ++*held; });
    }
    ZEXPECT(seen == 2);
}

ZEST_CASE(release_keeps_it_from_running) {
    int calls = 0;
    {
        ScopeExit count([&] { calls += 1; });
        count.release();
    }
    ZEXPECT(calls == 0);
}

ZEST_CASE(runs_in_constant_evaluation) {
    ZSTATIC_EXPECT(calls_after_scope(false) == 1);
    ZSTATIC_EXPECT(calls_after_scope(true) == 0);
}

// Its action may throw, but the destructor that calls it does not: a throw
// from it ends the program.
ZEST_CASE(neither_copies_nor_moves_nor_throws) {
    using Guard = ScopeExit<void (*)()>;
    ZSTATIC_EXPECT(std::is_nothrow_constructible_v<Guard, void (*)()>);
    ZSTATIC_EXPECT(std::is_nothrow_destructible_v<Guard>);
    ZSTATIC_EXPECT(noexcept(std::declval<Guard&>().release()));
    ZSTATIC_EXPECT(!std::is_copy_constructible_v<Guard>);
    ZSTATIC_EXPECT(!std::is_move_constructible_v<Guard>);
    ZSTATIC_EXPECT(!std::is_copy_assignable_v<Guard>);
    ZSTATIC_EXPECT(!std::is_move_assignable_v<Guard>);
}

// Moving the action in cannot throw, so that once given it always runs.
ZEST_CASE(takes_an_action_moved_without_throwing) {
    ZSTATIC_EXPECT(accepts<void (*)()>);
    ZSTATIC_EXPECT(!accepts<int>);
    ZSTATIC_EXPECT(!accepts<ThrowingMove>);
}

#if KOTA_ENABLE_EXCEPTIONS

ZEST_CASE(runs_when_a_throw_leaves_its_scope) {
    int calls = 0;
    ZEXPECT(zest::throws<std::runtime_error>([&] {
        ScopeExit count([&] { calls += 1; });
        throw std::runtime_error("leaving");
    }));
    ZEXPECT(calls == 1);
}

#endif  // KOTA_ENABLE_EXCEPTIONS

};  // ZEST_SUITE(support_scope_exit)

}  // namespace

}  // namespace kota
