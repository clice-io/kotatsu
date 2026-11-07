#include "kotatsu/async/io/request.h"

#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "awaiter.h"
#include "kotatsu/support/scope_exit.h"
#include "kotatsu/sys/env.h"

namespace kotatsu::async {

namespace {

/// Work on libuv's thread pool. Its status is 0, or ECANCELED once the work
/// was dequeued by a cancel, which ended the task already: nothing reads it.
struct work_op : uv::request_op<work_op, uv_work_t> {
    uv_loop_t* loop;
    function<void()> work;
    function<void()> hook;

    work_op(uv_loop_t* loop, function<void()> work, function<void()> hook) :
        loop(loop), work(std::move(work)), hook(std::move(hook)) {}

    bool start() noexcept {
        // uv_queue_work fails only without a work callback.
        ::uv_queue_work(loop, &req, run, on_done);
        return true;
    }

    void cancel() noexcept {
        // Dequeue first, so that the hook cannot make room on the pool for
        // this very work before uv_cancel runs. Failing that, the work runs
        // (or just ran), and the hook tells it to return early.
        if(::uv_cancel(reinterpret_cast<uv_req_t*>(&req)) != 0) {
            hook();
        }
    }

    static void run(uv_work_t* req) {
        static_cast<work_op*>(static_cast<request_op*>(req->data))->work();
    }
};

}  // namespace

task<> detail::run_on_pool(function<void()> work, function<void()> on_cancel, event_loop& loop) {
    co_await work_op(loop.native_handle(), std::move(work), std::move(on_cancel));
}

error set_thread_pool_size(unsigned threads) {
    if(threads < 1 || threads > 1024) {
        return error::invalid_argument;
    }
    constexpr std::string_view variable = "UV_THREADPOOL_SIZE";
    std::string previous;
    const bool was_set = sys::get_env(previous, variable);
    // With this name and a number, only memory can run out.
    if(sys::set_env(variable, std::format("{}", threads))) {
        return error::not_enough_memory;
    }
    ScopeExit restore([&] {
        [[maybe_unused]] auto restored =
            was_set ? sys::set_env(variable, previous) : sys::unset_env(variable);
    });

    // libuv starts the pool, reading the variable, at the first work queued
    // in the process, and waits for its threads to start.
    uv_loop_t loop;
    if(auto failed = error(::uv_loop_init(&loop))) {
        return failed;
    }
    uv_work_t work{};
    ::uv_queue_work(&loop, &work, [](uv_work_t*) {}, [](uv_work_t*, int) {});
    ::uv_run(&loop, UV_RUN_DEFAULT);
    ::uv_loop_close(&loop);
    return {};
}

}  // namespace kotatsu::async
