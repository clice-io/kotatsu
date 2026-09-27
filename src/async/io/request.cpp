#include "kota/async/io/request.h"

#include <utility>

#include "awaiter.h"

namespace kota {

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

}  // namespace kota
