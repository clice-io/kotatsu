#pragma once

#include <cassert>
#include <source_location>
#include <type_traits>
#include <vector>

#if KOTA_ENABLE_EXCEPTIONS
#include <exception>
#endif

#include "kota/support/config.h"
#include "kota/async/runtime/node.h"
#include "kota/async/runtime/traits.h"
#include "kota/async/vocab/outcome.h"

namespace kota {

template <typename... Errors>
class task_group : public aggregate_op {
public:
    using error_type = detail::task_group_error_type_t<Errors...>;
    using result_type = std::conditional_t<std::is_void_v<error_type>,
                                           void,
                                           outcome<void, std::vector<error_type>, void>>;

    explicit task_group([[maybe_unused]] event_loop& loop) : aggregate_op(NodeKind::TaskGroup) {}

    task_group(const task_group&) = delete;
    task_group& operator=(const task_group&) = delete;
    task_group(task_group&&) = delete;
    task_group& operator=(task_group&&) = delete;

    ~task_group() {
        for(auto* child: children) {
            // Null slots are tombstones of eagerly reclaimed children.
            if(!child) {
                continue;
            }
            assert(child->kind == async_node::NodeKind::Task);
            assert((child->state == async_node::Finished || child->state == async_node::Cancelled ||
                    child->state == async_node::Failed) &&
                   "task_group destroyed before all children completed; co_await join() first");
            static_cast<task_frame*>(child)->handle().destroy();
        }
    }

    template <typename T, typename E, typename C>
        requires std::is_void_v<E> || is_one_of<E, Errors...>
    bool spawn(task<T, E, C>&& t) {
        if(decided() || settled) {
            return false;
        }

        // Compact the tombstones left by eager child-frame reclamation once
        // they outnumber the live slots, so a long-lived group stays bounded
        // by its in-flight children rather than its lifetime spawn count.
        if(reclaimed * 2 > children.size()) {
            compact_children();
        }

        auto* node = detail::node_from(t);
        node->intercept_cancel();

        children.reserve(children.size() + 1);
        error_handlers.reserve(error_handlers.size() + 1);

        t.release();
        ++pending;
        children.push_back(node);
        error_handlers.push_back(&extract_error<T, E>);

        auto handle = node->attach(*this, std::source_location::current());
        async_node::resume_and_drain(handle);
        return true;
    }

    /// Aborts the remaining children and lets join() resume normally with
    /// the collected errors. Distinct from cancelling the group node
    /// through the task tree (async_node::cancel), which settles the whole
    /// group — and its joiner — as Cancelled.
    void abort() {
        if(decided() || settled) {
            return;
        }
        {
            // The cascade may complete every child synchronously; the pin
            // keeps those completions from settling mid-loop.
            pin held(*this);
            decide(Decision::Resume);
        }
        async_node::resume_and_drain(settle_if_idle());
    }

    auto join() {
        return join_awaiter{*this};
    }

private:
    using error_handler_fn = void (*)(async_node& child, task_group& group);

    struct join_awaiter {
        task_group& group;

        bool await_ready() noexcept {
            assert(!group.joined && "join() called twice on the same task_group");
            if(group.pending == 0) {
                group.settled = true;
                group.state = Finished;
                return true;
            }
            return false;
        }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<Promise> h,
            std::source_location location = std::source_location::current()) noexcept {
            assert(!group.settled && "join() called twice on the same task_group");
            assert(group.pending > 0 &&
                   "join await_suspend called even though task_group is complete");
            group.location = location;
            auto* parent_node = static_cast<async_node*>(&h.promise());
            assert(parent_node->is_task_frame() && "task_group join must be awaited from a task");
            static_cast<task_frame*>(parent_node)->set_child(&group);
            group.parent = parent_node;
            group.state = Running;

            // Cancellation checkpoint: the parent was cancelled while it was
            // executing, so its cancel cascade could not reach this group
            // (it was not attached yet). Unlike the other checkpoints, the
            // children are already running and must complete structurally:
            // cancel them now and settle once the last one finishes.
            if(parent_node->state == Cancelled) {
                group.state = Cancelled;
                {
                    pin held(group);
                    group.cancel_children();
                }
                return group.settle_if_idle();
            }

            return std::noop_coroutine();
        }

        result_type await_resume() {
            group.joined = true;
            group.collect_errors();

#if KOTA_ENABLE_EXCEPTIONS
            if(!group.exceptions.empty()) {
                std::rethrow_exception(group.exceptions.front());
            }
#endif

            if constexpr(!std::is_void_v<error_type>) {
                if(!group.errors.empty()) {
                    return result_type(outcome_error(std::move(group.errors)));
                }
                return result_type();
            }
        }
    };

    void collect_errors() {
        assert(children.size() == error_handlers.size() &&
               "task_group child/error-handler vectors diverged");
        for(std::size_t i = 0; i < children.size(); ++i) {
            if(children[i] && children[i]->state == async_node::Failed) {
                error_handlers[i](*children[i], *this);
            }
        }
    }

    void compact_children() noexcept {
        std::size_t live = 0;
        for(std::size_t i = 0; i < children.size(); ++i) {
            if(children[i]) {
                children[live] = children[i];
                error_handlers[live] = error_handlers[i];
                live += 1;
            }
        }
        children.resize(live);
        error_handlers.resize(live);
        reclaimed = 0;
    }

    template <typename T, typename E>
    static void extract_error(async_node& child, task_group& g) {
        if(child.propagated_exception) {
#if KOTA_ENABLE_EXCEPTIONS
            g.exceptions.push_back(child.propagated_exception);
#endif
            return;
        }

        if constexpr(!std::is_void_v<E>) {
            auto* promise =
                static_cast<task_promise_object<T, E>*>(static_cast<task_frame*>(&child));
            if(promise->value.has_value() && promise->value->has_error()) {
                if constexpr(std::is_same_v<E, error_type>) {
                    g.errors.push_back(std::move(*promise->value).error());
                } else {
                    g.errors.push_back(error_type(std::move(*promise->value).error()));
                }
            }
        }
    }

    bool joined = false;

    std::vector<error_handler_fn> error_handlers;

    std::
        conditional_t<std::is_void_v<error_type>, std::type_identity<void>, std::vector<error_type>>
            errors;

#if KOTA_ENABLE_EXCEPTIONS
    std::vector<std::exception_ptr> exceptions;
#endif
};

task_group(event_loop&) -> task_group<>;

}  // namespace kota
