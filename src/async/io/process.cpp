#include "kotatsu/async/io/process.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <unistd.h>

// pidfd_open's number on every architecture but Alpha's and MIPS's, for C
// libraries whose headers are older than it (Linux 5.3).
#if !defined(SYS_pidfd_open) && !defined(__alpha__) && !defined(__mips__)
#define SYS_pidfd_open 434
#endif
#elif defined(__APPLE__)
#include <sys/event.h>
#include <unistd.h>
#endif

#include "awaiter.h"
#include "kotatsu/support/scope_exit.h"
#include "kotatsu/sys/detail/descriptors.h"
#include "kotatsu/async/runtime/when.h"

#ifdef _WIN32
#include "kotatsu/fs/windows.h"
#endif

namespace kotatsu::async {

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
/// posts to the loop, and so for the processes wait_exit() waits for. It
/// keeps the loop alive while a child whose process is still there runs, as
/// libuv's process handles do, and while wait_exit() waits.
struct process::Self::Watch {
    uv_loop_t& loop;

    /// The children watched, orphans among them.
    std::vector<Self*> watched;

    /// Those whose processes are still there, and the processes waited on.
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

    /// A wait_exit(): the thread pool's wait on a process, through a handle
    /// that may only wait, which the watch hands to the loop as a child's.
    struct Exit {
        HANDLE process;

        /// The thread pool's wait; null once it is unregistered.
        HANDLE wait_handle = nullptr;

        /// Its watch; null once it watches it no more: the process exited,
        /// or the loop is gone.
        Watch* watch = nullptr;

        uv::waiter_slot<void> slot;
    };

    /// The processes waited on, counted with the children whose processes
    /// are there, and those whose waits fired, handed over as `signaled`.
    std::vector<Exit*> exits;

    std::vector<Exit*> signaled_exits;
#endif

    explicit Watch(uv_loop_t& loop) noexcept : loop(loop) {}

    /// Opens what wakes the loop, which close() closes.
    void open() noexcept {
        // Made at the loop's first spawn, around which a program may close a
        // standard stream; kqueue() makes it close-on-exec.
#ifdef __linux__
        multiplexer = sys::detail::move_above_standard_streams(::epoll_create1(EPOLL_CLOEXEC));
#elif defined(__APPLE__)
        multiplexer = sys::detail::move_above_standard_streams(::kqueue());
#endif
#ifndef _WIN32
        // One the loop cannot poll is none, and SIGCHLD stands in: epoll
        // refuses a set nested past its limit, or past max_user_watches.
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
        // Null until the watch is made: a throw before leaves it to the next
        // spawn to make.
        auto& entry = table[native];
        if(entry == nullptr) {
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
            // Opened once nothing can throw any more: a throw leaves no
            // handle on the loop.
            entry = made.release();
            entry->open();
        }
        return *entry;
    }

