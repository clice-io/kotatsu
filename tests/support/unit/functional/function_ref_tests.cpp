#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/support/functional.h"

namespace kota {

namespace {

int add(int a, int b) {
    return a + b;
}

int one() {
    return 1;
}

constexpr int twice(int x) {
    return 2 * x;
}

struct Adder {
    int base;

    int add(int x) {
        return base + x;
    }

    int add_const(int x) const {
        return base + x;
    }

    int get() const noexcept {
        return base;
    }
};

struct Offset {
    int value;

    int operator()(int x) const {
        return value + x;
    }
};

ZEST_SUITE(support_functional_function_ref) {

ZEST_CASE(calls_a_function) {
    function_ref<int(int, int)> fn(add);
    ZEXPECT(fn(3, 4) == 7);
}

ZEST_CASE(keeps_a_lambda_without_captures_by_value) {
    // Constant evaluation can call through the function pointer the lambda converts to, which
    // outlives it, but not through a reference to the lambda, a cast from `const void*` it
    // does not allow before C++26: this compiles only on the pointer path.
    constexpr auto result = [] {
        function_ref<int(int, int)> fn(add);
        {
            auto multiply = [](int a, int b) {
                return a * b;
            };
            fn = multiply;
        }
        return fn(3, 4);
    }();
    ZSTATIC_EXPECT(result == 12);
}

ZEST_CASE(refers_to_a_callable) {
    int base = 10;
    auto lambda = [&base](int x) {
        return base + x;
    };
    function_ref<int(int)> fn(lambda);
    ZEXPECT(fn(5) == 15);
    base = 20;
    ZEXPECT(fn(5) == 25);
}

ZEST_CASE(calls_a_mutable_callable_in_place) {
    int calls = 0;
    auto counter = [calls](int x) mutable {
        return calls += x;
    };
    function_ref<int(int)> fn(counter);
    ZEXPECT(fn(5) == 5);
    ZEXPECT(fn(3) == 8);
    ZEXPECT(counter(0) == 8);
}

ZEST_CASE(calls_a_const_callable) {
    const Offset offset{100};
    function_ref<int(int)> fn(offset);
    ZEXPECT(fn(23) == 123);
}

ZEST_CASE(returns_nothing) {
    int result = 0;
    auto set = [&result](int value) {
        result = value;
    };
    function_ref<void(int)> fn(set);
    fn(42);
    ZEXPECT(result == 42);
}

ZEST_CASE(converts_arguments_at_the_call) {
    auto length = [](const std::string& text) {
        return text.size();
    };
    function_ref<std::size_t(std::string)> by_value(length);
    ZEXPECT(by_value("four") == 4U);
    function_ref<long(long)> widen = +[](long x) {
        return x;
    };
    ZEXPECT(widen(7) == 7L);
}

ZEST_CASE(refers_to_a_function_of_a_convertible_signature) {
    function_ref<long(int)> widened(twice);
    ZEXPECT(widened(4) == 8L);
    function_ref<void(int, int)> discarded(add);
    discarded(1, 2);
}

ZEST_CASE(converts_the_result) {
    auto identity = [](int x) {
        return x;
    };
    function_ref<long(int)> widen(identity);
    ZEXPECT(widen(7) == 7L);
    int calls = 0;
    auto count = [&calls](int x) {
        calls += 1;
        return x;
    };
    function_ref<void(int)> discard(count);
    discard(1);
    ZEXPECT(calls == 1);
}

ZEST_CASE(forwards_reference_parameters) {
    auto increment = [](int& value) {
        ++value;
    };
    function_ref<void(int&)> fn(increment);
    int value = 1;
    fn(value);
    ZEXPECT(value == 2);

    auto take = [](std::unique_ptr<int>&& owner) {
        return std::move(owner);
    };
    function_ref<std::unique_ptr<int>(std::unique_ptr<int>&&)> taker(take);
    auto owner = std::make_unique<int>(5);
    auto taken = taker(std::move(owner));
    ZASSERT(taken != nullptr);
    ZEXPECT(*taken == 5);
    ZEXPECT(owner == nullptr);
}

ZEST_CASE(passes_move_only_values) {
    auto unwrap = [](std::unique_ptr<int> owner) {
        return *owner;
    };
    function_ref<int(std::unique_ptr<int>)> fn(unwrap);
    ZEXPECT(fn(std::make_unique<int>(9)) == 9);
}

ZEST_CASE(copies_refer_to_the_same_callable) {
    int calls = 0;
    auto count = [&calls] {
        return ++calls;
    };
    function_ref<int()> first(count);
    function_ref<int()> second(first);
    function_ref<int()> assigned(one);
    assigned = second;
    first();
    second();
    ZEXPECT(assigned() == 3);
}

ZEST_CASE(binds_a_member_function) {
    Adder adder{100};
    auto fn = bind_ref<&Adder::add>(adder);
    ZEXPECT(fn(5) == 105);
    adder.base = 0;
    ZEXPECT(fn(5) == 5);
}

ZEST_CASE(binds_a_const_member_function) {
    const Adder adder{42};
    auto fn = bind_ref<&Adder::add_const>(adder);
    ZEXPECT(fn(8) == 50);
    ZEXPECT(zest::type_eq<decltype(fn), function_ref<int(int)>>());
}

ZEST_CASE(binds_a_member_function_of_any_qualifiers) {
    Adder adder{7};
    auto fn = bind_ref<&Adder::get>(adder);
    ZEXPECT(fn() == 7);
    ZEXPECT(zest::type_eq<mem_fn<&Adder::get>::function_type, int()>());
}

ZEST_CASE(refuses_to_refer_to_a_temporary) {
    auto capture = [x = 1] {
        return x;
    };
    ZSTATIC_EXPECT(!std::is_constructible_v<function_ref<int()>, decltype(capture)>);
    ZSTATIC_EXPECT(std::is_constructible_v<function_ref<int()>, decltype(capture)&>);
}

ZEST_CASE(calls_a_function_in_constant_evaluation) {
    constexpr auto result = [] {
        function_ref<int(int)> fn(twice);
        return fn(21);
    }();
    ZSTATIC_EXPECT(result == 42);
}

ZEST_CASE(mem_fn_names_the_class_and_signature) {
    using non_const = mem_fn<&Adder::add>;
    using constant = mem_fn<&Adder::add_const>;
    ZEXPECT(zest::type_eq<non_const::class_type, Adder>());
    ZEXPECT(zest::type_eq<non_const::function_type, int(int)>());
    ZEXPECT(zest::type_eq<constant::class_type, Adder>());
    ZEXPECT(zest::type_eq<constant::function_type, int(int)>());
    ZSTATIC_EXPECT(is_mem_fn_of<Adder, non_const>);
    ZSTATIC_EXPECT(is_mem_fn_of<const Adder, constant>);
    ZSTATIC_EXPECT(!is_mem_fn_of<Offset, constant>);
}

};  // ZEST_SUITE(support_functional_function_ref)

}  // namespace

}  // namespace kota
