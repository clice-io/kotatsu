#include "kota/async/io/loop.h"

#include <atomic>
#include <cassert>
#include <deque>
#include <mutex>
#include <vector>

#include "../libuv.h"
#include "kota/support/functional.h"
#include "kota/async/runtime/node.h"

namespace kota {

struct relay::Self {
    uv_async_t async = {};
    std::mutex mutex;
    std::vector<function<void()>> queue;
    std::atomic<int> count{0};
};

struct event_loop::Self : relay::Self {
    uv_loop_t loop = {};
    uv_idle_t idle = {};
    uv_check_t check = {};
    std::deque<async_node*> tasks;
    std::deque<async_node*> deferred;
    /// Ops to complete on a later iteration: yields, and waits that stop()
    /// or a destructor aborted. New ops land in `staged`; each() promotes
    /// the staged batch to `ready` and completes the batch promoted by the
    /// previous each(). The two-step promotion guarantees an op never
    /// completes in the iteration that enqueued it, no matter which callback
    /// phase (timer, idle, poll, check) it was enqueued from.
    std::deque<io_op*> staged;
    std::deque<io_op*> ready;
    std::vector<function<void()>> destroy_callbacks;
    /// What owners let go of while the loop was being destroyed and closing
    /// their handles; run once it has closed them all.
    std::vector<function<void()>> frees;

