#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "support/harness/tracked.h"
#include "kota/zest/zest.h"
#include "kota/support/functional.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

int negate(int x) {
    return -x;
}

constexpr int square(int x) {
    return x * x;
}

struct Adder {
    int base;

    int add(int x) {
        return base + x;
    }

    int add_const(int x) const {
        return base + x;
    }
};

struct Counter {
    int count = 0;

    int& bump() {
        return ++count;
    }

    int read() const noexcept {
        return count;
    }
};

struct Padded {
    int value;
    char padding[32] = {};

    int operator()(int x) const {
        return value + x;
    }
};

/// A small callable aligned past what a function's buffer is.
struct alignas(2 * alignof(std::max_align_t)) OverAligned {
    int value;

    int operator()(int x) const {
        return value + x;
    }
};

/// A callable whose move can throw, which a function cannot move in place.
struct ThrowingMove {
    ThrowingMove() = default;

    ThrowingMove(ThrowingMove&&) noexcept(false) {}

    int operator()(int x) const {
        return x;
    }
};

/// A callable that points into itself, which moving must keep true: bytes copied elsewhere
/// point back into the old object.
struct SelfPointing {
    int value;
    const int* self = &value;

    explicit SelfPointing(int value) : value(value) {}

    SelfPointing(SelfPointing&& other) noexcept : value(other.value) {}

    int operator()() const {
        return self == &value ? value : -1;
    }
};

/// A callable holding a Tracked, too large for a function to keep inline.
auto large_tracked(int value) {
    return [tracked = test::Tracked(value), padding = Padded{0}](int x) {
        return tracked.value() + padding(x);
    };
}

using Function = function<int(int)>;

/// A way a function keeps its callable, and one such function.
struct Kind {
    std::string_view name;
    Function (*make)();
    /// What the function returns for 1.
    int result;
    /// How many Tracked the callable holds.
    int tracked;
};

const Kind kinds[] = {
    {"pointer",                   [] { return Function(negate); },                                  -1, 0},
    {"trivially copyable inline",
     [] { return Function([base = 30](int x) { return base + x; }); },
     31,                                                                                                0},
    {"inline",
     [] { return Function([tracked = test::Tracked(10)](int x) { return tracked.value() + x; }); },
     11,                                                                                                1},
    {"heap",                      [] { return Function(large_tracked(20)); },                       21, 1},
};

