#include "kota/async/io/process.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#elif defined(__linux__)
#include <sys/epoll.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#include <unistd.h>
#endif

#include "awaiter.h"
#include "kota/async/runtime/when.h"

namespace kota {

struct process::Self {
    sys::Child child;

    /// Its loop; null once the loop is gone.
    uv_loop_t* loop;

    struct Watch;

    /// What wakes the loop when the child exits; null once nothing does: the
    /// child reaped, or the loop gone.
    Watch* watch = nullptr;

    /// The pending wait.
    uv::waiter_slot<exit_status> slot;

    /// How the child ended, once it has.
    std::optional<exit_status> exited;

    /// Why it could not be reaped: another did.
    error lost;

    /// Its process is gone, but the child still runs: reaped and freed once
    /// it exits, or with the loop.
    bool orphaned = false;

#ifdef _WIN32
    /// The thread-pool wait on its handle.
    HANDLE wait_handle = nullptr;
#elif defined(__APPLE__)
    /// kqueue told of its exit, which it may before the child is a zombie:
    /// the next look waits for it.
    bool exiting = false;
#endif

    struct ExitWait;

    /// capture()'s wait for the exit, while it waits.
    ExitWait* exit_wait = nullptr;

    Self(sys::Child child, uv_loop_t& loop) noexcept : child(std::move(child)), loop(&loop) {}

    /// Reaps the child if it has exited, and wakes what waits for it.
    void reap();

    static void destroy(Self* self) noexcept;
};

/// The children of one loop whose exits it waits for, and what wakes it
/// for them: on Linux an epoll set of their pidfds, on macOS a kqueue of
/// their EVFILT_PROC events, under one poll handle; SIGCHLD for children
/// neither can watch; on Windows a thread-pool wait on each handle, which
/// posts to the loop. It keeps the loop alive while a child whose process
/// is still there runs, as libuv's process handles do.
struct process::Self::Watch {
    uv_loop_t& loop;

    /// The children watched, orphans among them.
    std::vector<Self*> watched;

    /// Those whose processes are still there.
    int owned = 0;

    /// Handles still to close once the loop goes.
    int closing = 0;

#ifndef _WIN32
    /// The pidfds' epoll set or the kqueue, -1 where it cannot be had or
    /// the loop cannot poll it.
    int multiplexer = -1;

    uv_poll_t poll{};

    /// SIGCHLD, once a child needs it; the children it is for.
    std::optional<uv_signal_t> sigchld;

    std::vector<Self*> polled;

    /// The children to look at on the next turn, which nothing else may
    /// wake for: the exit is seen from the loop, never during the spawn.
    uv_timer_t soon{};

    std::vector<Self*> unseen;
#else
    uv_async_t async{};

    /// The children whose waits fired, handed over from the thread pool.
    std::mutex mutex;

    std::vector<Self*> signaled;
#endif

    explicit Watch(uv_loop_t& loop) noexcept : loop(loop) {}

    /// Opens what wakes the loop, which close() closes.
    void open() noexcept {
#ifdef __linux__
        multiplexer = ::epoll_create1(EPOLL_CLOEXEC);
#elif defined(__APPLE__)
        multiplexer = ::kqueue();
#endif
#ifndef _WIN32
        // One the loop cannot poll is none, and SIGCHLD stands in: epoll
        // refuses a set nested in the loop's past its limit.
        if(multiplexer >= 0 && ::uv_poll_init(&loop, &poll, multiplexer) != 0) {
            ::close(std::exchange(multiplexer, -1));
        }
        if(multiplexer >= 0) {
            poll.data = this;
            ::uv_poll_start(&poll, UV_READABLE, on_ready);
            ::uv_unref(reinterpret_cast<uv_handle_t*>(&poll));
        }
        ::uv_timer_init(&loop, &soon);
        soon.data = this;
        ::uv_unref(reinterpret_cast<uv_handle_t*>(&soon));
#else
        ::uv_async_init(&loop, &async, on_signaled);
        async.data = this;
        ::uv_unref(reinterpret_cast<uv_handle_t*>(&async));
#endif
    }