    /// Starts watching `self`'s child; the error when it cannot.
    error add(Self& self) {
#ifndef _WIN32
        // Room for it in the lists it may join, made before anything changes:
        // nothing throws once it is watched.
        make_room(polled);
        make_room(unseen);
#endif
        watched.push_back(&self);
        self.watch = this;
        own(+1);
#ifdef _WIN32
        if(auto failed = wait_for<&Watch::signaled>(self, self.child.native_handle())) {
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

#ifdef _WIN32
    /// Waits for `exit`'s process; the error when it cannot.
    error add(Exit& exit) {
        exits.push_back(&exit);
        exit.watch = this;
        own(+1);
        if(auto failed = wait_for<&Watch::signaled_exits>(exit, exit.process)) {
            forget(exit);
            return failed;
        }
        return {};
    }

    /// Has the thread pool wait for `object`, and hand `entry` over through
    /// `Fired` once it is signaled; the error when it cannot.
    template <auto Fired, typename Entry>
    static error wait_for(Entry& entry, HANDLE object) {
        auto on_fired = [](void* context, BOOLEAN) {
            auto& fired = *static_cast<Entry*>(context);
            auto& watch = *fired.watch;
            {
                std::scoped_lock lock(watch.mutex);
                (watch.*Fired).push_back(&fired);
            }
            ::uv_async_send(&watch.async);
        };
        if(::RegisterWaitForSingleObject(&entry.wait_handle,
                                         object,
                                         on_fired,
                                         &entry,
                                         INFINITE,
                                         WT_EXECUTEONLYONCE)) {
            return {};
        }
        entry.wait_handle = nullptr;
        return error(::uv_translate_sys_error(static_cast<int>(::GetLastError())));
    }

    /// Ends the thread pool's wait for `entry`, if it has one, and takes it
    /// out of `Fired`. Waits for a callback under way, which reads
    /// `entry.watch` and hands `entry` over.
    template <auto Fired, typename Entry>
    void unwait(Entry& entry) {
        if(entry.wait_handle == nullptr) {
            return;
        }
        ::UnregisterWaitEx(std::exchange(entry.wait_handle, nullptr), INVALID_HANDLE_VALUE);
        std::scoped_lock lock(mutex);
        std::erase(this->*Fired, &entry);
    }

    /// Stops waiting for `exit`'s process, which exited, or whose wait goes.
    void forget(Exit& exit) {
        unwait<&Watch::signaled_exits>(exit);
        std::erase(exits, &exit);
        exit.watch = nullptr;
        own(-1);
    }
#else
    /// Makes room for one more in `children`, growing it as push_back does.
    static void make_room(std::vector<Self*>& children) {
        if(children.size() == children.capacity()) {
            children.reserve(2 * children.size() + 1);
        }
    }

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

    /// Counts a child whose process is there or a process waited on, or one
    /// fewer; the loop stays alive while there are some.
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
        unwait<&Watch::signaled>(self);
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
        std::vector<Exit*> exited;
        {
            std::scoped_lock lock(watch.mutex);
            ready.swap(watch.signaled);
            exited.swap(watch.signaled_exits);
        }
        watch.reap(ready);
        // What a wait resumes may end the waits after it.
        for(auto* exit: exited) {
            if(std::ranges::find(watch.exits, exit) == watch.exits.end()) {
                continue;
            }
            watch.forget(*exit);
            // A wait cancelled stays until its task goes.
            if(exit->slot.waiting()) {
                exit->slot.deliver({});
            }
        }
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
#ifdef _WIN32
        for(auto* exit: std::vector(exits)) {
            forget(*exit);
            exit->slot.detach();
        }
#endif
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
#define KOTATSU_ERRNO(name)                                                                        \
    case name: return error(UV_##name);
        KOTATSU_ERRNO(E2BIG)
        KOTATSU_ERRNO(EACCES)
        KOTATSU_ERRNO(EAGAIN)
        KOTATSU_ERRNO(EBADF)
        KOTATSU_ERRNO(EBUSY)
        KOTATSU_ERRNO(EEXIST)
        KOTATSU_ERRNO(EILSEQ)
        KOTATSU_ERRNO(EINVAL)
        KOTATSU_ERRNO(EIO)
        KOTATSU_ERRNO(EISDIR)
        KOTATSU_ERRNO(ELOOP)
        KOTATSU_ERRNO(EMFILE)
        KOTATSU_ERRNO(ENAMETOOLONG)
        KOTATSU_ERRNO(ENFILE)
        KOTATSU_ERRNO(ENOENT)
        KOTATSU_ERRNO(ENOEXEC)
        KOTATSU_ERRNO(ENOMEM)
        KOTATSU_ERRNO(ENOSPC)
        KOTATSU_ERRNO(ENOSYS)
        KOTATSU_ERRNO(ENOTDIR)
        KOTATSU_ERRNO(ENOTSUP)
        KOTATSU_ERRNO(EPERM)
        KOTATSU_ERRNO(ESRCH)
#undef KOTATSU_ERRNO
        default: return error::unknown_error;
    }
#else
    return error(-code.value());
#endif
}

/// The parent's end of a pipe of the child's, on `loop`.
result<pipe> open_pipe(kotatsu::fs::File::native_handle_type end, event_loop& loop) {
#ifdef _WIN32
    // libuv takes a C runtime descriptor, which owns the handle it wraps.
    int fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(end), 0);
    if(fd < 0) {
        ::CloseHandle(end);
        return outcome_error(error::too_many_open_files);
    }
#else
    int fd = end;
#endif
    // Closed unless the pipe takes it: its open failed or threw, or on Windows
    // libuv took a duplicate.
    ScopeExit unopened([fd] {
#ifdef _WIN32
        ::_close(fd);
#else
        ::close(fd);
#endif
    });

    auto opened = pipe::open(fd, loop);
#ifdef _WIN32
    // Of a descriptor from 0 to 2, which a program that closed its standard
    // streams gives out again, libuv takes a duplicate of the handle and
    // leaves the descriptor: kept, it would hold the pipe open.
    const bool taken = opened && fd > 2;
#else
    const bool taken = opened.has_value();
#endif
    if(taken) {
        unopened.release();
    }
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

#ifdef __linux__
/// A pidfd of process `pid`, or -1 with errno set: ENOSYS where the kernel
/// has none, EPERM where a sandbox forbids them.
int open_pidfd(int pid) noexcept {
#ifdef SYS_pidfd_open
    return sys::detail::move_above_standard_streams(
        static_cast<int>(::syscall(SYS_pidfd_open, pid, 0)));
#else
    errno = ENOSYS;
    return -1;
#endif
}

/// How a process stands, as /proc tells.
enum class ProcState : std::uint8_t {
    Running,
    /// Exited, and not reaped yet.
    Zombie,
    /// No process has the pid.
    Gone,
};

/// How process `pid` stands, as /proc/<pid>/stat tells; the error that kept
/// it from telling.
result<ProcState> state_of(int pid) {
    const int fd = ::open(std::format("/proc/{}/stat", pid).c_str(), O_RDONLY | O_CLOEXEC);
    if(fd < 0 && errno != ENOENT && errno != EACCES && errno != EPERM) {
        return outcome_error(error(-errno));
    }
    // Gone, or out of sight, /proc not mounted or hiding other users'
    // processes: kill() tells whether the pid is there, zombies among them.
    if(fd < 0) {
        return ::kill(pid, 0) != 0 && errno == ESRCH ? ProcState::Gone : ProcState::Running;
    }
    char stat[512];
    const auto size = ::read(fd, stat, sizeof(stat));
    const int read_errno = errno;
    ::close(fd);
    // A process reaped since the open reads as ESRCH.
    if(size < 0 && read_errno == ESRCH) {
        return ProcState::Gone;
    }
    if(size < 0) {
        return outcome_error(error(-read_errno));
    }
    // "<pid> (<name>) <state> ...", where the name may hold anything.
    const std::string_view text(stat, static_cast<std::size_t>(size));
    const auto name_end = text.rfind(')');
    assert(name_end != std::string_view::npos && name_end + 2 < text.size());
    const auto fields = text.substr(name_end + 2);
    if(fields.front() != 'Z') {
        return ProcState::Running;
    }
    // A first thread that ended before the others is a zombie the process
    // outlives: the 20th field counts the threads, a zombie among them.
    std::size_t threads = 0;
    for(int field = 3; field < 20; ++field) {
        threads = fields.find(' ', threads) + 1;
    }
    return fields.substr(threads).starts_with("1 ") ? ProcState::Zombie : ProcState::Running;
}
#endif

#ifndef _WIN32
/// What wait_exit() waits on: a descriptor the loop polls, readable once the
/// process has exited, a pidfd on Linux and on macOS a kqueue told of the
/// exit; or on Linux without pidfds a timer that looks at /proc every
/// second.
struct ExitPoll : uv::owned_handle<ExitPoll> {
    union {
        uv_handle_t handle;
        uv_poll_t poll;
#ifdef __linux__
        uv_timer_t timer;
#endif
    };

    uv::waiter_slot<void> slot;

    /// The descriptor polled, closed with this; -1 for the timer.
    int fd = -1;

#ifdef __linux__
    /// The process the timer looks at.
    int pid = 0;
#endif

    ~ExitPoll() {
        if(fd >= 0) {
            ::close(fd);
        }
    }

    /// Polls `fd`, set to a descriptor, on `loop`.
    error start_poll(uv_loop_t& loop) {
        if(auto failed = error(::uv_poll_init(&loop, &poll, fd))) {
            return failed;
        }
        ::uv_poll_start(&poll, UV_READABLE, on_readable);
        return {};
    }

    /// Ends the wait with `status`. A wait cancelled stays until its task
    /// goes, which closes the handle.
    void end(error status) {
        if(slot.waiting()) {
            slot.deliver(status);
        }
    }

    static void on_readable(uv_poll_t* handle, int status, int) {
        // Readable for good once the process has exited.
        ::uv_poll_stop(handle);
        static_cast<ExitPoll*>(handle->data)->end(error(status));
    }

#ifdef __linux__
    static void on_tick(uv_timer_t* handle) {
        auto& self = *static_cast<ExitPoll*>(handle->data);
        auto state = state_of(self.pid);
        if(state.has_value() && *state == ProcState::Running) {
            return;
        }
        ::uv_timer_stop(handle);
        self.end(state.has_value() ? error() : state.error());
    }
#endif
};
#endif

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
    // Its watch would come after the loop closed its handles, and keep it
    // from closing.
    if(uv::destroying(*loop.native_handle())) {
        return outcome_error(error::operation_aborted);
    }
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
    ScopeExit rollback([&self] {
        [[maybe_unused]] auto killed = end_tree(self->child);
        [[maybe_unused]] auto reaped = self->child.wait();
    });

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
    rollback.release();
    return spawn_result{
        .proc = process(std::move(self)),
        .stdin_pipe = std::move(pipes[0]),
        .stdout_pipe = std::move(pipes[1]),
        .stderr_pipe = std::move(pipes[2]),
    };
}

#ifdef _WIN32

result<process> process::open(int pid, std::uint32_t access, event_loop& loop) {
    // Its watch would come after the loop closed its handles, as spawn's.
    if(uv::destroying(*loop.native_handle())) {
        return outcome_error(error::operation_aborted);
    }
    auto child = sys::Child::open(pid, access);
    if(!child) {
        return outcome_error(error_of(child.error()));
    }
    detail::unique_handle<Self> self(new Self(std::move(*child), *loop.native_handle()));
    if(auto failed = Self::Watch::of(loop).add(*self)) {
        return outcome_error(failed);
    }
    return process(std::move(self));
}

#endif

task<process::capture_result, error> process::capture(options opts, event_loop& loop) {
    opts.streams = {stdio::ignore(), stdio::pipe(), stdio::pipe()};
    auto spawned = spawn(opts, loop);
    if(!spawned) {
        co_await fail(spawned.error());
    }

    // A capture that ends before both pipes did, cancelled once the child
    // exited while what it started holds them, kills its group too.
    ScopeExit killer(
        [&spawned] { [[maybe_unused]] auto killed = end_tree(spawned->proc.self->child); });

    // The exit wait starts first, so that a read failing at once still kills
    // the child and waits for it. Both pipes are read while the child runs:
    // one that fills a pipe waits for it to be read before it can exit.
    auto [status, stdout_data, stderr_data] =
        co_await or_fail(co_await when_all(Self::ExitWait(*spawned->proc.self),
                                           spawned->stdout_pipe.read_to_end(),
                                           spawned->stderr_pipe.read_to_end()));
    killer.release();
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

process::native_handle_type process::native_handle() const noexcept {
    return self ? self->child.native_handle() : sys::Child().native_handle();
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

task<void, error> wait_exit(int pid, event_loop& loop) {
    // No process has an ID below 1, which kill() takes for groups.
    if(pid <= 0) {
        co_await fail(error::invalid_argument);
    }
    // What it waits with would come after the loop closed its handles, as a
    // spawn's watch would.
    if(uv::destroying(*loop.native_handle())) {
        co_await fail(error::operation_aborted);
    }
#ifdef _WIN32
    using Watch = process::Self::Watch;
    Watch::Exit waited{};
    waited.process = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if(waited.process == nullptr) {
        const auto code = ::GetLastError();
        co_await fail(code == ERROR_INVALID_PARAMETER
                          ? error::no_such_process
                          : error_of(kotatsu::fs::windows::to_error_code(code)));
    }
    ScopeExit close([&waited] {
        if(waited.watch != nullptr) {
            waited.watch->forget(waited);
        }
        ::CloseHandle(waited.process);
    });
    if(auto failed = Watch::of(loop).add(waited)) {
        co_await fail(failed);
    }
    // Nothing but the exit ends it: the loop's destruction lets it go.
    co_await waited.slot.wait();
#else
    auto waited = ExitPoll::make();
#ifdef __linux__
    waited->fd = open_pidfd(pid);
    if(waited->fd >= 0) {
        if(auto failed = waited->start_poll(*loop.native_handle())) {
            co_await fail(failed);
        }
    } else if(errno == ENOSYS || errno == EPERM) {
        // No pidfds: /proc tells, now and every second.
        auto state = state_of(pid);
        if(!state.has_value()) {
            co_await fail(state.error());
        }
        if(*state == ProcState::Gone) {
            co_await fail(error::no_such_process);
        }
        if(*state == ProcState::Zombie) {
            co_return;
        }
        waited->pid = pid;
        ::uv_timer_init(loop.native_handle(), &waited->timer);
        ::uv_timer_start(&waited->timer, ExitPoll::on_tick, 1000, 1000);
    } else {
        co_await fail(error(-errno));
    }
#else
    waited->fd = sys::detail::move_above_standard_streams(::kqueue());
    if(waited->fd < 0) {
        co_await fail(error(-errno));
    }
    struct kevent change;
    EV_SET(&change,
           static_cast<uintptr_t>(pid),
           EVFILT_PROC,
           EV_ADD | EV_ONESHOT,
           NOTE_EXIT,
           0,
           nullptr);
    if(::kevent(waited->fd, &change, 1, nullptr, 0, nullptr) != 0) {
        // A process exiting, or a zombie, is none kqueue knows of, but one
        // kill() still finds.
        if(errno == ESRCH && (::kill(pid, 0) == 0 || errno == EPERM)) {
            co_return;
        }
        co_await fail(error(-errno));
    }
    if(auto failed = waited->start_poll(*loop.native_handle())) {
        co_await fail(failed);
    }
#endif
    if(auto failed = co_await waited->slot.wait()) {
        co_await fail(failed);
    }
#endif
}

}  // namespace kotatsu::async
