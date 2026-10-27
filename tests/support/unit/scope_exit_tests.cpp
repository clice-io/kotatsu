#include <stdexcept>
#include <type_traits>

#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/support/scope_exit.h"

namespace kota {

namespace {

/// How many times a guard counting its calls ran once its scope was left,
/// released first when `released`.
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

/// Whether ScopeExit<F> is a type.
template <typename F>
concept guards = requires { typename ScopeExit<F>; };

/// A function whose move may throw.
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

// Its function may throw, but the destructor that calls it does not: a throw
// from it ends the program.
ZEST_CASE(neither_copies_nor_moves_nor_throws) {
    using Guard = ScopeExit<void (*)()>;
    ZSTATIC_EXPECT(std::is_nothrow_constructible_v<Guard, void (*)()>);
    ZSTATIC_EXPECT(std::is_nothrow_destructible_v<Guard>);
    ZSTATIC_EXPECT(noexcept(std::declval<Guard&>().release()));
    ZSTATIC_EXPECT(!std::is_copy_constructible_v<Guard>);
    ZSTATIC_EXPECT(!std::is_move_constructible_v<Guard>);
}

// Its construction cannot throw, so that the function it is given always
// runs.
ZEST_CASE(takes_a_function_moved_without_throwing) {
    ZSTATIC_EXPECT(guards<void (*)()>);
    ZSTATIC_EXPECT(!guards<int>);
    ZSTATIC_EXPECT(!guards<ThrowingMove>);
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
