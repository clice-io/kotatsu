#pragma once

#include <cassert>
#include <cstddef>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/config.h"
#include "kota/support/type_list.h"
#include "kota/async/io/loop.h"
#include "kota/async/runtime/sync.h"
#include "kota/async/runtime/task.h"
#include "kota/async/vocab/cancellation.h"
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
        std::conditional_t<std::is_void_v<error_type>, void, outcome<void, error_type, void>>;

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
        done.reset();
        loop.schedule(monitor(with_token(std::move(t), cancel_source.token())));
    }

    task<result_type> join() {
        if(active > 0) {
            auto wait_result = co_await done.wait().catch_cancel();
            if(!wait_result.has_value()) {
                cancel_source.cancel();
                if(active > 0) {
                    co_await done.wait();
                }
                co_await cancel();
            }
        }

#if KOTA_ENABLE_EXCEPTIONS
        if(exception) {
            std::rethrow_exception(exception);
        }
#endif

        if constexpr(!std::is_void_v<error_type>) {
            if(has_error) {
                co_return result_type(outcome_error(std::move(*first_error)));
            }
            co_return result_type();
        }
    }

    void cancel_all() {
        cancel_source.cancel();
    }

private:
    template <typename T, typename E, typename C>
    task<> monitor(task<T, E, C> child) {
        KOTA_TRY {
            auto result = co_await std::move(child).catch_cancel();

            if constexpr(!std::is_void_v<E>) {
                if(result.has_error() && !has_error) {
                    has_error = true;
                    if constexpr(std::is_same_v<E, error_type>) {
                        first_error.emplace(std::move(result).error());
                    } else {
                        first_error.emplace(error_type(std::move(result).error()));
                    }
                }
            }
        }
        KOTA_CATCH_ALL() {
#if KOTA_ENABLE_EXCEPTIONS
            if(!exception) {
                exception = std::current_exception();
            }
#endif
        }

        if(--active == 0) {
            done.set();
        }
    }

    event_loop& loop;
    event done{true};
    cancellation_source cancel_source;
    std::size_t active = 0;

    bool has_error = false;
    using stored_error_type = std::conditional_t<std::is_void_v<error_type>, int, error_type>;
    std::optional<stored_error_type> first_error;

    std::exception_ptr exception;
};

task_group(event_loop&) -> task_group<>;

}  // namespace kota
