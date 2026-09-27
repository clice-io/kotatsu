#pragma once

#include <cassert>
#include <coroutine>
#include <source_location>
#include <type_traits>
#include <utility>

#include "../libuv.h"
#include "kota/async/runtime/node.h"
#include "kota/async/vocab/error.h"
#include "kota/async/vocab/outcome.h"
#include "kota/async/vocab/owned.h"

namespace kota::uv {

/// Base of every awaiter that waits for libuv. `D` supplies
///
///   bool start()    begins the operation; false when it ended without
///                   waiting, and await_resume() has its result already;
///   void cancel()   its task was cancelled while it waited: complete it now,
///                   or let the libuv callback that is still due complete it;
///   await_resume().
///
/// start() runs before the op is attached, so a task that is cancelled by
/// the time it awaits still hands libuv's work to cancel().
template <typename D>
struct uv_op : io_op {
    uv_op() noexcept {
        action = [](io_op* op) {
            static_cast<D*>(op)->cancel();
        };
    }

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        if(!static_cast<D*>(this)->start()) {
            return waiting;
        }
        return attach(waiting.promise(), location);
    }
};

/// Base of an op that submits one libuv request, whose callback reports a
/// status: `D::start()` submits `req` with on_done and keeps what libuv
/// returned with submitted(). libuv cannot take most requests back, so by
/// default a cancel leaves the request to end from its callback.
template <typename D, typename Req>
struct request_op : uv_op<D> {
    Req req = {};
    error result;

    request_op() noexcept {
        req.data = this;
    }

    /// Keeps the status of the submitting call; true if the request is on
    /// its way, false if libuv refused it.
    bool submitted(int status) noexcept {
        result = error(status);
        return !result;
    }

    void cancel() noexcept {}

    static void on_done(Req* req, int status) {
        auto* op = static_cast<D*>(static_cast<request_op*>(req->data));
        op->result = status_to_error(status);
        op->complete();
    }

    error await_resume() noexcept {
        return result;
    }
};

/// The one task a resource's libuv callbacks wake.
///
/// A resource keeps what arrives while nobody waits in its own way (bytes
/// in a buffer, a queue of datagrams, a count of fires); the slot hands
/// over what arrives while a task waits. It holds one waiter, from the wait
/// until the task resumes: a second wait meanwhile fails with
/// resource_busy_or_locked. Cancelling the waiter only withdraws it.
/// deliver() resumes it before returning; deliver_later() and abort() on a
/// later loop turn, which is how stop() and destruction end a pending wait
/// without resuming a task inside their caller.
template <typename T>
class waiter_slot {
public:
    /// What a wait ends with: an error alone when nothing else comes.
    using value_type = std::conditional_t<std::is_void_v<T>, error, result<T>>;

    struct awaiter : uv_op<awaiter> {
        /// The slot it waits on, until it resumes, is withdrawn or aborted;
        /// null when it never waited.
        waiter_slot* slot;
        /// What it resumes with: set up front by ready(), and by wait() to
        /// the error of a slot found taken; replaced by the slot.
        value_type value;

        awaiter(waiter_slot* slot, value_type value) noexcept :
            slot(slot), value(std::move(value)) {}

        bool start() noexcept {
            if(!slot || slot->waiter) {
                slot = nullptr;
                return false;
            }
            slot->waiter = this;
            return true;
        }

        void cancel() noexcept {
            // Aborted: its queued completion ends it.
            if(!slot) {
                return;
            }
            const bool woken = slot->unlink();
            slot = nullptr;
            // Woken: its queued completion ends it.
            if(!woken) {
                this->complete();
            }
        }

        value_type await_resume() noexcept {
            // Still linked when deliver_later() woke it.
            if(slot) {
                slot->unlink();
            }
            return std::move(value);
        }
    };

    /// Waits for the slot to be settled; fails at once with
    /// resource_busy_or_locked while another task waits.
    awaiter wait() noexcept {
        return awaiter(this, failure(error::resource_busy_or_locked));
    }

    /// Resumes at once with `value`, without waiting.
    static awaiter ready(value_type value) noexcept {
        return awaiter(nullptr, std::move(value));
    }

    /// Whether a task waits and nothing has woken it yet.
    bool waiting() const noexcept {
        return waiter && !woken;
    }

    /// Wakes the waiter with `value` before it returns.
    void deliver(value_type value) {
        assert(waiting() && "deliver() needs a waiting task");
        auto* resumed = waiter;
        unlink();
        resumed->slot = nullptr;
        resumed->value = std::move(value);
        resumed->complete();
    }

    /// Wakes the waiter with `value` on a later turn of `loop`.
    void deliver_later(uv_loop_t& loop, value_type value) {
        assert(waiting() && "deliver_later() needs a waiting task");
        woken = true;
        waiter->value = std::move(value);
        complete_later(loop, *waiter);
    }

    /// Ends the pending wait, if any, with `err` on a later turn of `loop`;
    /// that includes a wait deliver_later() woke that has not resumed yet.
    void abort(uv_loop_t& loop, error err) {
        if(!waiter) {
            return;
        }
        auto* ended = waiter;
        const bool queued = unlink();
        ended->slot = nullptr;
        ended->value = failure(err);
        if(!queued) {
            complete_later(loop, *ended);
        }
    }

private:
    static value_type failure(error err) noexcept {
        if constexpr(std::is_void_v<T>) {
            return err;
        } else {
            return outcome_error(err);
        }
    }

    /// Drops the waiter; returns whether a wake was already queued for it.
    bool unlink() noexcept {
        waiter = nullptr;
        return std::exchange(woken, false);
    }

    awaiter* waiter = nullptr;
    /// Whether deliver_later() has queued the waiter's wake.
    bool woken = false;
};

/// Base of a resource's state, `Derived`, which embeds its libuv handle as
/// `handle` (a uv_handle_t in a union with the concrete handle type) and
/// the waiter its callbacks wake as `slot`. Derived lives until libuv is
/// done with the handle.
template <typename Derived>
struct owned_handle {
    static unique_handle<Derived> make() {
        unique_handle<Derived> self(new Derived());
        self->handle.data = self.get();
        return self;
    }

    /// Ends a pending wait with operation_aborted, then closes the handle,
    /// whose close callback frees Derived. A handle that was never
    /// initialized goes at once, as does one the loop closed on its way out,
    /// which cleared its `data` (event_loop::~event_loop). One that loop is
    /// still closing, when a task the teardown resumed drops it, goes once
    /// the loop has closed.
    static void destroy(Derived* self) noexcept {
        auto& handle = self->handle;
        if(handle.loop == nullptr || handle.data == nullptr) {
            delete self;
            return;
        }
        self->slot.abort(*handle.loop, error::operation_aborted);
        if(::uv_is_closing(&handle)) {
            free_when_closed(*handle.loop, [self] { delete self; });
            return;
        }
        ::uv_close(&handle,
                   [](uv_handle_t* closed) { delete static_cast<Derived*>(closed->data); });
    }
};

}  // namespace kota::uv