ZEST_SUITE(support_functional_function) {

ZEST_CASE(small_callables_whose_move_cannot_throw_live_inline) {
    STATIC_EXPECT(Function::sbo_eligible<int (*)(int)>);
    STATIC_EXPECT(Function::sbo_eligible<test::Tracked>);
    STATIC_EXPECT(!Function::sbo_eligible<Padded>);
    STATIC_EXPECT(!Function::sbo_eligible<ThrowingMove>);
    STATIC_EXPECT(!Function::sbo_eligible<OverAligned>);
}

ZEST_CASE(calls_a_function) {
    Function fn(negate);
    EXPECT(fn(5) == -5);
}

ZEST_CASE(calls_a_lambda_without_captures) {
    Function fn([](int x) { return x * x; });
    EXPECT(fn(5) == 25);
}

ZEST_CASE(calls_an_inline_callable) {
    int base = 10;
    Function fn([base](int x) { return base + x; });
    EXPECT(fn(5) == 15);
}

ZEST_CASE(calls_a_heap_callable) {
    Function fn(Padded{42});
    EXPECT(fn(8) == 50);
}

ZEST_CASE(calls_an_over_aligned_callable) {
    Function fn(OverAligned{4});
    Function moved(std::move(fn));
    EXPECT(moved(1) == 5);
}

ZEST_CASE(passes_arguments_in_order) {
    function<int(int, int, int)> fn([](int a, int b, int c) { return a * 100 + b * 10 + c; });
    EXPECT(fn(1, 2, 3) == 123);
}

ZEST_CASE(calls_a_callable_whose_move_can_throw) {
    Function fn(ThrowingMove{});
    Function moved(std::move(fn));
    EXPECT(moved(3) == 3);
}

ZEST_CASE(calls_a_generic_lambda) {
    Function fn([offset = 7](auto x) { return offset + x; });
    EXPECT(fn(5) == 12);
}

ZEST_CASE(keeps_the_state_of_a_mutable_callable) {
    Function fn([sum = 0](int x) mutable { return sum += x; });
    EXPECT(fn(2) == 2);
    EXPECT(fn(3) == 5);
}

ZEST_CASE(returns_nothing) {
    int result = 0;
    function<void(int)> fn([&result](int value) { result = value; });
    fn(99);
    EXPECT(result == 99);
}

ZEST_CASE(converts_arguments_at_the_call) {
    function<std::string(std::string)> echo([](std::string text) { return text; });
    EXPECT(echo("text") == "text");
    function<long(long)> widen([](long x) { return x; });
    EXPECT(widen(7) == 7L);
}

ZEST_CASE(keeps_a_function_of_a_convertible_signature) {
    function<long(int)> widened(negate);
    EXPECT(widened(4) == -4L);
    STATIC_EXPECT(Function::sbo_eligible<long (*)(int)>);
}

ZEST_CASE(converts_the_result) {
    function<long(int)> widen([](int x) { return x; });
    EXPECT(widen(7) == 7L);
    function<std::string()> text([] { return "text"; });
    EXPECT(text() == "text");
    int calls = 0;
    function<void(int)> discard([&calls](int x) {
        calls += 1;
        return x;
    });
    discard(1);
    EXPECT(calls == 1);
}

ZEST_CASE(passes_move_only_values) {
    function<std::unique_ptr<int>(std::unique_ptr<int>)> fn(
        [](std::unique_ptr<int> value) { return std::make_unique<int>(*value + 1); });
    auto result = fn(std::make_unique<int>(41));
    ASSERT(result != nullptr);
    EXPECT(*result == 42);
}

ZEST_CASE(const_form_calls_a_const_callable) {
    const function<int(int) const> fn([base = 100](int x) { return base + x; });
    EXPECT(fn(23) == 123);
    const function<int(int) const> pointer(negate);
    EXPECT(pointer(5) == -5);
    const function<int(int) const> heap(Padded{1});
    EXPECT(heap(1) == 2);
}

ZEST_CASE(only_the_const_form_is_called_as_const) {
    STATIC_EXPECT(!std::is_invocable_v<const Function&, int>);
    STATIC_EXPECT(std::is_invocable_v<const function<int(int) const>&, int>);
    auto mutable_lambda = [sum = 0](int x) mutable {
        return sum += x;
    };
    STATIC_EXPECT(!std::is_constructible_v<function<int(int) const>, decltype(mutable_lambda)>);
    STATIC_EXPECT(std::is_constructible_v<Function, decltype(mutable_lambda)>);
}

ZEST_CASE(is_move_only) {
    STATIC_EXPECT(!std::is_copy_constructible_v<Function>);
    STATIC_EXPECT(std::is_nothrow_move_constructible_v<Function>);
    STATIC_EXPECT(std::is_nothrow_move_assignable_v<Function>);
}

ZEST_CASE(move_keeps_an_inline_callable_that_points_into_itself) {
    function<int()> fn(SelfPointing(7));
    function<int()> moved(std::move(fn));
    EXPECT(moved() == 7);
    function<int()> assigned(SelfPointing(1));
    assigned = std::move(moved);
    EXPECT(assigned() == 7);
}

ZEST_CASE(move_keeps_an_inline_small_vector) {
    // The small_vector's elements are in its inline buffer, which it points to.
    function<int()> kept([] { return 0; });
    {
        small_vector<int, 2> values = {3, 4};
        function<int()> fn([values] { return values[0] + values[1]; });
        kept = std::move(fn);
    }
    EXPECT(kept() == 7);
}

ZEST_CASE(move_construction_keeps_each_kind) {
    for(const auto& kind: kinds) {
        ZEST_CONTEXT("{} function", kind.name);
        test::Census census;
        {
            Function fn = kind.make();
            Function moved(std::move(fn));
            EXPECT(moved(1) == kind.result);
            EXPECT(census.live == kind.tracked);
        }
        EXPECT(census.live == 0);
    }
}

ZEST_CASE(move_assignment_between_kinds) {
    // The callable assigned over is destroyed once, and the one moved in is not copied.
    for(const auto& from: kinds) {
        for(const auto& to: kinds) {
            ZEST_CONTEXT("{} function assigned to a {} one", from.name, to.name);
            test::Census census;
            {
                Function source = from.make();
                Function target = to.make();
                target = std::move(source);
                EXPECT(target(1) == from.result);
                EXPECT(census.live == from.tracked);
                EXPECT(census.copies == 0);
            }
            EXPECT(census.live == 0);
        }
    }
}

ZEST_CASE(const_form_moves_its_callable_once) {
    using ConstFunction = function<int(int) const>;
    test::Census census;
    {
        ConstFunction inline_fn(
            [tracked = test::Tracked(10)](int x) { return tracked.value() + x; });
        ConstFunction moved(std::move(inline_fn));
        ConstFunction heap(large_tracked(20));
        heap = std::move(moved);
        EXPECT(heap(1) == 11);
        EXPECT(census.live == 1);
    }
    EXPECT(census.live == 0);
}

ZEST_CASE(move_assignment_to_itself_keeps_the_callable) {
    Function fn([](int x) { return x + 42; });
    auto& same = fn;
    fn = std::move(same);
    EXPECT(fn(0) == 42);

    Function heap(Padded{10});
    auto& heap_same = heap;
    heap = std::move(heap_same);
    EXPECT(heap(5) == 15);
}

ZEST_CASE(calls_a_function_in_constant_evaluation) {
    constexpr auto result = [] {
        Function fn(square);
        Function moved(std::move(fn));
        return moved(9);
    }();
    STATIC_EXPECT(result == 81);
}

ZEST_CASE(bind_owns_a_copy_of_the_object) {
    Adder adder{50};
    auto fn = bind<&Adder::add>(adder);
    adder.base = 0;
    EXPECT(fn(7) == 57);
    EXPECT(zest::type_eq<decltype(fn), Function>());
}

ZEST_CASE(bind_calls_a_const_member_function) {
    auto fn = bind<&Adder::add_const>(Adder{10});
    EXPECT(fn(5) == 15);
}

ZEST_CASE(bind_keeps_a_large_object) {
    auto fn = bind<&Padded::operator()>(Padded{50});
    EXPECT(fn(7) == 57);
}

ZEST_CASE(bind_calls_a_noexcept_member_function) {
    auto fn = bind<&Counter::read>(Counter{5});
    EXPECT(fn() == 5);
}

ZEST_CASE(bind_returns_what_the_member_function_returns) {
    auto fn = bind<&Counter::bump>(Counter{});
    EXPECT(zest::type_eq<decltype(fn), function<int&()>>());
    int& count = fn();
    count += 10;
    EXPECT(fn() == 12);
}

};  // ZEST_SUITE(support_functional_function)

}  // namespace

}  // namespace kota
