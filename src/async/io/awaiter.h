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
/// status: `D::start()` points `req.data` at the op and submits `req` with
/// on_done, keeping what libuv returned with submitted().
template <typename D, typename Req>
struct request_op : uv_op<D> {
    Req req = {};
    error result;

    /// Keeps the status of the submitting call; true if the request is on
    /// its way, false if libuv refused it.
    bool submitted(int status) noexcept {
        result = error(status);
        return !result;
    }

    static void on_done(Req* req, int status) {
        auto* op = static_cast<D*>(req->data);
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
/// over what arrives while a task waits. It holds one waiter: a second
/// wait fails with resource_busy_or_locked. Cancelling the waiter only
/// withdraws it, and abort() ends it on a later loop turn, which is how
/// stop() and destruction end a pending wait without resuming a task
/// inside their caller.
template <typename T>
class waiter_slot {
public:
    /// What a wait ends with: an error alone when nothing else comes.
    using value_type = std::conditional_t<std::is_void_v<T>, error, result<T>>;

    struct awaiter : uv_op<awaiter> {
        /// The slot it waits on; null once it no longer waits there, or
        /// when it never had to (ready()).
        waiter_slot* slot;
        /// What it resumes with: set up front by ready(), and by wait() to
        /// the error of a slot found taken; replaced by deliver() or abort().
        value_type value;

        awaiter(waiter_slot* slot, value_type value) noexcept :
            slot(slot), value(std::move(value)) {}

        bool start() noexcept {
            if(!slot || slot->waiter) {
                return false;
            }
            slot->waiter = this;
            return true;
        }

        void cancel() noexcept {
            // abort() unlinked it already: its queued completion ends it.
            if(!slot) {
                return;
            }
            slot->waiter = nullptr;
            slot = nullptr;
            this->complete();
        }

        value_type await_resume() noexcept {
            return std::move(value);
        }
    };

    /// Waits for deliver() or abort(); fails at once with
    /// resource_busy_or_locked while another task waits.
    awaiter wait() noexcept {
        return awaiter(this, failure(error::resource_busy_or_locked));
    }

    /// Resumes at once with `value`, without waiting.
    static awaiter ready(value_type value) noexcept {
        return awaiter(nullptr, std::move(value));
    }

    bool waiting() const noexcept {
        return waiter != nullptr;
    }

    /// Wakes the waiter with `value` before it returns.
    void deliver(value_type value) {
        assert(waiter && "deliver() needs a waiting task");
        auto* woken = std::exchange(waiter, nullptr);
        woken->slot = nullptr;
        woken->value = std::move(value);
        woken->complete();
    }

    /// Ends the pending wait, if any, with `err` on a later turn of `loop`.
    void abort(uv_loop_t& loop, error err) {
        if(!waiter) {
            return;
        }
        auto* ended = std::exchange(waiter, nullptr);
        ended->slot = nullptr;
        ended->value = failure(err);
        complete_later(loop, *ended);
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
    static unique_handle<Derived> make() {
        unique_handle<Derived> self(new Derived());
        self->handle.data = self.get();
        return self;
    }

    /// Ends a pending wait with operation_aborted, then closes the handle,
    /// whose close callback frees Derived. A handle that was never
    /// initialized goes at once, as does one the loop closed on its way out,
    /// which cleared its `data` (event_loop::~event_loop).
    static void destroy(Derived* self) noexcept {
        auto& handle = self->handle;
        if(handle.loop == nullptr || handle.data == nullptr) {
            delete self;
            return;
        }
        self->slot.abort(*handle.loop, error::operation_aborted);
        ::uv_close(&handle,
                   [](uv_handle_t* closed) { delete static_cast<Derived*>(closed->data); });
    }
};

}  // namespace kota::uv
