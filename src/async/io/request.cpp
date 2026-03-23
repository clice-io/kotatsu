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

struct batch_work_op : uv::await_op<batch_work_op> {
    using promise_t = task<void, error>::promise_type;

    // Batch items whose uv_work_t requests should be cancelled.
    std::vector<uv_work_t>* reqs = nullptr;
    std::size_t submitted = 0;
    error result;

    static void on_cancel(system_op* op) {
        auto* self = static_cast<batch_work_op*>(op);
        if(self->reqs) {
            for(std::size_t i = 0; i < self->submitted; ++i) {
                uv::cancel((*self->reqs)[i]);
            }
        }
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

    // Shared state lives on the heap so that after_cb can safely call complete()
    // (which may destroy the coroutine frame) without use-after-free.
    struct batch_state {
        function<void(std::size_t)> fn;
        std::atomic<std::size_t> remaining;
        error first_error;
        batch_work_op* awaiter = nullptr;
        // Keep uv_work_t requests here so batch_work_op::on_cancel can reach them.
        std::vector<uv_work_t> reqs;
        // Per-item index stored alongside each request.
        std::vector<std::size_t> indices;

        batch_state(function<void(std::size_t)> f, std::size_t n) :
            fn(std::move(f)), remaining(n), reqs(n), indices(n) {
            for(std::size_t i = 0; i < n; ++i) {
                indices[i] = i;
            }
        }
    };

    // req.data points to this to recover both the state and the item index.
    struct item_context {
        std::shared_ptr<batch_state> state;
        std::size_t index;
    };

    auto state = std::make_shared<batch_state>(std::move(fn), count);

    batch_work_op op;
    op.reqs = &state->reqs;
    state->awaiter = &op;

    // Heap-allocate contexts; each one holds a shared_ptr keeping state alive.
    auto contexts = std::make_unique<std::vector<item_context>>(count);
    for(std::size_t i = 0; i < count; ++i) {
        (*contexts)[i] = {state, i};
    }

    auto work_cb = [](uv_work_t* req) {
        auto* ctx = static_cast<item_context*>(req->data);
        ctx->state->fn(ctx->index);
    };

    auto after_cb = [](uv_work_t* req, int status) {
        auto* ctx = static_cast<item_context*>(req->data);
        // Copy shared_ptr so state survives even if complete() destroys the frame.
        auto st = ctx->state;

        if(status < 0) {
            st->first_error = uv::status_to_error(status);
        }

        if(st->remaining.fetch_sub(1) == 1) {
            st->awaiter->result = st->first_error;
            st->awaiter->complete();
        }
    };

    std::size_t submitted = 0;
    for(std::size_t i = 0; i < count; ++i) {
        state->reqs[i].data = &(*contexts)[i];

        if(auto err = uv::queue_work(loop, state->reqs[i], work_cb, after_cb)) {
            std::size_t not_submitted = count - i;
            state->remaining.fetch_sub(not_submitted);
            if(state->remaining.load() == 0) {
                co_await fail(err);
            }
            state->first_error = err;
            break;
        }
        ++submitted;
    }

    op.submitted = submitted;

    if(auto err = co_await op) {
        co_await fail(std::move(err));
    }
}

}  // namespace eventide
