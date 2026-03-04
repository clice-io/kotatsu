#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "task.h"

namespace eventide {

class cancellation_token;

namespace detail {

template <typename T>
task<std::expected<T, cancellation>> with_token_impl(cancellation_token token,
                                                     task<std::expected<T, cancellation>> child);

struct cancellation_watch_flag {
    bool cancelled = false;
};

class cancellation_state : public std::enable_shared_from_this<cancellation_state> {
public:
    struct watcher_entry {
        std::size_t id = 0;
        async_node* node = nullptr;
        std::weak_ptr<cancellation_watch_flag> flag;
    };

    bool cancelled() const noexcept {
        return cancelled_state;
    }

    void cancel() noexcept {
        if(cancelled_state) {
            return;
        }

        auto keepalive = this->shared_from_this();
        cancelled_state = true;
        for(auto& watcher: watchers) {
            if(watcher.id == 0) {
                continue;
            }

            if(auto flag = watcher.flag.lock()) {
                flag->cancelled = true;
            }

            if(watcher.node) {
                watcher.node->cancel();
            }
        }

        watchers.clear();
    }

    std::size_t subscribe(async_node* node,
                          const std::shared_ptr<cancellation_watch_flag>& flag) noexcept {
        if(flag) {
            flag->cancelled = cancelled_state;
        }

        if(cancelled_state) {
            if(node) {
                node->cancel();
            }
            return 0;
        }

        auto id = next_id++;
        if(next_id == 0) {
            next_id = 1;
        }

        watchers.push_back(watcher_entry{
            .id = id,
            .node = node,
            .flag = flag,
        });
        return id;
    }

    void unsubscribe(std::size_t id) noexcept {
        if(id == 0 || watchers.empty()) {
            return;
        }

        for(auto& watcher: watchers) {
            if(watcher.id == id) {
                watcher.id = 0;
                watcher.node = nullptr;
                watcher.flag.reset();
                break;
            }
        }

        if(!cancelled_state) {
            compact();
        }
    }

private:
    void compact() noexcept {
        watchers.erase(std::remove_if(watchers.begin(),
                                      watchers.end(),
                                      [](const watcher_entry& watcher) { return watcher.id == 0; }),
                       watchers.end());
    }

private:
    std::vector<watcher_entry> watchers;
    std::size_t next_id = 1;
    bool cancelled_state = false;
};

}  // namespace detail

class cancellation_token {
public:
    class registration {
    public:
        registration() = default;

        registration(const registration&) = delete;
        registration& operator=(const registration&) = delete;

        registration(registration&& other) noexcept :
            state(std::move(other.state)), registration_id(other.registration_id),
            watch_flag(std::move(other.watch_flag)) {
            other.registration_id = 0;
        }

        registration& operator=(registration&& other) noexcept {
            if(this == &other) {
                return *this;
            }

            unregister();
            state = std::move(other.state);
            registration_id = other.registration_id;
            watch_flag = std::move(other.watch_flag);
            other.registration_id = 0;
            return *this;
        }

        ~registration() {
            unregister();
        }

        void unregister() noexcept {
            if(state && registration_id != 0) {
                state->unsubscribe(registration_id);
            }

            registration_id = 0;
            state.reset();
        }

        bool cancelled() const noexcept {
            return watch_flag && watch_flag->cancelled;
        }

    private:
        friend class cancellation_token;

        registration(std::shared_ptr<detail::cancellation_state> state,
                     std::size_t id,
                     std::shared_ptr<detail::cancellation_watch_flag> flag) :
            state(std::move(state)), registration_id(id), watch_flag(std::move(flag)) {}

    private:
        std::shared_ptr<detail::cancellation_state> state;
        std::size_t registration_id = 0;
        std::shared_ptr<detail::cancellation_watch_flag> watch_flag;
    };

    cancellation_token() = default;

    bool cancelled() const noexcept {
        return state && state->cancelled();
    }

private:
    template <typename T>
    friend task<std::expected<T, cancellation>>
        detail::with_token_impl(cancellation_token token,
                                task<std::expected<T, cancellation>> child);

    registration register_task(async_node* node) const {
        auto flag = std::make_shared<detail::cancellation_watch_flag>();
        if(!state) {
            return registration(nullptr, 0, std::move(flag));
        }

        auto id = state->subscribe(node, flag);
        return registration(state, id, std::move(flag));
    }

private:
    friend class cancellation_source;

    explicit cancellation_token(std::shared_ptr<detail::cancellation_state> state) :
        state(std::move(state)) {}

private:
    std::shared_ptr<detail::cancellation_state> state;
};

class cancellation_source {
public:
    cancellation_source() : state(std::make_shared<detail::cancellation_state>()) {}

    cancellation_source(const cancellation_source&) = delete;
    cancellation_source& operator=(const cancellation_source&) = delete;

    cancellation_source(cancellation_source&&) noexcept = default;

    cancellation_source& operator=(cancellation_source&& other) noexcept {
        if(this == &other) {
            return *this;
        }

        cancel();
        state = std::move(other.state);
        return *this;
    }

    ~cancellation_source() {
        cancel();
    }

    void cancel() noexcept {
        if(state) {
            state->cancel();
        }
    }

    bool cancelled() const noexcept {
        return state && state->cancelled();
    }

    cancellation_token token() const noexcept {
        return cancellation_token(state);
    }

private:
    std::shared_ptr<detail::cancellation_state> state;
};

namespace detail {

template <typename T>
task<std::expected<T, cancellation>> with_token_impl(cancellation_token token,
                                                     task<std::expected<T, cancellation>> child) {
    if(token.cancelled()) {
        co_await cancel();
        std::abort();
    }

    child->policy = async_node::InterceptCancel;
    auto registration = token.register_task(child.operator->());
    auto result = co_await std::move(child);
    registration.unregister();

    if(!result.has_value()) {
        co_await cancel();
        std::abort();
    }

    if constexpr(std::is_void_v<T>) {
        co_return;
    } else {
        co_return std::move(*result);
    }
}

}  // namespace detail

template <typename T>
    requires (!is_cancellation_t<T>)
task<std::expected<T, cancellation>> with_token(cancellation_token token, task<T>&& task) {
    auto child = std::move(task).catch_cancel();
    auto wrapped = detail::with_token_impl<T>(std::move(token), std::move(child));
    wrapped->policy = async_node::InterceptCancel;
    return wrapped;
}

template <typename T>
task<std::expected<T, cancellation>> with_token(cancellation_token token,
                                                task<std::expected<T, cancellation>>&& task) {
    auto wrapped = detail::with_token_impl<T>(std::move(token), std::move(task));
    wrapped->policy = async_node::InterceptCancel;
    return wrapped;
}

}  // namespace eventide
