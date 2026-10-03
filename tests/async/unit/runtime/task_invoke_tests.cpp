#include <memory>
#include <string>
#include <utility>

#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

ZEST_SUITE(async_runtime_task_invoke, zest::LoopFixture) {

ZEST_CASE(invoke_gives_the_task_type_the_callable_returns) {
    auto plain = []() -> task<int> {
        co_return 1;
    };
    auto failing = [](int) -> task<void, error> {
        co_return;
    };
    EXPECT(zest::type_eq<decltype(co_invoke(plain)), task<int>>());
    EXPECT(zest::type_eq<decltype(co_invoke(failing, 1)), task<void, error>>());
}

// The closure and `owned` are gone before the task starts; the frame keeps
// the closure it moved in, whose capture is then the value's one owner.
ZEST_CASE(captures_outlive_the_closure_that_made_the_task) {
    auto make = [] {
        auto owned = std::make_shared<std::string>("kotatsu");
        return co_invoke([owned]() -> task<std::pair<std::string, long>> {
            co_await yield();
            co_return std::pair{*owned, owned.use_count()};
        });
    };
    auto made = make();

    auto [result] = run(std::move(made));
    ASSERT(result.has_value());
    EXPECT(result->first == "kotatsu");
    EXPECT(result->second == 1);
}

// A callable that takes its argument by reference reads the copy in the
// frame, not the temporary the caller passed.
ZEST_CASE(arguments_outlive_the_full_expression_that_passed_them) {
    auto made = co_invoke(
        [](const std::string& text, std::unique_ptr<int> number) -> task<std::string> {
            co_await yield();
            co_return text + std::to_string(*number);
        },
        std::string("kotatsu-"),
        std::make_unique<int>(7));

    auto [result] = run(std::move(made));
    ASSERT(result.has_value());
    EXPECT(*result == "kotatsu-7");
}

ZEST_CASE(invoke_passes_the_error_through) {
    auto failing = []() -> task<int, error> {
        co_await yield();
        co_await fail(error::connection_refused);
    };

    auto [result] = run(co_invoke(failing));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::connection_refused);
}

ZEST_CASE(invoke_passes_the_cancellation_through) {
    auto cancelling = []() -> task<int> {
        co_await yield();
        co_await cancel();
    };
    auto caught = []() -> task<int, void, cancellation> {
        co_await cancel();
    };
    auto awaiting = [&]() -> task<bool> {
        auto result = co_await co_invoke(caught);
        co_return result.is_cancelled();
    };

    auto [plain, as_value] = run(co_invoke(cancelling), awaiting());
    EXPECT(plain.is_cancelled());
    ASSERT(as_value.has_value());
    EXPECT(*as_value);
}

// A cancel of the task co_invoke() gave reaches the task the callable made.
ZEST_CASE(cancel_reaches_the_task_the_callable_made) {
    event gate;
    auto waiting = [&]() -> task<> {
        co_await gate.wait();
    };
    auto target = co_invoke(waiting);
    auto canceler = [&]() -> task<> {
        target.cancel();
        co_return;
    };

    auto [result, cancelled] = run(target, canceler());
    EXPECT(result.is_cancelled());
    EXPECT(!gate.has_waiters());
}

};  // ZEST_SUITE(async_runtime_task_invoke)

}  // namespace

}  // namespace kota
