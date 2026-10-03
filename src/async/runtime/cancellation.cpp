#include "kota/async/runtime/cancellation.h"

#include "kota/async/runtime/sync.h"

namespace kota {

namespace detail {

struct cancellation_state {
    event fired;

    /// The callbacks that have not run, in the order they were registered.
    cancellation_node* head = nullptr;
    cancellation_node* tail = nullptr;

    void link(cancellation_node& node) noexcept;
    void unlink(cancellation_node& node) noexcept;
};

struct cancellation_node {
    /// The state whose list holds this node; null once it is off the list.
    std::shared_ptr<cancellation_state> state;
    function<void()> callback;
    cancellation_node* prev = nullptr;
    cancellation_node* next = nullptr;
};

void cancellation_state::link(cancellation_node& node) noexcept {
    node.prev = tail;
    (tail ? tail->next : head) = &node;
    tail = &node;
}

void cancellation_state::unlink(cancellation_node& node) noexcept {
    (node.prev ? node.prev->next : head) = node.next;
    (node.next ? node.next->prev : tail) = node.prev;
    node.prev = nullptr;
    node.next = nullptr;
    node.state = nullptr;
}

}  // namespace detail

cancellation_callback::cancellation_callback() noexcept = default;

cancellation_callback::cancellation_callback(std::unique_ptr<detail::cancellation_node> node) noexcept
    : node(std::move(node)) {}

cancellation_callback::cancellation_callback(cancellation_callback&& other) noexcept = default;

cancellation_callback& cancellation_callback::operator=(cancellation_callback&& other) noexcept {
    if(this != &other) {
        cancellation_callback dropped(std::move(*this));
        node = std::move(other.node);
    }
    return *this;
}

cancellation_callback::~cancellation_callback() {
    if(node && node->state) {
        // unlink() resets the node's state, which may be the last reference.
        auto state = node->state;
        state->unlink(*node);
    }
}

cancellation_token::cancellation_token(std::shared_ptr<detail::cancellation_state> state) noexcept
    : state(std::move(state)) {}

bool cancellation_token::cancelled() const noexcept {
    return state && state->fired.is_set();
}

task<> cancellation_token::wait() const {
    return wait_for(state);
}

task<> cancellation_token::wait_for(std::shared_ptr<detail::cancellation_state> state) {
    if(state) {
        co_await state->fired.wait();
    } else {
        // No source: nothing sets it, and only a cancel ends the wait.
        event never;
        co_await never.wait();
    }
    co_await cancel();
}

cancellation_callback cancellation_token::on_cancel(function<void()> callback) const {
    if(!state) {
        return {};
    }
    if(state->fired.is_set()) {
        callback();
        return {};
    }
    auto node = std::make_unique<detail::cancellation_node>(state, std::move(callback));
    state->link(*node);
    return cancellation_callback(std::move(node));
}

cancellation_source::cancellation_source() : state(std::make_shared<detail::cancellation_state>()) {}

void cancellation_source::cancel() noexcept {
    if(state->fired.is_set()) {
        return;
    }
    // A callback may destroy this source: nothing below touches it.
    auto held = state;
    held->fired.set();
    while(auto* node = held->head) {
        held->unlink(*node);
        // Taken out first: the callback may destroy its own registration.
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
