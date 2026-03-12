#pragma once

#include <cassert>
#include <cstddef>
#include <source_location>

#include "frame.h"
#include "task.h"

namespace eventide {

/// Shared base for synchronization resources. These are not awaitable runtime
/// nodes; waiter_link sub-objects bridge tasks into the wait queue.
class sync_primitive {
public:
    enum class Kind : std::uint8_t {
        Mutex,
        Event,
        Semaphore,
        ConditionVariable,
    };

    friend class async_node;

    explicit sync_primitive(Kind k) : kind(k) {}

    const Kind kind;

    std::source_location location;

    /// Appends a waiter to the end of the wait queue.
    void insert(waiter_link* link);

    /// Removes a waiter from the wait queue.
    void remove(waiter_link* link);

protected:
    bool has_waiters() const noexcept {
        return head != nullptr;
    }

    waiter_link* pop_waiter() noexcept {
        auto* link = head;
        if(link) {
            remove(link);
        }
        return link;
    }

    bool front_waiter_matches(std::size_t snapshot) const noexcept {
        return head && head->generation == snapshot;
    }

    bool resume_waiter(waiter_link* link) noexcept {
        if(!link) {
            return false;
        }
        auto* awaiting = link->awaiter;
        link->awaiter = nullptr;
        if(!awaiting || awaiting->is_cancelled()) {
            return false;
        }
        awaiting->clear_awaitee();
        awaiting->resume();
        return true;
    }

    bool cancel_waiter(waiter_link* link) noexcept;

    /// Starts a logical "snapshot" of the current wait queue.
    ///
    /// We do not materialize that snapshot into a temporary container: doing so
    /// would keep raw waiter pointers alive across synchronous resumes, and one
    /// resumed waiter is allowed to cancel/destroy sibling waiters immediately.
    /// Instead, each queued waiter is tagged with the current generation. An
    /// interrupt bumps the generation once, then keeps popping from the front
    /// only while the head still belongs to the old generation.
    std::size_t begin_waiter_snapshot() noexcept {
        auto snapshot = waiter_generation;
        waiter_generation += 1;
        return snapshot;
    }

private:
    /// Head and tail of the intrusive doubly-linked waiter queue.
    waiter_link* head = nullptr;
    waiter_link* tail = nullptr;

    /// Monotonic tag used to distinguish "already queued" waiters from
    /// re-entrantly added waiters during interrupt().
    std::size_t waiter_generation = 0;
};

class mutex : public sync_primitive {
public:
    mutex(std::source_location location = std::source_location::current()) :
        sync_primitive(sync_primitive::Kind::Mutex) {
        this->location = location;
    }

    mutex(const mutex&) = delete;
    mutex& operator=(const mutex&) = delete;

    struct lock_awaiter : waiter_link {
        explicit lock_awaiter(mutex& owner) :
            waiter_link(async_node::NodeKind::MutexWaiter), owner(&owner) {}

        bool await_ready() noexcept {
            return owner->try_lock();
        }

        template <typename Promise>
        auto await_suspend(
            std::coroutine_handle<Promise> awaiter,
            std::source_location location = std::source_location::current()) noexcept {
            owner->insert(this);
            return link_continuation(&awaiter.promise(), location);
        }

        void await_resume() noexcept {}

    private:
        mutex* owner = nullptr;
    };

    lock_awaiter lock() noexcept {
        return lock_awaiter(*this);
    }

    bool try_lock() noexcept {
        if(locked) {
            return false;
        }
        locked = true;
        return true;
    }

    void unlock() noexcept {
        assert(locked && "mutex::unlock without lock");
        while(auto* waiter = pop_waiter()) {
            if(resume_waiter(waiter)) {
                return;
            }
        }
        locked = false;
    }

private:
    bool locked = false;
};

class semaphore : public sync_primitive {
public:
    explicit semaphore(std::ptrdiff_t initial = 0,
                       std::source_location location = std::source_location::current()) :
        sync_primitive(sync_primitive::Kind::Semaphore) {
        assert(initial >= 0 && "semaphore initial count must be non-negative");
        this->location = location;
        count = initial;
    }

    semaphore(const semaphore&) = delete;
    semaphore& operator=(const semaphore&) = delete;

    struct acquire_awaiter : waiter_link {
        /// Reuses EventWaiter kind — all waiter_link subtypes share identical
        /// cancel/link_continuation/final_transition logic, so a dedicated
        /// SemaphoreWaiter kind is unnecessary.
        explicit acquire_awaiter(semaphore& owner) :
            waiter_link(async_node::NodeKind::EventWaiter), owner(&owner) {}

        bool await_ready() noexcept {
            return owner->try_acquire();
        }

        template <typename Promise>
        auto await_suspend(
            std::coroutine_handle<Promise> awaiter,
            std::source_location location = std::source_location::current()) noexcept {
            owner->insert(this);
            return link_continuation(&awaiter.promise(), location);
        }

        void await_resume() noexcept {}

    private:
        semaphore* owner = nullptr;
    };