    /// The watch of `loop`, made on first use and gone with the loop.
    static Watch& of(event_loop& loop) {
        // Never destroyed: a loop of static storage may go after them.
        static auto& table_mutex = *new std::mutex;
        static auto& table = *new std::map<uv_loop_t*, Watch*>;
        std::scoped_lock lock(table_mutex);
        auto* native = loop.native_handle();
        // Null until the watch is made: an allocation that throws leaves it
        // to the next spawn to make.
        auto& entry = table[native];
        if(entry == nullptr) {
            // Opened once nothing can throw any more: a throw leaves no
            // handle on the loop, nor a callback for a watch never made.
            auto made = std::make_unique<Watch>(*native);
            // A loop made later at the same address starts afresh.
            loop.on_destroy([native] {
                Watch* watch = nullptr;
                {
                    std::scoped_lock lock(table_mutex);
                    watch = table.extract(native).mapped();
                }
                watch->close();
            });
            entry = made.release();
            entry->open();
        }
        return *entry;
    }

    /// Starts watching `self`'s child; the error when it cannot.
    error add(Self& self) {
        watched.push_back(&self);
        self.watch = this;
        own(+1);
#ifdef _WIN32
        auto on_exited = [](void* context, BOOLEAN) {
            auto& exited = *static_cast<Self*>(context);
            auto& watch = *exited.watch;
            {
                std::scoped_lock lock(watch.mutex);
                watch.signaled.push_back(&exited);
            }
            ::uv_async_send(&watch.async);
        };
        if(!::RegisterWaitForSingleObject(&self.wait_handle,
                                          self.child.native_handle(),
                                          on_exited,
                                          &self,
                                          INFINITE,
                                          WT_EXECUTEONLYONCE)) {
            auto failed = error(::uv_translate_sys_error(static_cast<int>(::GetLastError())));
            self.wait_handle = nullptr;
            forget(self);
            return failed;
        }
#else
        if(!watch_alone(self)) {
            // SIGCHLD stands in, armed before the first look: a child that
            // exited before it was would send none.
            if(!sigchld) {
                sigchld.emplace();
                ::uv_signal_init(&loop, &*sigchld);
                sigchld->data = this;
                // Kept alive as the poll handle is: this child counts already.
                ::uv_signal_start(&*sigchld, on_sigchld, SIGCHLD);
            }
            polled.push_back(&self);
            look_soon(self);
        }
#endif
        return {};
    }

#ifndef _WIN32
    /// Looks at `self`'s child on the next turn, where a SIGCHLD that came
    /// before its handler was armed has no one to tell.
    void look_soon(Self& self) {
        unseen.push_back(&self);
        ::uv_timer_start(&soon, on_soon, 0, 0);
    }

    /// Watches `self`'s child through the multiplexer; false when it cannot,
    /// a child without a pidfd among them. A child that exited already is
    /// reaped at once.
    bool watch_alone(Self& self) {
        if(multiplexer < 0) {
            return false;
        }
#ifdef __linux__
        int pidfd = self.child.native_handle();
        if(pidfd < 0) {
            return false;
        }
        epoll_event event{.events = EPOLLIN, .data = {.ptr = &self}};
        return ::epoll_ctl(multiplexer, EPOLL_CTL_ADD, pidfd, &event) == 0;
#else
        struct kevent change;
        EV_SET(&change,
               static_cast<uintptr_t>(self.child.pid()),
               EVFILT_PROC,
               EV_ADD | EV_ONESHOT,
               NOTE_EXIT,
               0,
               &self);
        if(::kevent(multiplexer, &change, 1, nullptr, 0, nullptr) == 0) {
            return true;
        }
        // Gone already, or going: a process exiting, or a zombie, is none
        // kqueue knows of.
        if(errno == ESRCH) {
            self.exiting = true;
            look_soon(self);
            return true;
        }
        return false;
#endif
    }
#endif

    /// Counts a child whose process is there, or one fewer; the loop stays
    /// alive while there are some.
    void own(int change) {
        const bool before = owned > 0;
        owned += change;
        const bool after = owned > 0;
        if(before == after) {
            return;
        }
        auto keep = [&](uv_handle_t* handle) {
            after ? ::uv_ref(handle) : ::uv_unref(handle);
        };
#ifdef _WIN32
        keep(reinterpret_cast<uv_handle_t*>(&async));
#else
        if(multiplexer >= 0) {
            keep(reinterpret_cast<uv_handle_t*>(&poll));
        }
        if(sigchld) {
            keep(reinterpret_cast<uv_handle_t*>(&*sigchld));
        }
#endif
    }

    /// Whether `self` is still watched: what woke the loop may name one a
    /// task woken before it has freed since.
    bool watches(Self* self) const {
        return std::ranges::find(watched, self) != watched.end();
    }

