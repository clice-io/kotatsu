#pragma once

#include <cassert>
#include <cstddef>
#include <source_location>

#include "frame.h"
#include "task.h"

namespace eventide {

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

        void await_resume() noexcept {}

    private:
        event* owner = nullptr;
    };

    wait_awaiter wait() noexcept {
        return wait_awaiter(*this);
    }

    void set() noexcept {
        signaled = true;
        drain_waiters([this](waiter_link* waiter) { resume_waiter(waiter); });
    }

    void reset() noexcept {
        signaled = false;
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
        drain_waiters([this](waiter_link* waiter) { resume_waiter(waiter); });
    }
};

}  // namespace eventide
