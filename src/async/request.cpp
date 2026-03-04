#include "eventide/async/request.h"

#include <cassert>

#include "awaiter.h"
#include "eventide/async/error.h"
#include "eventide/async/loop.h"
#include "eventide/async/task.h"

namespace eventide {

namespace {

struct work_op : uv::await_op<work_op> {
    using promise_t = task<error>::promise_type;

    // libuv request object; req.data points back to this awaiter.
    uv_work_t req{};
    // User-supplied function executed on libuv's worker thread.
    work_fn fn;
    // Completion status consumed by await_resume().
    error result;

    work_op() = default;

    static void on_cancel(system_op* op) {
        auto* self = static_cast<work_op*>(op);
        uv::cancel(self->req);
    }

    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<promise_t> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        return this->link_continuation(&waiting.promise(), location);
    }

    error await_resume() noexcept {
        return result;
    }
};

}  // namespace

task<error> queue(work_fn fn, event_loop& loop) {
    work_op op;
    op.fn = std::move(fn);

    auto work_cb = [](uv_work_t* req) {
        auto* holder = static_cast<work_op*>(req->data);
        assert(holder != nullptr && "work_cb requires operation in req->data");
        if(holder->fn) {
            holder->fn();
        }
    };

    auto after_cb = [](uv_work_t* req, int status) {
        auto* holder = static_cast<work_op*>(req->data);
        assert(holder != nullptr && "after_cb requires operation in req->data");

        holder->mark_cancelled_if(status);
        holder->result = uv::status_to_error(status);
        holder->complete();
    };

    op.result.clear();
    op.req.data = &op;

    auto err = uv::queue_work(loop, op.req, work_cb, after_cb);
    if(err) {
        co_return err;
    }

    co_return co_await op;
}

}  // namespace eventide
