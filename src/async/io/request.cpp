#include "eventide/async/io/request.h"

#include <atomic>
#include <cassert>
#include <vector>

#include "awaiter.h"
#include "eventide/async/io/loop.h"
#include "eventide/async/runtime/task.h"
#include "eventide/async/vocab/error.h"

namespace eventide {

namespace {

struct work_op : uv::await_op<work_op> {
    using promise_t = task<void, error>::promise_type;

    // libuv request object; req.data points back to this awaiter.
    uv_work_t req{};
    // User-supplied function executed on libuv's worker thread.
    function<void()> fn;
    // Completion status consumed by await_resume().
    error result;

    explicit work_op(function<void()> fn) : fn(std::move(fn)) {}

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

task<void, error> queue(function<void()> fn, event_loop& loop) {
    work_op op(std::move(fn));

    auto work_cb = [](uv_work_t* req) {
        auto* holder = static_cast<work_op*>(req->data);
        assert(holder != nullptr && "work_cb requires operation in req->data");
        holder->fn();
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

    if(auto err = uv::queue_work(loop, op.req, work_cb, after_cb)) {
        co_await fail(err);
    }

    if(auto err = co_await op) {
        co_await fail(std::move(err));
    }
}

task<void, error> queue_batch(std::size_t count, function<void(std::size_t)> fn, event_loop& loop) {
    if(count == 0) {
        co_return;
    }

    // Shared state: the awaiter that all items report back to, plus an atomic counter.
    struct batch_state {
        work_op* awaiter = nullptr;
        std::atomic<std::size_t> remaining;
        error first_error;

        explicit batch_state(std::size_t n) : remaining(n) {}
    };

    // Per-item request — lightweight, no coroutine frame.
    struct batch_item {
        uv_work_t req{};
        batch_state* state = nullptr;
        function<void(std::size_t)>* fn = nullptr;
        std::size_t index = 0;
    };

    work_op op(function<void()>([]() {}));  // dummy fn, unused

    batch_state state(count);
    state.awaiter = &op;

    std::vector<batch_item> items(count);

    auto work_cb = [](uv_work_t* req) {
        auto* item = static_cast<batch_item*>(req->data);
        (*item->fn)(item->index);
    };

    auto after_cb = [](uv_work_t* req, int status) {
        auto* item = static_cast<batch_item*>(req->data);
        auto* st = item->state;

        if(status < 0) {
            st->first_error = uv::status_to_error(status);
        }

        if(st->remaining.fetch_sub(1) == 1) {
            // Last item completed — resume the awaiter.
            st->awaiter->result = st->first_error;
            st->awaiter->complete();
        }
    };

    op.result.clear();
    op.req.data = &op;

    for(std::size_t i = 0; i < count; ++i) {
        items[i].state = &state;
        items[i].fn = &fn;
        items[i].index = i;
        items[i].req.data = &items[i];

        if(auto err = uv::queue_work(loop, items[i].req, work_cb, after_cb)) {
            // Adjust remaining so the already-submitted items still complete cleanly.
            state.remaining.fetch_sub(count - i);
            if(state.remaining.load() == 0) {
                // Nothing was submitted successfully.
                co_await fail(err);
            }
            state.first_error = err;
            break;
        }
    }

    // Suspend until all submitted items complete.
    if(auto err = co_await op) {
        co_await fail(std::move(err));
    }
}

}  // namespace eventide