    acquire_awaiter acquire() noexcept {
        return acquire_awaiter(*this);
    }

    bool try_acquire() noexcept {
        if(count <= 0) {
            return false;
        }
        count -= 1;
        return true;
    }

    void release(std::ptrdiff_t n = 1) {
        assert(n >= 0 && "semaphore::release count must be non-negative");
        for(std::ptrdiff_t i = 0; i < n; ++i) {
            if(has_waiters()) {
                while(auto* waiter = pop_waiter()) {
                    if(resume_waiter(waiter)) {
                        break;
                    }
                }
            } else {
                count += 1;
            }
        }
    }

private:
    std::ptrdiff_t count = 0;
};

class event : public sync_primitive {
public:
    explicit event(bool signaled = false,
                   std::source_location location = std::source_location::current()) :
        sync_primitive(sync_primitive::Kind::Event), signaled(signaled) {
        this->location = location;
    }

    event(const event&) = delete;
    event& operator=(const event&) = delete;

    struct wait_awaiter : waiter_link {
        explicit wait_awaiter(event& owner) :
            waiter_link(async_node::NodeKind::EventWaiter), owner(&owner) {}

        bool await_ready() noexcept {
            return owner->is_set();
        }

        template <typename Promise>
        auto await_suspend(
            std::coroutine_handle<Promise> awaiter,
            std::source_location location = std::source_location::current()) noexcept {
            owner->insert(this);
            return link_continuation(&awaiter.promise(), location);
        }

        outcome<void, void, cancellation> await_resume() noexcept {
            if(this->state == async_node::Cancelled) {
                return detail::cancel_box<cancellation>{cancellation{}};
            }

            return {};
        }

    private:
        event* owner = nullptr;
    };

    /// Waits until the event is set. If the current wait queue is interrupted,
    /// cancellation is propagated through the returned task.
    task<> wait() {
        auto result = co_await wait_awaiter(*this);
        if(result.is_cancelled()) {
            co_await cancel();
        }
    }

    void set() noexcept {
        signaled = true;
        const auto snapshot = begin_waiter_snapshot();
        while(front_waiter_matches(snapshot)) {
            resume_waiter(pop_waiter());
        }
    }

    void reset() noexcept {
        signaled = false;
    }

    /// Interrupts the current wait queue without changing the signaled state.
    void interrupt() noexcept {
        const auto snapshot = begin_waiter_snapshot();

        // cancel_waiter() resumes user code synchronously. That code may call
        // ev.wait() again before we come back here, so a plain
        // `while(pop_waiter())` would also drain those newcomers. By stopping
        // once the front waiter no longer belongs to the snapshot generation,
        // interrupt() only cancels the queue as it existed at entry.
        while(front_waiter_matches(snapshot)) {
            cancel_waiter(pop_waiter());
        }
    }

    bool is_set() const noexcept {
        return signaled;
    }

private:
    bool signaled = false;
};

class condition_variable : public sync_primitive {
public:
    condition_variable(std::source_location location = std::source_location::current()) :
        sync_primitive(sync_primitive::Kind::ConditionVariable) {
        this->location = location;
    }

    condition_variable(const condition_variable&) = delete;
    condition_variable& operator=(const condition_variable&) = delete;

    struct wait_awaiter : waiter_link {
        /// Reuses EventWaiter kind — see semaphore::acquire_awaiter comment.
        explicit wait_awaiter(condition_variable& owner) :
            waiter_link(async_node::NodeKind::EventWaiter), owner(&owner) {}

        bool await_ready() const noexcept {
            return false;
        }

        template <typename Promise>
        auto await_suspend(
            std::coroutine_handle<Promise> awaiter,
            std::source_location location = std::source_location::current()) noexcept {
            owner->insert(this);
            return link_continuation(&awaiter.promise(), location);
        }

        void await_resume() noexcept {}

    private:
        condition_variable* owner = nullptr;
    };

    /// Atomically unlocks `m`, waits for a notification, then re-locks `m`.
    ///
    /// Cancellation note: if this task is cancelled while suspended on the
    /// wait_awaiter, the mutex will NOT be re-acquired (co_await m.lock() is
    /// never reached). In normal usage this is safe because cancellation
    /// propagates upward — the caller is also cancelled and never observes
    /// the unlocked mutex. However, if cancellation is intercepted externally
    /// (e.g., catch_cancel() or with_token() wrapping a cv.wait() call), the
    /// caller resumes with the mutex NOT held. Avoid intercepting cancellation
    /// around cv.wait().
    task<> wait(mutex& m) {
        m.unlock();
        co_await wait_awaiter(*this);
        co_await m.lock();
    }

    void notify_one() {
        while(auto* waiter = pop_waiter()) {
            if(resume_waiter(waiter)) {
                break;
            }
        }
    }

    void notify_all() {
        const auto snapshot = begin_waiter_snapshot();
        while(front_waiter_matches(snapshot)) {
            resume_waiter(pop_waiter());
        }
    }
};

}  // namespace eventide