    /// Stops watching `self`, which has exited or goes.
    void forget(Self& self) {
#ifdef _WIN32
        if(self.wait_handle != nullptr) {
            // Waits for a callback under way, which reads `self.watch` and
            // hands `self` over.
            ::UnregisterWaitEx(std::exchange(self.wait_handle, nullptr), INVALID_HANDLE_VALUE);
            std::scoped_lock lock(mutex);
            std::erase(signaled, &self);
        }
#endif
        std::erase(watched, &self);
        self.watch = nullptr;
        if(!self.orphaned) {
            own(-1);
        }
#ifndef _WIN32
        std::erase(polled, &self);
        std::erase(unseen, &self);
#ifdef __linux__
        int pidfd = self.child.native_handle();
        if(multiplexer >= 0 && pidfd >= 0) {
            ::epoll_ctl(multiplexer, EPOLL_CTL_DEL, pidfd, nullptr);
        }
#elif defined(__APPLE__)
        if(multiplexer >= 0 && !self.exited) {
            struct kevent change;
            EV_SET(&change,
                   static_cast<uintptr_t>(self.child.pid()),
                   EVFILT_PROC,
                   EV_DELETE,
                   0,
                   0,
                   nullptr);
            ::kevent(multiplexer, &change, 1, nullptr, 0, nullptr);
        }
#endif
#endif
    }

    /// The process of `self` is gone while its child runs.
    void orphan(Self& self) {
        self.orphaned = true;
        own(-1);
    }

    /// Reaps each of `children` still watched.
    void reap(const std::vector<Self*>& children) {
        for(auto* self: children) {
            if(watches(self)) {
                self->reap();
            }
        }
    }

#ifndef _WIN32
    static void on_ready(uv_poll_t* handle, int, int) {
        auto& watch = *static_cast<Watch*>(handle->data);
        std::vector<Self*> ready;
#ifdef __linux__
        epoll_event events[16];
        int count = ::epoll_wait(watch.multiplexer, events, 16, 0);
        for(int i = 0; i < count; ++i) {
            ready.push_back(static_cast<Self*>(events[i].data.ptr));
        }
#else
        struct kevent events[16];
        timespec now{};
        int count = ::kevent(watch.multiplexer, nullptr, 0, events, 16, &now);
        for(int i = 0; i < count; ++i) {
            auto* self = static_cast<Self*>(events[i].udata);
            if(watch.watches(self)) {
                self->exiting = true;
            }
            ready.push_back(self);
        }
#endif
        watch.reap(ready);
    }

    static void on_soon(uv_timer_t* handle) {
        auto& watch = *static_cast<Watch*>(handle->data);
        std::vector<Self*> children;
        children.swap(watch.unseen);
        watch.reap(children);
    }

    static void on_sigchld(uv_signal_t* handle, int) {
        auto& watch = *static_cast<Watch*>(handle->data);
        // A copy: reaping takes them out.
        watch.reap(std::vector(watch.polled));
    }
#else
    static void on_signaled(uv_async_t* handle) {
        auto& watch = *static_cast<Watch*>(handle->data);
        std::vector<Self*> ready;
        {
            std::scoped_lock lock(watch.mutex);
            ready.swap(watch.signaled);
        }
        watch.reap(ready);
    }
#endif

    /// The loop goes: the orphans with it, unreaped, and the processes still
    /// there wait on nothing any more.
    void close() {
        for(auto* self: std::vector(watched)) {
            const bool orphaned = self->orphaned;
            forget(*self);
            if(orphaned) {
                // One that exited is reaped still; one that runs is left to
                // the system.
                [[maybe_unused]] auto reaped = self->child.try_wait();
                delete self;
            } else {
                self->loop = nullptr;
                self->slot.detach();
            }
        }
        auto closed = [](uv_handle_t* handle) {
            auto* watch = static_cast<Watch*>(handle->data);
            if(--watch->closing == 0) {
                delete watch;
            }
        };
#ifdef _WIN32
        ++closing;
        ::uv_close(reinterpret_cast<uv_handle_t*>(&async), closed);
#else
        if(multiplexer >= 0) {
            ++closing;
            ::uv_close(reinterpret_cast<uv_handle_t*>(&poll), closed);
        }
        if(sigchld) {
            ++closing;
            ::uv_close(reinterpret_cast<uv_handle_t*>(&*sigchld), closed);
        }
        ++closing;
        ::uv_close(reinterpret_cast<uv_handle_t*>(&soon), closed);
#endif
    }

