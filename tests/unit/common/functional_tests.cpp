#include <string>
#include <utility>

#include <eventide/zest/macro.h>
#include <eventide/zest/zest.h>
#include <eventide/common/functional.h>

using namespace eventide;

// --- Helpers ---

static int free_add(int a, int b) {
    return a + b;
}

static int free_negate(int x) {
    return -x;
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

struct SmallCallable {
    int val;

    int operator()(int x) const {
        return val + x;
    }
};

static_assert(function<int(int)>::sbo_eligible<SmallCallable>);

struct LargeCallable {
    int val;
    char padding[32]{};

    int operator()(int x) const {
        return val + x;
    }
};

static_assert(!function<int(int)>::sbo_eligible<LargeCallable>);

struct NonTrivialCallable {
    std::unique_ptr<int> val;

    NonTrivialCallable(int val) : val(std::make_unique<int>(val)) {}

    int operator()(int x) const {
        return *val + x;
    }
};

static_assert(!std::is_trivially_copyable_v<NonTrivialCallable>);
static_assert(function<int(int)>::sbo_eligible<NonTrivialCallable>);

// --- Tests ---

TEST_SUITE(functional) {

// ===== function_ref tests =====

TEST_CASE(function_ref_from_function_pointer) {
    function_ref<int(int, int)> fn(free_add);
    EXPECT_EQ(fn(3, 4), 7);
    EXPECT_EQ(fn(0, 0), 0);
    EXPECT_EQ(fn(-1, 1), 0);
};

TEST_CASE(function_ref_from_stateless_lambda) {
    // Stateless lambdas are convertible to function pointers,
    // so this should go through the Sign* path in make().
    auto lambda = +[](int a, int b) -> int {
        return a * b;
    };
    function_ref<int(int, int)> fn(lambda);
    EXPECT_EQ(fn(3, 4), 12);
    EXPECT_EQ(fn(0, 5), 0);
};

TEST_CASE(function_ref_from_stateful_lambda) {
    // Stateful lambda goes through the mem_fn<operator()> path in make().
    int capture = 10;
    auto lambda = [&capture](int x) -> int {
        return capture + x;
    };
    function_ref<int(int)> fn(lambda);
    EXPECT_EQ(fn(5), 15);
    capture = 20;
    EXPECT_EQ(fn(5), 25);  // reflects changed capture
};

TEST_CASE(function_ref_from_pointer_and_mem_fn) {
    Adder adder{100};
    auto fn = bind_ref<&Adder::add>(adder);
    EXPECT_EQ(fn(5), 105);
    EXPECT_EQ(fn(-100), 0);
};

TEST_CASE(function_ref_from_const_mem_fn) {
    Adder adder{42};
    auto fn = bind_ref<&Adder::add_const>(adder);
    EXPECT_EQ(fn(8), 50);
};

TEST_CASE(function_ref_bind_ref_non_const) {
    Adder adder{7};
    auto fn = bind_ref<&Adder::add>(adder);
    EXPECT_EQ(fn(3), 10);
};

TEST_CASE(function_ref_copy) {
    function_ref<int(int, int)> fn(free_add);
    function_ref<int(int, int)> fn2(fn);  // copy construct
    EXPECT_EQ(fn2(10, 20), 30);

    function_ref<int(int, int)> fn3(free_add);
    fn3 = fn;  // copy assign
    EXPECT_EQ(fn3(1, 2), 3);
};

TEST_CASE(function_ref_move) {
    function_ref<int(int, int)> fn(free_add);
    function_ref<int(int, int)> fn2(std::move(fn));  // move construct
    EXPECT_EQ(fn2(5, 6), 11);

    function_ref<int(int, int)> fn3(free_add);
    fn3 = std::move(fn2);  // move assign
    EXPECT_EQ(fn3(7, 8), 15);
};

TEST_CASE(function_ref_void_return) {
    int result = 0;
    auto setter = [&result](int v) {
        result = v;
    };
    function_ref<void(int)> fn(setter);
    fn(42);
    EXPECT_EQ(result, 42);
};

TEST_CASE(function_ref_callable_object) {
    SmallCallable sc{100};
    function_ref<int(int)> fn(sc);
    EXPECT_EQ(fn(23), 123);
};

// ===== function tests =====

TEST_CASE(function_from_function_pointer) {
    function<int(int, int)> fn(free_add);
    EXPECT_EQ(fn(3, 4), 7);
    EXPECT_EQ(fn(-5, 5), 0);
};

TEST_CASE(function_from_stateless_lambda) {
    function<int(int)> fn(+[](int x) -> int { return x * x; });
    EXPECT_EQ(fn(5), 25);
    EXPECT_EQ(fn(0), 0);
    EXPECT_EQ(fn(-3), 9);
};

TEST_CASE(function_from_small_lambda_sbo) {
    // Small lambda uses SBO storage
    int capture = 10;
    auto lambda = [capture](int x) -> int {
        return capture + x;
    };
    static_assert(sizeof(lambda) <= function<int(int)>::sbo_size);
    function<int(int)> fn(std::move(lambda));
    EXPECT_EQ(fn(5), 15);
    EXPECT_EQ(fn(-10), 0);
};

TEST_CASE(function_from_large_lambda_heap) {
    // Large lambda uses heap allocation
    char padding[32] = {};
    padding[0] = 'A';
    int capture = 42;
    auto lambda = [capture, padding](int x) -> int {
        (void)padding;
        return capture + x;
    };
    static_assert(sizeof(lambda) > function<int(int)>::sbo_size);
    function<int(int)> fn(std::move(lambda));
    EXPECT_EQ(fn(8), 50);
    EXPECT_EQ(fn(-42), 0);
};

TEST_CASE(function_move_construct) {
    int capture = 5;
    function<int(int)> fn([capture](int x) -> int { return capture + x; });
    function<int(int)> fn2(std::move(fn));
    EXPECT_EQ(fn2(10), 15);
};

TEST_CASE(function_move_assign) {
    function<int(int)> fn([](int x) -> int { return x + 1; });
    function<int(int)> fn2([](int x) -> int { return x + 2; });
    EXPECT_EQ(fn(10), 11);
    EXPECT_EQ(fn2(10), 12);
    fn2 = std::move(fn);
    EXPECT_EQ(fn2(10), 11);
};

TEST_CASE(function_move_assign_large_to_large) {
    // Move assign heap-allocated function to another heap-allocated function
    // Exercises the deleter in operator=
    char p1[32] = {};
    char p2[32] = {};
    int c1 = 1, c2 = 2;
    auto lambda1 = [c1, p1](int x) -> int {
        (void)p1;
        return c1 + x;
    };
    auto lambda2 = [c2, p2](int x) -> int {
        (void)p2;
        return c2 + x;
    };
    static_assert(sizeof(lambda1) > function<int(int)>::sbo_size);
    static_assert(sizeof(lambda2) > function<int(int)>::sbo_size);
    function<int(int)> fn1(std::move(lambda1));
    function<int(int)> fn2(std::move(lambda2));
    EXPECT_EQ(fn1(10), 11);
    EXPECT_EQ(fn2(10), 12);
    fn2 = std::move(fn1);  // old fn2's heap memory should be freed
    EXPECT_EQ(fn2(10), 11);
};

TEST_CASE(function_void_return) {
    int result = 0;
    function<void(int)> fn([&result](int v) { result = v; });
    fn(99);
    EXPECT_EQ(result, 99);
};

TEST_CASE(function_with_mem_fn_small) {
    SmallCallable sc{50};
    auto fn = bind<&SmallCallable::operator()>(sc);
    EXPECT_EQ(fn(7), 57);
};

TEST_CASE(function_with_mem_fn_large) {
    LargeCallable lc{50};
    auto fn = bind<&LargeCallable::operator()>(lc);
    EXPECT_EQ(fn(7), 57);
};

TEST_CASE(function_from_free_function_no_deleter) {
    // function from raw function pointer should have no deleter
    function<int(int)> fn(free_negate);
    EXPECT_EQ(fn(5), -5);
    // Move it - exercises move with null deleter
    function<int(int)> fn2(std::move(fn));
    EXPECT_EQ(fn2(5), -5);
};

TEST_CASE(function_move_chain) {
    // Chain of moves to verify no double-free or corruption
    int val = 7;
    function<int(int)> fn1([val](int x) -> int { return val * x; });
    function<int(int)> fn2(std::move(fn1));
    function<int(int)> fn3(std::move(fn2));
    EXPECT_EQ(fn3(6), 42);
};

TEST_CASE(function_from_non_trivial_sbo) {
    // Non-trivially-copyable object that fits in SBO
    NonTrivialCallable nc{10};
    function<int(int)> fn(std::move(nc));
    EXPECT_EQ(fn(5), 15);
    EXPECT_EQ(fn(-10), 0);
};

TEST_CASE(function_move_non_trivial_sbo) {
    NonTrivialCallable nc{20};
    function<int(int)> fn1(std::move(nc));
    EXPECT_EQ(fn1(5), 25);
    function<int(int)> fn2(std::move(fn1));
    EXPECT_EQ(fn2(5), 25);
};

TEST_CASE(function_move_assign_non_trivial_sbo) {
    NonTrivialCallable nc1{1};
    NonTrivialCallable nc2{2};
    function<int(int)> fn1(std::move(nc1));
    function<int(int)> fn2(std::move(nc2));
    EXPECT_EQ(fn1(10), 11);
    EXPECT_EQ(fn2(10), 12);
    fn2 = std::move(fn1);
    EXPECT_EQ(fn2(10), 11);
};

// ===== mem_fn tests =====

TEST_CASE(mem_fn_non_const) {
    using MF = mem_fn<&Adder::add>;
    static_assert(std::is_same_v<MF::ClassType, Adder>);
    auto ptr = MF::get();
    Adder a{10};
    EXPECT_EQ((a.*ptr)(5), 15);
};

TEST_CASE(mem_fn_const) {
    using MF = mem_fn<&Adder::add_const>;
    static_assert(std::is_same_v<MF::ClassType, Adder>);
    auto ptr = MF::get();
    const Adder a{10};
    EXPECT_EQ((a.*ptr)(5), 15);
};

};  // TEST_SUITE(functional)