    /// Keeps each() running while it has work; starting a running idle
    /// handle does nothing. Once the loop is being destroyed, and has closed
    /// the idle handle, it runs nothing more.
    void ensure_idle();
};

static void on_relay(uv_async_t* handle) {
    auto* self = static_cast<event_loop::Self*>(handle->data);
    std::vector<function<void()>> batch;
    {
        std::lock_guard lock(self->mutex);
        batch = std::move(self->queue);
    }
    for(auto& cb: batch) {
        cb();
    }

    // Release the loop hold only when no relay is alive AND the queue is
    // still empty. A producer may send() and destroy its relay while the
    // batch above is draining; the re-armed async wakeup alone would not
    // keep uv_run alive once the handle is unreffed, and the refilled queue
    // would be dropped. count == 0 (acquire) pairs with the release
    // decrement in ~relay: it guarantees every send() on the destroyed
    // relays is already visible in the queue, so checking both under the
    // mutex is race-free.
    std::lock_guard lock(self->mutex);
    if(self->count.load(std::memory_order_acquire) == 0 && self->queue.empty()) {
        ::uv_unref(reinterpret_cast<uv_handle_t*>(handle));
    }
}

relay::relay(relay::Self* p) noexcept : self(p) {}

relay::relay(relay&& other) noexcept : self(std::exchange(other.self, nullptr)) {}

relay& relay::operator=(relay&& other) noexcept {
    if(this != &other) {
        auto* old = std::exchange(self, std::exchange(other.self, nullptr));
        if(old) {
            old->count.fetch_sub(1, std::memory_order_release);
            ::uv_async_send(&old->async);
        }
    }
    return *this;
}

relay::~relay() {
    if(self) {
        self->count.fetch_sub(1, std::memory_order_release);
        ::uv_async_send(&self->async);
    }
}

void relay::send(function<void()> callback) {
    if(!self) {
        return;
    }
    std::lock_guard lock(self->mutex);
    self->queue.push_back(std::move(callback));
    ::uv_async_send(&self->async);
}

relay event_loop::create_relay() {
    if(self->count.fetch_add(1, std::memory_order_relaxed) == 0) {
        ::uv_ref(reinterpret_cast<uv_handle_t*>(&self->async));
    }
    return relay(self.get());
}

static thread_local event_loop* current_loop = nullptr;

event_loop& event_loop::current() {
    assert(current_loop && "event_loop::current() called outside a running loop");
    return *current_loop;
}

bool event_loop::has_current() noexcept {
    return current_loop != nullptr;
}

static void each(uv_idle_t* idle) {
    auto self = static_cast<event_loop::Self*>(idle->data);

    if(self->tasks.empty() && self->staged.empty() && self->ready.empty()) {
        ::uv_idle_stop(idle);
        return;
    }

    // Promote the staged ops and snapshot the task batch up front: anything
    // produced by the resumes below belongs to a later iteration.
    auto completing = std::move(self->ready);
    self->ready = std::move(self->staged);
    auto all = std::move(self->tasks);

    for(auto& task: all) {
        task->resume();
    }

    // Complete the previously promoted ops after this iteration's scheduled
    // tasks — yield() resumes only once everything queued before it has run.
    // A cancelled op is never dequeued early; its completion here reports
    // the Cancelled state, so no entry in this batch can dangle.
    for(auto* op: completing) {
        op->complete();
    }
}

void event_loop::Self::ensure_idle() {
    if(!::uv_is_closing(reinterpret_cast<uv_handle_t*>(&idle))) {
        ::uv_idle_start(&idle, each);
    }
}

void event_loop::schedule(async_node& frame, std::source_location loc) {
    assert(self && "schedule: no current event loop in this thread");

    if(frame.state == async_node::Pending) {
        frame.state = async_node::Running;
    } else if(frame.state == async_node::Finished || frame.state == async_node::Running) {
        std::abort();
    }

    frame.location = loc;
    self->ensure_idle();
    self->tasks.push_back(&frame);
}

static void drain_deferred_queue(event_loop::Self* self) {
    while(!self->deferred.empty()) {
        auto batch = std::move(self->deferred);
        for(auto* node: batch) {
            node->resume();
        }
    }
}

static void on_check(uv_check_t* handle) {
    drain_deferred_queue(static_cast<event_loop::Self*>(handle->data));
    ::uv_check_stop(handle);
}

void event_loop::defer_resume(async_node& node) {
    self->deferred.push_back(&node);
    ::uv_check_start(&self->check, on_check);
}

void event_loop::drain_deferred() {
    drain_deferred_queue(self.get());
}

void event_loop::on_destroy(function<void()> callback) {
    self->destroy_callbacks.push_back(std::move(callback));
}

void uv::complete_later(uv_loop_t& loop, io_op& op) {
    auto* self = static_cast<event_loop::Self*>(loop.data);
    self->staged.push_back(&op);
    self->ensure_idle();
}

void uv::free_when_closed(uv_loop_t& loop, function<void()> free) {
    static_cast<event_loop::Self*>(loop.data)->frees.push_back(std::move(free));
}

yield_awaiter::yield_awaiter(event_loop& loop) noexcept : loop(&loop) {
    // Cancellation needs no action: the op is intentionally left queued, and
    // the queued completion in each() delivers the Cancelled outcome on the
    // next iteration (structured completion). Never dequeuing on cancel is
    // also what keeps the each() batch free of dangling pointers.
    action = +[](io_op*) {
    };
}

std::coroutine_handle<> yield_awaiter::suspend(async_node& parent_node,
                                               std::source_location loc) noexcept {
    // Enqueue before attach: when the parent is already cancelled, attach's
    // cancellation checkpoint cancels this op in place, and the queued
    // completion still resolves it in a later iteration.
    uv::complete_later(*loop->native_handle(), *this);
    return attach(parent_node, loc);
}

event_loop::event_loop() : self(new Self()) {
    auto* loop = &self->loop;
    if(::uv_loop_init(loop) != 0) {
        std::abort();
    }
    loop->data = self.get();

    ::uv_idle_init(loop, &self->idle);
    self->idle.data = self.get();

    ::uv_check_init(loop, &self->check);
    self->check.data = self.get();

    ::uv_async_init(loop, &self->async, on_relay);
    self->async.data = self.get();
    ::uv_unref(reinterpret_cast<uv_handle_t*>(&self->async));
}

event_loop::~event_loop() {
    assert(self->count.load(std::memory_order_acquire) == 0 &&
           "event_loop destroyed with live relays");

    {
        std::lock_guard lock(self->mutex);
        self->queue.clear();
    }

    auto callbacks = std::move(self->destroy_callbacks);
    for(auto& callback: callbacks) {
        callback();
    }

    auto* loop = &self->loop;
    if(::uv_loop_close(loop) == UV_EBUSY) {
        // Close what is still open. Owners may outlive the loop: a cleared
        // `data` tells them the handle is closed already, so that they free
        // their state themselves (uv::owned_handle::destroy).
        ::uv_walk(
            loop,
            [](uv_handle_t* handle, void*) {
                if(!::uv_is_closing(handle)) {
                    ::uv_close(handle, [](uv_handle_t* closed) { closed->data = nullptr; });
                }
            },
            nullptr);

        // Run the loop until every close callback has fired. The requests
        // they end resume their tasks, which may drop what they own.
        while(::uv_loop_close(loop) == UV_EBUSY) {
            ::uv_run(loop, UV_RUN_ONCE);
        }
    }

    for(auto& free: self->frees) {
        free();
    }
}

uv_loop_t* event_loop::native_handle() noexcept {
    return &self->loop;
}

int event_loop::run() {
    auto previous = current_loop;
    current_loop = this;
    const int result = ::uv_run(&self->loop, UV_RUN_DEFAULT);
    current_loop = previous;
    return result;
}

void event_loop::stop() {
    ::uv_stop(&self->loop);
}

}  // namespace kota
