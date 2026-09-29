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
    EXPECT(fn(3, 4) == 7);
}

ZEST_CASE(keeps_a_lambda_without_captures_by_value) {
    function_ref<int(int, int)> fn = +[](int a, int b) {
        return a * b;
    };
    {
        auto multiply = [](int a, int b) {
            return a * b;
        };
        fn = multiply;
    }
    // The lambda converted to a function pointer, which outlives it.
    EXPECT(fn(3, 4) == 12);
}

ZEST_CASE(refers_to_a_callable) {
    int base = 10;
    auto lambda = [&base](int x) {
        return base + x;
    };
    function_ref<int(int)> fn(lambda);
    EXPECT(fn(5) == 15);
    base = 20;
    EXPECT(fn(5) == 25);
}

ZEST_CASE(calls_a_mutable_callable_in_place) {
    int calls = 0;
    auto counter = [calls](int x) mutable {
        return calls += x;
    };
    function_ref<int(int)> fn(counter);
    EXPECT(fn(5) == 5);
    EXPECT(fn(3) == 8);
    EXPECT(counter(0) == 8);
}

ZEST_CASE(calls_a_const_callable) {
    const Offset offset{100};
    function_ref<int(int)> fn(offset);
    EXPECT(fn(23) == 123);
}

ZEST_CASE(returns_nothing) {
    int result = 0;
    auto set = [&result](int value) {
        result = value;
    };
    function_ref<void(int)> fn(set);
    fn(42);
    EXPECT(result == 42);
}

ZEST_CASE(converts_arguments_at_the_call) {
    auto length = [](const std::string& text) {
        return text.size();
    };
    function_ref<std::size_t(std::string)> by_value(length);
    EXPECT(by_value("four") == 4U);
    function_ref<long(long)> widen = +[](long x) {
        return x;
    };
    EXPECT(widen(7) == 7L);
}

ZEST_CASE(forwards_reference_parameters) {
    auto increment = [](int& value) {
        ++value;
    };
    function_ref<void(int&)> fn(increment);
    int value = 1;
    fn(value);
    EXPECT(value == 2);

    auto take = [](std::unique_ptr<int>&& owner) {
        return std::move(owner);
    };
    function_ref<std::unique_ptr<int>(std::unique_ptr<int>&&)> taker(take);
    auto owner = std::make_unique<int>(5);
    auto taken = taker(std::move(owner));
    ASSERT(taken != nullptr);
    EXPECT(*taken == 5);
    EXPECT(owner == nullptr);
}

ZEST_CASE(passes_move_only_values) {
    auto unwrap = [](std::unique_ptr<int> owner) {
        return *owner;
    };
    function_ref<int(std::unique_ptr<int>)> fn(unwrap);
    EXPECT(fn(std::make_unique<int>(9)) == 9);
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
    EXPECT(assigned() == 3);
}

ZEST_CASE(binds_a_member_function) {
    Adder adder{100};
    auto fn = bind_ref<&Adder::add>(adder);
    EXPECT(fn(5) == 105);
    adder.base = 0;
    EXPECT(fn(5) == 5);
}

ZEST_CASE(binds_a_const_member_function) {
    const Adder adder{42};
    auto fn = bind_ref<&Adder::add_const>(adder);
    EXPECT(fn(8) == 50);
    EXPECT(zest::type_eq<decltype(fn), function_ref<int(int)>>());
}

ZEST_CASE(refuses_to_refer_to_a_temporary) {
    auto capture = [x = 1] {
        return x;
    };
    STATIC_EXPECT(!std::is_constructible_v<function_ref<int()>, decltype(capture)>);
    STATIC_EXPECT(std::is_constructible_v<function_ref<int()>, decltype(capture)&>);
}

ZEST_CASE(calls_a_function_in_constant_evaluation) {
    constexpr auto result = [] {
        function_ref<int(int)> fn(twice);
        return fn(21);
    }();
    STATIC_EXPECT(result == 42);
}

ZEST_CASE(mem_fn_names_the_class_and_signature) {
    using non_const = mem_fn<&Adder::add>;
    using constant = mem_fn<&Adder::add_const>;
    EXPECT(zest::type_eq<non_const::ClassType, Adder>());
    EXPECT(zest::type_eq<non_const::FunctionType, int(int)>());
    EXPECT(zest::type_eq<constant::ClassType, Adder>());
    EXPECT(zest::type_eq<constant::FunctionType, int(int)>());
    Adder adder{10};
    EXPECT((adder.*non_const::get())(5) == 15);
    EXPECT((adder.*constant::get())(1) == 11);
    STATIC_EXPECT(is_mem_fn_of<const Adder, constant>);
    STATIC_EXPECT(!is_mem_fn_of<Offset, constant>);
}

};  // ZEST_SUITE(support_functional_function_ref)

}  // namespace

}  // namespace kota
