#pragma once

#include <cassert>
#include <deque>
#include <optional>
#include <utility>

#include "libuv.h"
#include "ringbuffer.h"
#include "eventide/async/error.h"
#include "eventide/async/frame.h"
#include "eventide/async/stream.h"

namespace eventide::uv {

template <typename StatusT>
inline bool is_cancelled_status(StatusT status) noexcept {
    return static_cast<long long>(status) == static_cast<long long>(UV_ECANCELED);
}

struct single_waiter {
    system_op* waiter = nullptr;

    bool has_waiter() const noexcept {
        return waiter != nullptr;
    }

    void arm(system_op& op) noexcept {
        waiter = &op;
    }

    void disarm() noexcept {
        waiter = nullptr;
    }

    template <typename StatusT>
    bool mark_cancelled_if(StatusT status) noexcept {
        if(waiter == nullptr || !is_cancelled_status(status)) {
            return false;
        }

        waiter->state = async_node::Cancelled;
        return true;
    }
};

template <typename ResultT>
struct waiter_binding : single_waiter {
    ResultT* active = nullptr;

    void arm(system_op& op, ResultT& slot) noexcept {
        this->waiter = &op;
        active = &slot;
    }

    void disarm() noexcept {
        single_waiter::disarm();
        active = nullptr;
    }

    bool try_deliver(ResultT&& value) {
        if(this->waiter == nullptr || active == nullptr) {
            return false;
        }

        *active = std::move(value);
        auto* w = this->waiter;
        disarm();
        w->complete();
        return true;
    }
};

template <typename Derived, async_node::NodeKind Kind = async_node::NodeKind::SystemIO>
struct await_op : system_op {
    await_op() : system_op(Kind) {
        this->action = &Derived::on_cancel;
    }

    template <typename CleanupFn>
    static void complete_cancel(system_op* op, CleanupFn&& cleanup) noexcept {
        auto* aw = static_cast<Derived*>(op);
        if(aw == nullptr) {
            return;
        }

        cleanup(*aw);
        aw->complete();
    }

    template <typename StatusT>
    bool mark_cancelled_if(StatusT status) noexcept {
        if(!is_cancelled_status(status)) {
            return false;
        }

        this->state = async_node::Cancelled;
        return true;
    }
};

template <typename ResultT>
struct queued_delivery : waiter_binding<ResultT> {
    std::deque<ResultT> pending;

    bool has_pending() const noexcept {
        return !pending.empty();
    }

    ResultT take_pending() {
        assert(!pending.empty() && "take_pending requires queued value");
        auto out = std::move(pending.front());
        pending.pop_front();
        return out;
    }

    void deliver(ResultT&& value) {
        if(!this->try_deliver(std::move(value))) {
            pending.push_back(std::move(value));
        }
    }

    void deliver(error err)
        requires requires { ResultT(std::unexpected(err)); }
    {
        deliver(ResultT(std::unexpected(err)));
    }
};

template <typename ResultT>
struct stored_delivery : waiter_binding<ResultT> {
    std::optional<ResultT> pending;

    bool has_pending() const noexcept {
        return pending.has_value();
    }

    ResultT take_pending() {
        assert(pending.has_value() && "take_pending requires stored value");
        auto out = std::move(*pending);
        pending.reset();
        return out;
    }

    void deliver(ResultT&& value) {
        if(!this->try_deliver(std::move(value))) {
            pending = std::move(value);
        }
    }

    void deliver(error err)
        requires requires { ResultT(std::unexpected(err)); }
    {
        deliver(ResultT(std::unexpected(err)));
    }
};

template <typename ResultT>
struct latched_delivery : waiter_binding<ResultT> {
    std::optional<ResultT> pending;

    bool has_pending() const noexcept {
        return pending.has_value();
    }

    const ResultT& peek_pending() const noexcept {
        assert(pending.has_value() && "peek_pending requires latched value");
        return *pending;
    }

    void deliver(ResultT value) {
        pending = value;
        this->try_deliver(std::move(value));
    }

    void deliver(error err)
        requires requires { ResultT(std::unexpected(err)); }
    {
        deliver(ResultT(std::unexpected(err)));
    }
};

template <typename ValueT>
struct latest_value_delivery : waiter_binding<result<ValueT>> {
    std::optional<result<ValueT>> pending;

    bool has_pending() const noexcept {
        return pending.has_value();
    }

    result<ValueT> take_pending() {
        assert(pending.has_value() && "take_pending requires stored value");
        result<ValueT> out(std::move(*pending));
        pending.reset();
        return out;
    }

    void deliver(result<ValueT>&& value) {
        if(this->waiter != nullptr && this->active != nullptr) {
            this->try_deliver(std::move(value));
            return;
        }

        pending = std::move(value);
    }

    void deliver(error err) {
        deliver(result<ValueT>(std::unexpected(err)));
    }
};

}  // namespace eventide::uv

namespace eventide {

struct stream_handle {
    union {
        uv_handle_t handle;
        uv_stream_t stream;
        uv_pipe_t pipe;
        uv_tcp_t tcp;
        uv_tty_t tty;
    };
};

struct stream::Self : uv_handle<stream::Self, uv_stream_t>, stream_handle {
    enum class read_mode { none, buffered, direct };

    uv::single_waiter reader;
    uv::single_waiter writer;
    ring_buffer buffer{};
    error error_code{};
    read_mode active_read_mode = read_mode::none;
};

template <typename Stream>
struct acceptor<Stream>::Self :
    uv_handle<acceptor<Stream>::Self, uv_stream_t>,
    stream_handle,
    uv::queued_delivery<result<Stream>> {
    int pipe_ipc = 0;
};

}  // namespace eventide
