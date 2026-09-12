#include "kota/async/io/loop.h"

#include <atomic>
#include <cassert>
#include <deque>
#include <mutex>
#include <vector>

#include "../libuv.h"
#include "kota/support/functional.h"
#include "kota/async/io/watcher.h"
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
    bool idle_running = false;
    bool check_running = false;
    std::deque<async_node*> tasks;
    std::deque<async_node*> deferred;
    /// Ops suspended via yield(). New ops land in `yields_staged`; each()
    /// promotes the staged batch to `yields_ready` and completes the batch
    /// promoted by the previous each(). The two-step promotion guarantees an
    /// op never resumes in the iteration that enqueued it, no matter which
    /// callback phase (timer, idle, poll, check) it was enqueued from.
    std::deque<io_op*> yields_staged;
    std::deque<io_op*> yields_ready;
    std::vector<function<void()>> destroy_callbacks;
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
        uv::unref(*handle);
    }
}

relay::relay(relay::Self* p) noexcept : self(p) {}

relay::relay(relay&& other) noexcept : self(std::exchange(other.self, nullptr)) {}

relay& relay::operator=(relay&& other) noexcept {
    if(this != &other) {
        auto* old = std::exchange(self, std::exchange(other.self, nullptr));
        if(old) {
            old->count.fetch_sub(1, std::memory_order_release);
            uv::async_send(old->async);
        }
    }
    return *this;
}

relay::~relay() {
    if(self) {
        self->count.fetch_sub(1, std::memory_order_release);
        uv::async_send(self->async);
    }
}

void relay::send(function<void()> callback) {
    if(!self) {
        return;
    }
    std::lock_guard lock(self->mutex);
    self->queue.push_back(std::move(callback));
    uv::async_send(self->async);
}

relay event_loop::create_relay() {
    if(self->count.fetch_add(1, std::memory_order_relaxed) == 0) {
        uv::ref(self->async);
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

void each(uv_idle_t* idle) {
    auto self = static_cast<event_loop::Self*>(idle->data);

    if(self->idle_running && self->tasks.empty() && self->yields_staged.empty() &&
       self->yields_ready.empty()) {
        self->idle_running = false;
        uv::idle_stop(*idle);
        return;
    }

    // Promote the staged yields and snapshot the task batch up front:
    // anything produced by the resumes below belongs to a later iteration.
    auto yielded = std::move(self->yields_ready);
    self->yields_ready = std::move(self->yields_staged);
    auto all = std::move(self->tasks);

    for(auto& task: all) {
        task->resume();
    }

    // Complete the previously promoted yields after this iteration's
    // scheduled tasks — yield() resumes only once everything queued before
    // it has run. A cancelled yield op is never dequeued early; its
    // completion here reports the Cancelled state, so no entry in this
    // batch can dangle.
    for(auto* op: yielded) {
        op->complete();
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
    auto& loop = *this;
    if(!loop->idle_running && loop->tasks.empty()) {
        loop->idle_running = true;
        uv::idle_start(loop->idle, each);
    }
    loop->tasks.push_back(&frame);
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
    auto* self = static_cast<event_loop::Self*>(handle->data);
    drain_deferred_queue(self);
    if(self->check_running) {
        self->check_running = false;
        uv::check_stop(*handle);
        uv::unref(*handle);
    }
}

void event_loop::defer_resume(async_node& node) {
    self->deferred.push_back(&node);
    if(!self->check_running) {
        self->check_running = true;
        uv::ref(self->check);
        uv::check_start(self->check, on_check);
    }
}

void event_loop::drain_deferred() {
    drain_deferred_queue(self.get());
}

void event_loop::on_destroy(function<void()> callback) {
    self->destroy_callbacks.push_back(std::move(callback));
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
    auto* self = loop->operator->();

    // Enqueue before attach: when the parent is already cancelled, attach's
    // cancellation checkpoint cancels this op in place, and the queued
    // completion still resolves it in a later iteration.
    self->yields_staged.push_back(this);
    if(!self->idle_running) {
        self->idle_running = true;
        uv::idle_start(self->idle, each);
    }

    return attach(parent_node, loc);
}

event_loop::event_loop() : self(new Self()) {
    auto& loop = self->loop;
    if(auto err = uv::loop_init(loop)) {
        abort();
    }

    auto& idle = self->idle;
    uv::idle_init(loop, idle);
    idle.data = self.get();

    auto& check = self->check;
    uv::check_init(loop, check);
    check.data = self.get();
    uv::unref(check);

    auto& async = self->async;
    uv::async_init(loop, async, on_relay);
    async.data = self.get();
    uv::unref(async);
}

event_loop::~event_loop() {
    constexpr static auto cleanup = +[](uv_handle_t* h, void* arg) {
        auto* self = static_cast<event_loop::Self*>(arg);
        if(!uv::is_closing(*h)) {
            auto* idle = uv::as_handle(self->idle);
            auto* check = uv::as_handle(self->check);
            auto* async = uv::as_handle(self->async);
            if(h == idle || h == check || h == async) {
                uv::close(*h, nullptr);
                return;
            }

            uv::close(*h, [](uv_handle_t* handle) { uv::loop_close_fallback::mark(handle); });
        }
    };

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

    auto& loop = self->loop;
    auto close_err = uv::loop_close(loop);
    if(close_err.value() == UV_EBUSY) {
        uv::walk(loop, cleanup, self.get());

        // Run event loop to trigger all close callbacks.
        while((close_err = uv::loop_close(loop)).value() == UV_EBUSY) {
            uv::run(loop, UV_RUN_ONCE);
        }
    }
}

event_loop::operator uv_loop_t&() noexcept {
    return self->loop;
}

event_loop::operator const uv_loop_t&() const noexcept {
    return self->loop;
}

int event_loop::run() {
    // Nesting a different loop is supported (saved/restored below);
    // re-entering the same loop is undefined behavior in libuv.
    assert(current_loop != this && "event_loop::run() re-entered on the same loop");
    auto previous = current_loop;
    current_loop = this;
    const int result = uv::run(self->loop, UV_RUN_DEFAULT);
    current_loop = previous;
    return result;
}

void event_loop::stop() {
    uv::stop(self->loop);
}

}  // namespace kota
