#include "kota/async/runtime/cancellation.h"

#include "kota/async/runtime/sync.h"

namespace kota {

namespace detail {

struct CancellationState {
    event fired;

    /// The callbacks that have not run, in the order they were registered.
    CancellationNode* head = nullptr;
    CancellationNode* tail = nullptr;

    void link(CancellationNode& node) noexcept;
    void unlink(CancellationNode& node) noexcept;
};

struct CancellationNode {
    /// The state whose list holds this node; null once it is off the list.
    /// The source keeps it while the node is on it: it takes every node off
    /// when it cancels, which it does when it goes.
    CancellationState* state;
    function<void()> callback;
    CancellationNode* prev = nullptr;
    CancellationNode* next = nullptr;
};

void CancellationState::link(CancellationNode& node) noexcept {
    node.prev = tail;
    (tail ? tail->next : head) = &node;
    tail = &node;
}

void CancellationState::unlink(CancellationNode& node) noexcept {
    (node.prev ? node.prev->next : head) = node.next;
    (node.next ? node.next->prev : tail) = node.prev;
    node.prev = nullptr;
    node.next = nullptr;
    node.state = nullptr;
}

}  // namespace detail

cancellation_callback::cancellation_callback() noexcept = default;

cancellation_callback::cancellation_callback(std::unique_ptr<detail::CancellationNode> node) noexcept
    : node(std::move(node)) {}

cancellation_callback::cancellation_callback(cancellation_callback&& other) noexcept = default;

cancellation_callback& cancellation_callback::operator=(cancellation_callback other) noexcept {
    std::swap(node, other.node);
    return *this;
}

cancellation_callback::~cancellation_callback() {
    if(node && node->state) {
        node->state->unlink(*node);
    }
}

cancellation_token::cancellation_token(std::shared_ptr<detail::CancellationState> state) noexcept
    : state(std::move(state)) {}

bool cancellation_token::cancelled() const noexcept {
    return state && state->fired.is_set();
}

task<> cancellation_token::wait() const {
    return wait_for(state);
}

task<> cancellation_token::wait_for(std::shared_ptr<detail::CancellationState> state) {
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
    auto node = std::make_unique<detail::CancellationNode>(state.get(), std::move(callback));
    state->link(*node);
    return cancellation_callback(std::move(node));
}

cancellation_source::cancellation_source() : state(std::make_shared<detail::CancellationState>()) {}

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