    ~Watch() {
#ifndef _WIN32
        if(multiplexer >= 0) {
            ::close(multiplexer);
        }
#endif
    }
};

namespace {

/// The error of a std::error_code from sys: errno's on POSIX, where libuv's
/// are their negations; on Windows a std::errc where the error has a POSIX
/// counterpart, else the Win32 code itself.
error error_of(std::error_code code) {
    if(!code) {
        return {};
    }
    // The child another reaped.
    if(code == std::errc::no_child_process) {
        return error::no_such_process;
    }
#ifdef _WIN32
    if(code.category() == std::system_category()) {
        return error(::uv_translate_sys_error(code.value()));
    }
    switch(code.value()) {
#define KOTA_ERRNO(name)                                                                           \
    case name: return error(UV_##name);
        KOTA_ERRNO(E2BIG)
        KOTA_ERRNO(EACCES)
        KOTA_ERRNO(EAGAIN)
        KOTA_ERRNO(EBADF)
        KOTA_ERRNO(EBUSY)
        KOTA_ERRNO(EEXIST)
        KOTA_ERRNO(EILSEQ)
        KOTA_ERRNO(EINVAL)
        KOTA_ERRNO(EIO)
        KOTA_ERRNO(EISDIR)
        KOTA_ERRNO(ELOOP)
        KOTA_ERRNO(EMFILE)
        KOTA_ERRNO(ENAMETOOLONG)
        KOTA_ERRNO(ENFILE)
        KOTA_ERRNO(ENOENT)
        KOTA_ERRNO(ENOEXEC)
        KOTA_ERRNO(ENOMEM)
        KOTA_ERRNO(ENOSPC)
        KOTA_ERRNO(ENOSYS)
        KOTA_ERRNO(ENOTDIR)
        KOTA_ERRNO(ENOTSUP)
        KOTA_ERRNO(EPERM)
        KOTA_ERRNO(ESRCH)
#undef KOTA_ERRNO
        default: return error::unknown_error;
    }
#else
    return error(-code.value());
#endif
}

/// The parent's end of a pipe of the child's, on `loop`.
result<pipe> open_pipe(fs::File::native_handle_type end, event_loop& loop) {
#ifdef _WIN32
    // libuv takes a C runtime descriptor, which owns the handle it wraps.
    int fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(end), 0);
    if(fd < 0) {
        ::CloseHandle(end);
        return outcome_error(error::too_many_open_files);
    }
    auto opened = pipe::open(fd, loop);
    if(!opened) {
        ::_close(fd);
    }
#else
    auto opened = pipe::open(end, loop);
    if(!opened) {
        ::close(end);
    }
#endif
    return opened;
}

/// Kills `child` and the group it leads, if it leads one: on POSIX the child
/// itself too, which may have left its group for another, where Windows's
/// job holds it for good. The error when the child could not be killed; a
/// child that exited counts as killed.
std::error_code end_tree(sys::Child& child) noexcept {
    auto killed = child.kill_tree();
#ifdef _WIN32
    if(!killed) {
        return {};
    }
#endif
    killed = child.kill();
    return killed == std::errc::no_such_process ? std::error_code() : killed;
}

}  // namespace

/// Waits for the child, started that same turn, to exit; a cancel kills it,
/// or the group it leads, and the wait goes on until it has exited.
struct process::Self::ExitWait : uv::uv_op<ExitWait> {
    Self& self;

    explicit ExitWait(Self& self) noexcept : self(self) {}

    bool start() noexcept {
        if(self.exited || self.lost) {
            return false;
        }
        self.exit_wait = this;
        return true;
    }

    void cancel() noexcept {
        // no_such_process only means a child that exited, whose reaping
        // completes this. A child the caller may not signal would keep the
        // cancel waiting for good.
        if(end_tree(self.child)) {
            std::abort();
        }
    }

