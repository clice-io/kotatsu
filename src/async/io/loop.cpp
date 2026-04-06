#include "eventide/async/io/loop.h"

#include <atomic>
#include <cassert>
#include <deque>

#include "../libuv.h"
#include "eventide/common/functional.h"
#include "eventide/async/runtime/frame.h"

namespace eventide {

/// A node in the lock-free MPSC (multi-producer, single-consumer) queue
/// used by event_loop::post(). Each post() allocates a node, atomically
/// pushes it onto the intrusive stack, and signals uv_async_t. The event
/// loop thread pops all nodes in one atomic exchange and executes them.
struct post_node {
    function<void()> callback;
    post_node* next = nullptr;
};

struct event_loop::self {
    uv_loop_t loop = {};
    uv_idle_t idle = {};
    uv_async_t async = {};
    bool idle_running = false;
    std::deque<async_node*> tasks;

    /// Lock-free MPSC stack head. Writers (any thread) push via CAS in
    /// post(); the single consumer (event loop thread) drains via exchange
    /// in the uv_async_t callback. No mutex required.
    std::atomic<post_node*> post_head{nullptr};
};

// ── relay implementation ────────────────────────────────────────────

struct relay::self {
    uv_async_t async = {};
    std::atomic<bool> sent{false};
    bool has_callback = false;
    function<void()> callback{+[] {}};
};

static void on_relay(uv_async_t* handle) {
    auto* p = static_cast<struct relay::self*>(handle->data);
    if(p->has_callback) {
        p->callback();
    }
    // Close the handle, releasing the loop hold. The close callback
    // frees the impl once libuv is done with the handle.
    uv::close(*handle, [](uv_handle_t* h) { delete static_cast<struct relay::self*>(h->data); });
}

relay::relay(struct relay::self* p) noexcept : self(p) {}

relay::relay(relay&& other) noexcept : self(std::exchange(other.self, nullptr)) {}

relay& relay::operator=(relay&& other) noexcept {
    if(this != &other) {
        auto* old = std::exchange(self, std::exchange(other.self, nullptr));
        if(old) {
            // Release the old handle by triggering an empty send.
            relay tmp(old);
            tmp.send([] {});
        }
    }
    return *this;
}

relay::~relay() {
    if(self) {
        // The relay was never sent; close the handle to release the loop hold.
        self->has_callback = false;
        self->sent.store(true, std::memory_order_release);
        uv::async_send(self->async);
        self = nullptr;
    }
}

void relay::send(function<void()> callback) {
    auto* p = std::exchange(self, nullptr);
    if(!p) {
        return;  // Already sent or moved-from.
    }
    // Only the first send() takes effect; guard against concurrent calls.
    if(p->sent.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    p->has_callback = true;
    p->callback = std::move(callback);
    uv::async_send(p->async);
}

relay event_loop::create_relay() {
    auto* p = new struct relay::self();
    uv::async_init(self->loop, p->async, on_relay);
    p->async.data = p;
    // The async handle is ref'd by default, keeping the loop alive.
    return relay(p);
}

// ── event_loop ──────────────────────────────────────────────────────

static thread_local event_loop* current_loop = nullptr;

event_loop& event_loop::current() {
    assert(current_loop && "event_loop::current() called outside a running loop");
    return *current_loop;
}

void each(uv_idle_t* idle) {
    auto self = static_cast<struct event_loop::self*>(idle->data);
    if(self->idle_running && self->tasks.empty()) {
        self->idle_running = false;
        uv::idle_stop(*idle);
        return;
    }

    /// Resume may create new tasks, we want to run them in the next iteration.
    auto all = std::move(self->tasks);
    for(auto& task: all) {
        task->resume();
    }
}

void event_loop::schedule(async_node& frame, std::source_location location) {
    assert(self && "schedule: no current event loop in this thread");

    if(frame.state == async_node::Pending) {
        frame.state = async_node::Running;
    } else if(frame.state == async_node::Finished || frame.state == async_node::Running) {
        std::abort();
    }

    frame.location = location;
    auto& self = *this;
    if(!self->idle_running && self->tasks.empty()) {
        self->idle_running = true;
        uv::idle_start(self->idle, each);
    }
    self->tasks.push_back(&frame);
}

void on_post(uv_async_t* handle) {
    auto* self = static_cast<struct event_loop::self*>(handle->data);

    // Atomically steal the entire pending list. Producers may keep
    // pushing concurrently — those nodes will be picked up next time.
    auto* head = self->post_head.exchange(nullptr, std::memory_order_acquire);

    // The stack is in LIFO order; reverse it to preserve FIFO submission order.
    post_node* reversed = nullptr;
    while(head) {
        auto* next = head->next;
        head->next = reversed;
        reversed = head;
        head = next;
    }

    // Execute all callbacks on the event loop thread, then free nodes.
    while(reversed) {
        auto* node = reversed;
        reversed = reversed->next;
        node->callback();
        delete node;
    }
}

void event_loop::post(function<void()> callback) {
    assert(self && "post: event loop has been destroyed");

    auto* node = new post_node{std::move(callback)};

    // Lock-free push: CAS the new node onto the head of the stack.
    // acq_rel on success: release makes this node's callback visible to
    // the consumer; acquire chains the visibility of all nodes pushed by
    // earlier producers (without acquire here, the consumer could follow
    // the next-pointer chain but see uninitialised callback data on
    // weakly-ordered architectures like ARM).
    // uv_async_send is thread-safe and coalescing — multiple sends
    // before the loop iterates result in a single callback invocation,
    // which is fine because on_post drains the entire list each time.
    auto* head = self->post_head.load(std::memory_order_relaxed);
    do {
        node->next = head;
    } while(!self->post_head.compare_exchange_weak(head,
                                                   node,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_relaxed));

    uv::async_send(self->async);
}

event_loop::event_loop() : self(new struct self()) {
    auto& loop = self->loop;
    if(auto err = uv::loop_init(loop)) {
        abort();
    }

    auto& idle = self->idle;
    uv::idle_init(loop, idle);
    idle.data = self.get();

    auto& async = self->async;
    uv::async_init(loop, async, on_post);
    async.data = self.get();
    // Unref so the async handle alone does not keep the loop alive.
    uv::unref(async);
}

event_loop::~event_loop() {
    constexpr static auto cleanup = +[](uv_handle_t* h, void* arg) {
        auto* self = static_cast<struct event_loop::self*>(arg);
        if(!uv::is_closing(*h)) {
            auto* idle = uv::as_handle(self->idle);
            auto* async = uv::as_handle(self->async);
            if(h == idle || h == async) {
                uv::close(*h, nullptr);
                return;
            }

            uv::close(*h, [](uv_handle_t* handle) { uv::loop_close_fallback::mark(handle); });
        }
    };

    // Drain any remaining posted callbacks that were never delivered
    // (e.g. posted after the loop stopped running).
    auto* leaked = self->post_head.exchange(nullptr, std::memory_order_acquire);
    while(leaked) {
        auto* next = leaked->next;
        delete leaked;
        leaked = next;
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
    auto previous = current_loop;
    current_loop = this;
    const int result = uv::run(self->loop, UV_RUN_DEFAULT);
    current_loop = previous;
    return result;
}

void event_loop::stop() {
    uv::stop(self->loop);
}

}  // namespace eventide
