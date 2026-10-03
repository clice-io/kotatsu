#pragma once

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <utility>

#include "kota/async/runtime/node.h"

namespace kota {

class event_loop;
class mutex;
class condition_variable;

template <typename Primitive>
class scoped_wait;

/// Base of the sync primitives. Tasks wait on one through a wait_node, in a
/// FIFO queue, and a primitive never resumes a task it grants inline: the task
/// resumes once whatever runs has suspended. A mutex, semaphore or condition
/// variable must not go while tasks wait on it, nor before the tasks it woke
/// have resumed: one cancelled in between hands what it was granted back to
/// it. An event may go once it is set. A waiting task resumes on its event
/// loop, so a primitive must not grant a wait whose loop has gone.
class sync_primitive {
public:
    enum class Kind : std::uint8_t {
        Mutex,
        Semaphore,
        Event,
        ConditionVariable,
    };

    const Kind kind;

    /// Where the primitive was created.
    const std::source_location location;

    sync_primitive(const sync_primitive&) = delete;
    sync_primitive& operator=(const sync_primitive&) = delete;

    /// Whether a task waits on the primitive.
    bool has_waiters() const noexcept {
        return head != nullptr;
    }

protected:
    sync_primitive(Kind kind, std::source_location location) noexcept :
        kind(kind), location(location) {}

    ~sync_primitive() {
        assert(head == nullptr && "sync primitive destroyed while tasks wait on it");
        assert(woken == 0 && "sync primitive destroyed before the tasks it woke resumed");
    }

    /// Grants the first waiter what it waits for; false when none waits.
    bool wake_one();

    void wake_all() {
        while(wake_one()) {}
    }

private:
    friend class wait_node;
    template <typename Derived>
    friend class async_visitor;

    void insert(wait_node& waiter) noexcept;

    void remove(wait_node& waiter) noexcept;

    wait_node* head = nullptr;
    wait_node* tail = nullptr;

    /// The waits it granted whose tasks have not resumed yet; an event's are
    /// not counted, as they need nothing more of it.
    std::size_t woken = 0;
};

/// A task's wait on a sync primitive: what lock(), acquire() and wait() give
/// to co_await. A waiter granted what it waits for and cancelled before its
/// task resumes hands the grant on as if the task had used it and let go: a
/// mutex to the next waiter, a semaphore unit likewise, a condition variable's
/// notification to the next waiter on it.
class wait_node : public async_node {
public:
    bool await_ready() noexcept;

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        return wait(waiting.promise(), location);
    }

    void await_resume() const noexcept {}

private:
    friend class async_node;
    friend class sync_primitive;
    friend class event_loop;
    friend class mutex;
    friend class semaphore;
    friend class event;
    friend class condition_variable;
    template <typename Primitive>
    friend class scoped_wait;
    template <typename Derived>
    friend class async_visitor;

    explicit wait_node(sync_primitive& owner) noexcept :
        async_node(NodeKind::Waiter), owner(&owner), owner_kind(owner.kind) {}

    /// A condition variable wait: it unlocks `relock` while it waits and locks
    /// it again before it ends, cancelled or not.
    wait_node(condition_variable& owner, mutex& relock) noexcept;

    std::coroutine_handle<> wait(task_frame& waiting, std::source_location location) noexcept;

    /// `from`, where the waiter was queued, grants it what it waits for.
    void grant(sync_primitive& from);

    void cancel_wait();

    /// Hands on what the waiter was granted, for a task that will not use it.
    void give_back();

    /// Resumes the task once the wait was granted, or ends it cancelled when a
    /// cancel reached the wait since. The event loop calls this.
    void resume();

    sync_primitive* owner;

    /// What `owner` is, known without it: an event may be gone before the
    /// task it woke is cancelled.
    sync_primitive::Kind owner_kind;

    /// The loop the task waits on, which resumes it once the wait is granted:
    /// a primitive may grant it where no loop runs.
    event_loop* loop = nullptr;

    /// The mutex a condition variable wait locks again.
    mutex* relock = nullptr;

    /// Where the waiter is queued; null once it has been granted.
    sync_primitive* queue = nullptr;

    wait_node* prev = nullptr;
    wait_node* next = nullptr;
};

/// What mutex::scoped_lock() and semaphore::scoped_acquire() give to
/// co_await: the wait of lock() or acquire(), which then gives a guard of what
/// it took. A wait that a cancel ends gives none, and keeps nothing.
template <typename Primitive>
class scoped_wait {
public:
    bool await_ready() noexcept {
        return wait.await_ready();
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        return wait.await_suspend(waiting, location);
    }

    typename Primitive::guard await_resume() noexcept {
        return typename Primitive::guard(owner);
    }

private:
    friend Primitive;

    explicit scoped_wait(Primitive& owner) noexcept : wait(owner), owner(owner) {}

    wait_node wait;
    Primitive& owner;
};

/// Mutual exclusion between tasks, handed from each unlock() to the first
/// task waiting in lock().
class mutex : public sync_primitive {
public:
    using lock_awaiter = wait_node;

    /// Holds the mutex scoped_lock() locked, and unlocks it when it goes,
    /// unless unlock() did before. A moved-from guard holds nothing.
    class [[nodiscard]] guard {
    public:
        guard(guard&& other) noexcept : held(std::exchange(other.held, nullptr)) {}