    result<exit_status> await_resume() const noexcept {
        if(self.lost) {
            return outcome_error(self.lost);
        }
        return *self.exited;
    }
};

void process::Self::reap() {
    auto status = child.try_wait();
#ifdef __APPLE__
    // Exited already, if not yet a zombie: a moment's wait.
    if(exiting && status && !*status) {
        status = child.wait();
    }
#endif
    if(status && !*status) {
        return;
    }
    if(status) {
        exited = **status;
    } else {
        lost = error_of(status.error());
    }
    if(watch) {
        watch->forget(*this);
    }
    if(orphaned) {
        delete this;
        return;
    }
    // capture()'s wait and wait()'s never wait on the same process. What
    // resumes may destroy the process, and this with it: nothing of this is
    // read after.
    if(auto* waiting = std::exchange(exit_wait, nullptr)) {
        waiting->complete();
    } else if(slot.waiting()) {
        if(lost) {
            slot.deliver(outcome_error(lost));
        } else {
            slot.deliver(*exited);
        }
    }
}

void process::Self::destroy(Self* self) noexcept {
    if(self->loop != nullptr) {
        self->slot.abort(*self->loop);
    }
    // Reaped once it exits, with nothing waiting for it.
    if(self->watch && !self->exited && !self->lost) {
        self->watch->orphan(*self);
        return;
    }
    if(self->watch) {
        self->watch->forget(*self);
    }
    delete self;
}

process::process() noexcept = default;

process::process(detail::unique_handle<Self> self) noexcept : self(std::move(self)) {}

process::~process() = default;

process::process(process&& other) noexcept = default;

process& process::operator=(process&& other) noexcept = default;

result<process::spawn_result> process::spawn(const options& opts, event_loop& loop) {
    // Made before the child, which nothing then separates from its watch but
    // what the rollback below undoes.
    detail::unique_handle<Self> self(new Self(sys::Child(), *loop.native_handle()));
    auto child = sys::spawn(opts);
    if(!child) {
        return outcome_error(error_of(child.error()));
    }
    self->child = std::move(*child);

    // Until the loop watches it, a failure, returned or thrown, ends the
    // child and its tree and reaps it, so that none is left behind; it has
    // not run much.
    struct Rollback {
        sys::Child* child;

        ~Rollback() {
            if(child != nullptr) {
                [[maybe_unused]] auto killed = end_tree(*child);
                [[maybe_unused]] auto reaped = child->wait();
            }
        }
    } rollback{&self->child};

    std::array<pipe, 3> pipes;
    for(std::size_t i = 0; i < pipes.size(); ++i) {
        if(opts.streams[i].kind != stdio::Kind::Pipe) {
            continue;
        }
        auto opened = open_pipe(self->child.take_pipe(i), loop);
        if(!opened) {
            return outcome_error(opened.error());
        }
        pipes[i] = std::move(*opened);
    }
    if(auto failed = Self::Watch::of(loop).add(*self)) {
        return outcome_error(failed);
    }
    rollback.child = nullptr;
    return spawn_result{
        .proc = process(std::move(self)),
        .stdin_pipe = std::move(pipes[0]),
        .stdout_pipe = std::move(pipes[1]),
        .stderr_pipe = std::move(pipes[2]),
    };
}

task<process::capture_result, error> process::capture(options opts, event_loop& loop) {
    opts.streams = {stdio::ignore(), stdio::pipe(), stdio::pipe()};
    auto spawned = spawn(opts, loop);
    if(!spawned) {
        co_await fail(spawned.error());
    }

    // A capture that ends before both pipes did, cancelled once the child
    // exited while what it started holds them, kills its group too.
    struct TreeKiller {
        Self& self;
        bool read = false;

        ~TreeKiller() {
            if(!read) {
                [[maybe_unused]] auto killed = end_tree(self.child);
            }
        }
    } killer{*spawned->proc.self};

    // The exit wait starts first, so that a read failing at once still kills
    // the child and waits for it. Both pipes are read while the child runs:
    // one that fills a pipe waits for it to be read before it can exit.
    auto [status, stdout_data, stderr_data] =
        co_await or_fail(co_await when_all(Self::ExitWait(*spawned->proc.self),
                                           spawned->stdout_pipe.read_to_end(),
                                           spawned->stderr_pipe.read_to_end()));
    killer.read = true;
    co_return capture_result{
        .status = co_await or_fail(std::move(status)),
        .stdout_data = std::move(stdout_data),
        .stderr_data = std::move(stderr_data),
    };
}

task<process::exit_status, error> process::wait() {
    if(!self) {
        co_await fail(error::invalid_argument);
    }
    if(self->exited) {
        co_return *self->exited;
    }
    if(self->lost) {
        co_await fail(self->lost);
    }
    // The loop gone, nothing reaps it any more.
    if(!self->watch) {
        co_await fail(error::operation_aborted);
    }
    co_return co_await self->slot.wait();
}

int process::pid() const noexcept {
    return self ? self->child.pid() : -1;
}

error process::kill(int signum) {
    if(!self) {
        return error::invalid_argument;
    }
    return error_of(self->child.kill(signum));
}

error process::kill() {
    if(!self) {
        return error::invalid_argument;
    }
    return error_of(self->child.kill());
}

error process::kill_tree(int signum) {
    if(!self) {
        return error::invalid_argument;
    }
    return error_of(self->child.kill_tree(signum));
}

error process::kill_tree() {
    if(!self) {
        return error::invalid_argument;
    }
    return error_of(self->child.kill_tree());
}

}  // namespace kota
