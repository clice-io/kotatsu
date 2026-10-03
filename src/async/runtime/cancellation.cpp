#include "kota/async/runtime/cancellation.h"

#include <utility>

#include "kota/async/runtime/sync.h"

namespace kota {

struct detail::cancellation_state {
    event fired;

    /// The callbacks waiting to run, in the order they were registered.
    cancellation_node* head = nullptr;
    cancellation_node* tail = nullptr;

    void link(cancellation_node& node) noexcept;
    void unlink(cancellation_node& node) noexcept;
};

struct detail::cancellation_node {
    explicit cancellation_node(function<void()> callback) noexcept :
        callback(std::move(callback)) {}

    /// The state whose list holds the callback; null once it has started.
    cancellation_state* state = nullptr;

    cancellation_node* prev = nullptr;
    cancellation_node* next = nullptr;

    function<void()> callback;

    ~cancellation_node() {
        if(state != nullptr) {
            state->unlink(*this);
        }
    }
};

void detail::cancellation_state::link(cancellation_node& node) noexcept {
    node.state = this;
    node.prev = tail;
    (tail ? tail->next : head) = &node;
    tail = &node;
}

void detail::cancellation_state::unlink(cancellation_node& node) noexcept {
    (node.prev ? node.prev->next : head) = node.next;
    (node.next ? node.next->prev : tail) = node.prev;
    node.state = nullptr;
    node.prev = nullptr;
    node.next = nullptr;
}

cancellation_callback::cancellation_callback() noexcept = default;

cancellation_callback::cancellation_callback(
    std::unique_ptr<detail::cancellation_node> node) noexcept : node(std::move(node)) {}

cancellation_callback::cancellation_callback(cancellation_callback&& other) noexcept = default;

cancellation_callback&
    cancellation_callback::operator=(cancellation_callback&& other) noexcept = default;

cancellation_callback::~cancellation_callback() = default;

cancellation_token::cancellation_token(std::shared_ptr<detail::cancellation_state> state) noexcept :
    state(std::move(state)) {}

bool cancellation_token::cancelled() const noexcept {
    return state->fired.is_set();
}

static task<> wait_until_fired(std::shared_ptr<detail::cancellation_state> state) {
    co_await state->fired.wait();
    co_await cancel();
}

task<> cancellation_token::wait() const {
    return wait_until_fired(state);
}

cancellation_callback cancellation_token::on_cancel(function<void()> callback) const {
    if(cancelled()) {
        callback();
        return {};
    }
    auto node = std::make_unique<detail::cancellation_node>(std::move(callback));
    state->link(*node);
    return cancellation_callback(std::move(node));
}

cancellation_source::cancellation_source() :
    state(std::make_shared<detail::cancellation_state>()) {}

void cancellation_source::cancel() noexcept {
    if(state->fired.is_set()) {
        return;
    }
    // A callback may destroy this source, and with it its hold on the state.
    auto held = state;
    held->fired.set();
    while(auto* node = held->head) {
        held->unlink(*node);
        // A callback may destroy its own registration, which frees the node:
        // it runs from here instead.
        auto callback = std::move(node->callback);
        callback();
    }
}

bool cancellation_source::cancelled() const noexcept {
    return state->fired.is_set();
}

cancellation_token cancellation_source::token() const noexcept {
    return cancellation_token(state);
}

}  // namespace kota
