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
    error status;

    request_op() noexcept {
        req.data = this;
    }

    /// Keeps the status of the submitting call; true if the request is on
    /// its way, false if libuv refused it.
    bool submitted(int code) noexcept {
        status = error(code);
        return !status;
    }

    void cancel() noexcept {}

    static void on_done(Req* req, int code) {
        auto* op = static_cast<D*>(static_cast<request_op*>(req->data));
        op->status = status_to_error(code);
        op->complete();
    }

    error await_resume() noexcept {
        return status;
    }
};

/// The one task a resource's libuv callbacks wake.
///
/// A resource keeps what arrives while nobody waits in its own way (bytes
/// in a buffer, a queue of datagrams, a count of fires); the slot hands
/// over what arrives while a task waits. It holds one waiter, from the wait
/// until the task resumes: a second wait meanwhile fails with
/// resource_busy_or_locked. Cancelling the waiter only withdraws it.
///
/// deliver() resumes the waiter before it returns. deliver_later() and
/// abort() resume it on a later loop turn instead, which is how stop() and
/// destruction end a pending wait without resuming a task inside their
/// caller. They go through the queue the loop completes io ops from, the one
/// yield() uses, rather than the one of waits sync primitives granted: io
/// ops complete one way, and an abort needs no more than to stay out of its
/// caller.
template <typename T>
class waiter_slot {
public:
    /// What a wait ends with: an error alone when nothing else comes.
    using value_type = std::conditional_t<std::is_void_v<T>, error, result<T>>;

    struct awaiter : uv_op<awaiter> {
        /// The slot it waits on, until it resumes or the slot lets it go;
        /// null when it never waited.
        waiter_slot* slot;
        /// What it resumes with: set up front by ready(), and by wait() to
        /// the error of a slot found taken; replaced by the slot.
        value_type value;
        /// deliver_later() or abort() has queued its completion.
        bool queued = false;

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
            leave();
            // Otherwise its queued completion ends it.
            if(!queued) {
                this->complete();
            }
        }

        value_type await_resume() noexcept {
            // Still in the slot when deliver_later() woke it.
            leave();
            return std::move(value);
        }

        void leave() noexcept {
            if(slot) {
                slot->waiter = nullptr;
                slot = nullptr;
            }
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

    /// Whether a task waits, woken already or not.
    bool taken() const noexcept {
        return waiter != nullptr;
    }

    /// Whether a task waits and nothing has woken it yet.
    bool waiting() const noexcept {
        return waiter && !waiter->queued;
    }

    /// Wakes the waiter with `value` before it returns.
    void deliver(value_type value) {
        assert(waiting() && "deliver() needs a waiting task");
        auto* resumed = waiter;
        resumed->leave();
        resumed->value = std::move(value);
        resumed->complete();
    }

    /// Wakes the waiter on a later turn of `loop`, with no error: what it
    /// waits for is kept by the resource. The waiter holds the slot until it
    /// resumes.
    void deliver_later(uv_loop_t& loop)
        requires std::is_void_v<T> {
        assert(waiting() && "deliver_later() needs a waiting task");
        waiter->value = {};
        waiter->queued = true;
        complete_later(loop, *waiter);
    }

    /// Ends the pending wait, if any, with operation_aborted on a later turn
    /// of `loop`; that includes a wait deliver_later() woke that has not
    /// resumed yet.
    void abort(uv_loop_t& loop) {
        if(!waiter) {
            return;
        }
        auto* ended = waiter;
        ended->leave();
        ended->value = failure(error::operation_aborted);
        if(!std::exchange(ended->queued, true)) {
            complete_later(loop, *ended);
        }
    }

    /// Lets the pending wait, if any, go without ending it, for a loop being
    /// destroyed, which completes nothing queued any more: the wait's cancel
    /// ends it.
    void detach() noexcept {
        if(waiter) {
            waiter->queued = false;
            waiter->leave();
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

    awaiter* waiter = nullptr;
};

/// Base of a resource's state, `Derived`, which embeds its libuv handle as
/// `handle` (a uv_handle_t in a union with the concrete handle type) and
/// the waiter its callbacks wake as `slot`. Derived lives until libuv is
/// done with the handle.
template <typename Derived>
struct owned_handle {
    static detail::unique_handle<Derived> make() {
        detail::unique_handle<Derived> self(new Derived());
        self->handle.data = self.get();
        return self;
    }

    /// After an init of `handle` failed: libuv may have listed the handle on
    /// its loop and taken it off again, as uv_spawn on Unix and uv_tty_init
    /// on macOS do on some failures. Such a handle must not be closed, so
    /// destroy() frees it at once, as one the init never listed.
    static void forget_if_unlisted(Derived& self) noexcept {
        if(self.handle.loop == nullptr) {
            return;
        }

        struct search {
            const uv_handle_t* target;
            bool found = false;
        } walk{&self.handle};

        ::uv_walk(
            self.handle.loop,
            [](uv_handle_t* handle, void* arg) {
                auto& seen = *static_cast<search*>(arg);
                seen.found = seen.found || handle == seen.target;
            },
            &walk);
        if(!walk.found) {
            self.handle.loop = nullptr;
        }
    }

    /// Ends a pending wait with operation_aborted, then closes the handle,
    /// whose close callback frees Derived.
    static void destroy(Derived* self) noexcept {
        auto& handle = self->handle;
        if(handle.loop != nullptr && handle.data != nullptr && !::uv_is_closing(&handle)) {
            self->slot.abort(*handle.loop);
            ::uv_close(&handle,
                       [](uv_handle_t* closed) { delete static_cast<Derived*>(closed->data); });
            return;
        }
        // Never initialized, or taken off its loop by a failed init. Otherwise
        // the loop is being destroyed and has closed the handle, clearing its
        // `data` (event_loop::~event_loop), or closes it still, when a task
        // that teardown resumed drops it; that loop completes nothing queued
        // any more.
        self->slot.detach();
        if(handle.loop != nullptr && handle.data != nullptr) {
            free_when_closed(*handle.loop, [self] { delete self; });
        } else {
            delete self;
        }
    }
};

}  // namespace kota::uv
