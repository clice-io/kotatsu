#pragma once

#include <cassert>
#include <cstddef>
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "kota/support/config.h"
#include "kota/support/type_list.h"
#include "kota/async/io/loop.h"
#include "kota/async/runtime/sync.h"
#include "kota/async/runtime/task.h"
#include "kota/async/vocab/outcome.h"

namespace kota {

namespace detail {

template <typename List>
struct tg_type_aggregate;

template <>
struct tg_type_aggregate<type_list<>> {
    using type = void;
};

template <typename T>
struct tg_type_aggregate<type_list<T>> {
    using type = T;
};

template <typename... Ts>
    requires (sizeof...(Ts) > 1)
struct tg_type_aggregate<type_list<Ts...>> {
    using type = std::variant<Ts...>;
};

template <typename... Ts>
using tg_error_type_t = typename tg_type_aggregate<type_list<Ts...>>::type;

}  // namespace detail

template <typename... Errors>
class task_group {
public:
    using error_type = detail::tg_error_type_t<Errors...>;
    using result_type =
        std::conditional_t<std::is_void_v<error_type>, void,
                           outcome<void, std::vector<error_type>, void>>;

    explicit task_group(event_loop& loop) : loop(loop) {}

    task_group(const task_group&) = delete;
    task_group& operator=(const task_group&) = delete;
    task_group(task_group&&) = delete;
    task_group& operator=(task_group&&) = delete;

    ~task_group() {
        assert(active == 0 && "task_group destroyed with active tasks");
    }

    template <typename T, typename E, typename C>
        requires std::is_void_v<E> || is_one_of<E, Errors...>
    void spawn(task<T, E, C>&& t) {
        ++active;
        done->reset();
        std::size_t index = children.size();
        auto m = monitor(std::move(t), index);
        children.push_back(m.operator->());
        loop.schedule(std::move(m));
    }

    task<result_type> join() {
        if(active > 0) {
            auto wait_result = co_await done->wait().catch_cancel();
            if(!wait_result.has_value()) {
                cancel();
                if(active > 0) {
                    co_await done->wait();
                }
                co_await kota::cancel();
            }
        }

#if KOTA_ENABLE_EXCEPTIONS
        if(!exceptions.empty()) {
            std::rethrow_exception(exceptions.front());
        }
#endif

        if constexpr(!std::is_void_v<error_type>) {
            if(!errors.empty()) {
                co_return result_type(outcome_error(std::move(errors)));
            }
            co_return result_type();
        }
    }

    void cancel() {
        cancelled = true;
        for(auto* node: children) {
            if(node) {
                auto* t = static_cast<standard_task*>(node);
                if(t->has_awaitee()) {
                    node->cancel();
                }
            }
        }
    }

private:
    template <typename T, typename E, typename C>
    task<> monitor(task<T, E, C> child, std::size_t index) {
        if(!cancelled) {
            KOTA_TRY {
                auto result = co_await std::move(child).catch_cancel();

                if constexpr(!std::is_void_v<E>) {
                    if(result.has_error()) {
                        if constexpr(std::is_same_v<E, error_type>) {
                            errors.push_back(std::move(result).error());
                        } else {
                            errors.push_back(error_type(std::move(result).error()));
                        }
                        cancel();
                    }
                }
            }
            KOTA_CATCH_ALL() {
#if KOTA_ENABLE_EXCEPTIONS
                exceptions.push_back(std::current_exception());
                cancel();
#endif
            }
        }

        children[index] = nullptr;
        if(--active == 0) {
            auto d = done;
            d->set();
        }
    }

    event_loop& loop;
    std::shared_ptr<event> done = std::make_shared<event>(true);
    std::size_t active = 0;
    bool cancelled = false;
    std::vector<async_node*> children;

    using stored_error_type = std::conditional_t<std::is_void_v<error_type>, int, error_type>;
    std::vector<stored_error_type> errors;

    std::vector<std::exception_ptr> exceptions;
};

task_group(event_loop&) -> task_group<>;

}  // namespace kota
