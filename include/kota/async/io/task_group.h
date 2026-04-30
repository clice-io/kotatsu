#pragma once

#include <cassert>
#include <source_location>
#include <type_traits>
#include <vector>

#if KOTA_ENABLE_EXCEPTIONS
#include <exception>
#endif

#include "kota/support/config.h"
#include "kota/support/type_list.h"
#include "kota/async/runtime/frame.h"
#include "kota/async/runtime/task.h"
#include "kota/async/vocab/outcome.h"

namespace kota {

namespace detail {

template <typename... Ts>
using task_group_error_type_t =
    typename type_list_to_union<type_list_unique_t<type_list<Ts...>>>::type;

}  // namespace detail

template <typename... Errors>
class task_group : public task_group_node {
public:
    using error_type = detail::task_group_error_type_t<Errors...>;
    using result_type = std::conditional_t<std::is_void_v<error_type>,
                                           void,
                                           outcome<void, std::vector<error_type>, void>>;

    explicit task_group([[maybe_unused]] event_loop& loop) {}

    task_group(const task_group&) = delete;
    task_group& operator=(const task_group&) = delete;
    task_group(task_group&&) = delete;
    task_group& operator=(task_group&&) = delete;

    ~task_group() {
        for(auto* child: awaitees) {
            if(child) {
                detail::destroy_or_detach(child);
            }
        }
    }

    template <typename T, typename E, typename C>
        requires std::is_void_v<E> || is_one_of<E, Errors...>
    bool spawn(task<T, E, C>&& t) {
        if(stopped || phase == Phase::Settled) {
            return false;
        }

        auto* node = detail::node_from(t);
        node->intercept_cancel();

        awaitees.reserve(awaitees.size() + 1);
        error_handlers.reserve(error_handlers.size() + 1);

        t.release();
        ++total;
        awaitees.push_back(node);
        error_handlers.push_back(&extract_error<T, E>);

        auto handle = node->link_continuation(this, std::source_location::current());
        detail::resume_and_drain(handle);
        return true;
    }

    void cancel() {
        if(stopped) {
            return;
        }
        stopped = true;
        cancel_children();

        if(deferred != Deferred::None && awaiter) {
            phase = Phase::Settled;
            auto handle = deliver_deferred();
            detail::resume_and_drain(handle);
        }
    }

    auto join() {
        return join_awaiter{*this};
    }

private:
    struct join_awaiter {
        task_group& group;

        bool await_ready() noexcept {
            if(group.completed >= group.total) {
                group.phase = Phase::Settled;
                return true;
            }
            return false;
        }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<Promise> h,
            std::source_location location = std::source_location::current()) noexcept {
            assert(group.phase != Phase::Settled && "join() called twice on the same task_group");
            group.location = location;
            auto* awaiter_node = static_cast<async_node*>(&h.promise());
            if(awaiter_node->kind == NodeKind::Task) {
                static_cast<standard_task*>(awaiter_node)->set_awaitee(&group);
            }
            group.awaiter = awaiter_node;
            group.state = Running;

            // Defensive: in cooperative single-threaded scheduling this cannot
            // differ from await_ready, but guards against future changes.
            if(group.completed >= group.total) {
                group.phase = Phase::Settled;
                awaiter_node->clear_awaitee();
                return h;
            }

            return std::noop_coroutine();
        }

        result_type await_resume() {
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

    template <typename T, typename E>
    static void extract_error(async_node* child, task_group_node* group_ptr) {
        auto* g = static_cast<task_group*>(group_ptr);

        if(child->propagated_exception) {
#if KOTA_ENABLE_EXCEPTIONS
            g->exceptions.push_back(child->propagated_exception);
#endif
            return;
        }

        if constexpr(!std::is_void_v<E>) {
            auto* promise =
                static_cast<task_promise_object<T, E>*>(static_cast<standard_task*>(child));
            if(promise->value.has_value() && promise->value->has_error()) {
                if constexpr(std::is_same_v<E, error_type>) {
                    g->errors.push_back(std::move(*promise->value).error());
                } else {
                    g->errors.push_back(error_type(std::move(*promise->value).error()));
                }
            }
        }
    }

    KOTA_NO_UNIQUE_ADDRESS
    std::
        conditional_t<std::is_void_v<error_type>, std::type_identity<void>, std::vector<error_type>>
            errors;

#if KOTA_ENABLE_EXCEPTIONS
    std::vector<std::exception_ptr> exceptions;
#endif
};

task_group(event_loop&) -> task_group<>;

}  // namespace kota