        guard& operator=(guard other) noexcept {
            std::swap(held, other.held);
            return *this;
        }

        ~guard() {
            if(held != nullptr) {
                held->unlock();
            }
        }

        /// Unlocks the mutex now.
        void unlock() noexcept {
            assert(held != nullptr && "mutex::guard::unlock without the mutex");
            std::exchange(held, nullptr)->unlock();
        }

    private:
        friend class scoped_wait<mutex>;

        explicit guard(mutex& locked) noexcept : held(&locked) {}

        mutex* held;
    };

    explicit mutex(std::source_location location = std::source_location::current()) noexcept :
        sync_primitive(Kind::Mutex, location) {}

    /// Locks the mutex; waits for it while another task holds it.
    lock_awaiter lock() noexcept {
        return lock_awaiter(*this);
    }

    /// Locks the mutex as lock() does, and gives a guard that unlocks it.
    scoped_wait<mutex> scoped_lock() noexcept {
        return scoped_wait<mutex>(*this);
    }

    /// Locks the mutex if it is free.
    bool try_lock() noexcept {
        if(locked) {
            return false;
        }
        locked = true;
        return true;
    }

    /// Unlocks the mutex, or hands it to the first waiter.
    void unlock() noexcept {
        assert(locked && "mutex::unlock without lock");
        if(!wake_one()) {
            locked = false;
        }
    }

private:
    bool locked = false;
};

/// A counting semaphore: release() adds units, acquire() takes one, and a
/// task waits in acquire() while there is none.
class semaphore : public sync_primitive {
public:
    using acquire_awaiter = wait_node;

    /// Holds the unit scoped_acquire() took, and releases it when it goes,
    /// unless release() did before. A moved-from guard holds nothing.
    class [[nodiscard]] guard {
    public:
        guard(guard&& other) noexcept : held(std::exchange(other.held, nullptr)) {}

        guard& operator=(guard other) noexcept {
            std::swap(held, other.held);
            return *this;
        }

        ~guard() {
            if(held != nullptr) {
                held->release();
            }
        }

        /// Releases the unit now.
        void release() {
            assert(held != nullptr && "semaphore::guard::release without a unit");
            std::exchange(held, nullptr)->release();
        }

    private:
        friend class scoped_wait<semaphore>;

        explicit guard(semaphore& acquired) noexcept : held(&acquired) {}

        semaphore* held;
    };

    explicit semaphore(std::ptrdiff_t initial = 0,
                       std::source_location location = std::source_location::current()) noexcept :
        sync_primitive(Kind::Semaphore, location), count(initial) {
        assert(initial >= 0 && "semaphore initial count must be non-negative");
    }

    /// Takes a unit; waits for one while there is none.
    acquire_awaiter acquire() noexcept {
        return acquire_awaiter(*this);
    }

    /// Takes a unit as acquire() does, and gives a guard that releases it.
    scoped_wait<semaphore> scoped_acquire() noexcept {
        return scoped_wait<semaphore>(*this);
    }

    /// Takes a unit if there is one.
    bool try_acquire() noexcept {
        if(count <= 0) {
            return false;
        }
        count -= 1;
        return true;
    }

    /// Adds `n` units, handing each to the first waiter while one waits.
    void release(std::ptrdiff_t n = 1) {
        assert(n >= 0 && "semaphore::release count must be non-negative");
        for(; n > 0; --n) {
            if(!wake_one()) {
                count += 1;
            }
        }
    }

private:
    std::ptrdiff_t count;
};

/// A flag tasks wait on until it is set; it stays set until reset().
class event : public sync_primitive {
public:
    using wait_awaiter = wait_node;

    explicit event(bool signaled = false,
                   std::source_location location = std::source_location::current()) noexcept :
        sync_primitive(Kind::Event, location), signaled(signaled) {}

    /// Waits until the event is set; does not suspend when it is.
    wait_awaiter wait() noexcept {
        return wait_awaiter(*this);
    }

    /// Sets the event and wakes every waiter.
    void set() noexcept {
        signaled = true;
        wake_all();
    }

    /// Clears the event; later waits wait for the next set().
    void reset() noexcept {
        signaled = false;
    }

    bool is_set() const noexcept {
        return signaled;
    }

private:
    bool signaled;
};

/// Waits for a notification under a mutex, like std::condition_variable.
class condition_variable : public sync_primitive {
public:
    using wait_awaiter = wait_node;

    explicit condition_variable(
        std::source_location location = std::source_location::current()) noexcept :
        sync_primitive(Kind::ConditionVariable, location) {}

    /// Unlocks `m`, waits for a notification and locks `m` again, like
    /// std::condition_variable::wait. Every way out holds `m`: a wait
    /// cancelled from outside locks `m` again before the cancellation goes
    /// on, and one reached by a cancel before it waits keeps `m` locked.
    wait_awaiter wait(mutex& m) noexcept {
        return wait_awaiter(*this, m);
    }

    /// Wakes the first waiter.
    void notify_one() {
        wake_one();
    }

    /// Wakes every waiter.
    void notify_all() {
        wake_all();
    }
};

}  // namespace kota
