#include "os.h"
#include "kotatsu/sys/process.h"

#ifdef __linux__
#include <csignal>
#include <sys/prctl.h>
#elif defined(__APPLE__)
#include <csignal>
#include <sys/event.h>

#include "kotatsu/sys/detail/descriptors.h"
#include "kotatsu/sys/thread.h"
#endif

namespace kotatsu::sys {

#ifdef _WIN32

namespace {

/// Ends this process, as the parent's exit does.
void CALLBACK end_process(void*, BOOLEAN) {
    ::TerminateProcess(::GetCurrentProcess(), 1);
}

}  // namespace

#endif

std::error_code exit_with_parent() {
#ifdef _WIN32
    auto parent =
        Handle::open_process(parent_pid(), SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION);
    if(!parent) {
        // Gone, and every handle to it closed.
        if(parent.error() == std::errc::no_such_process) {
            end_process(nullptr, FALSE);
        }
        return parent.error();
    }
    FILETIME parent_start, own_start, exited, kernel, user;
    if(!::GetProcessTimes(parent->handle, &parent_start, &exited, &kernel, &user) ||
       !::GetProcessTimes(::GetCurrentProcess(), &own_start, &exited, &kernel, &user)) {
        return fs::windows::last_error();
    }
    // A process that started after this one took the parent's ID, the
    // parent gone.
    if(::CompareFileTime(&parent_start, &own_start) > 0) {
        end_process(nullptr, FALSE);
    }
    HANDLE wait = nullptr;
    // Fires at once for a parent that has exited and is not gone yet.
    if(!::RegisterWaitForSingleObject(&wait,
                                      parent->handle,
                                      end_process,
                                      nullptr,
                                      INFINITE,
                                      WT_EXECUTEONLYONCE)) {
        return fs::windows::last_error();
    }
    // Waited on for good.
    parent->release();
    return {};
#elif defined(__linux__)
    auto parent = ::getppid();
    if(::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) {
        return errno_code();
    }
    // The parent exited before, and the signal waits for the one that
    // adopted this process.
    if(::getppid() != parent) {
        ::raise(SIGKILL);
    }
    return {};
#else
    auto parent = ::getppid();
    // kqueue() makes it close-on-exec, and fork does not pass it on.
    int queue = detail::move_above_standard_streams(::kqueue());
    if(queue < 0) {
        return errno_code();
    }
    struct kevent change;
    EV_SET(&change, parent, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, nullptr);
    if(::kevent(queue, &change, 1, nullptr, 0, nullptr) != 0) {
        auto error = errno_code();
        ::close(queue);
        if(error == std::errc::no_such_process) {
            ::raise(SIGKILL);
        }
        return error;
    }
    // The parent exited before the watch, which then watches the one that
    // adopted this process.
    if(::getppid() != parent) {
        ::raise(SIGKILL);
    }
    auto watcher = Thread::start(64 * 1024, [queue] {
        struct kevent event;
        int count;
        while((count = ::kevent(queue, nullptr, 0, &event, 1, nullptr)) < 0 && errno == EINTR) {}
        // The queue taken away from it, as a close of every descriptor in a
        // daemon does, leaves the process be.
        if(count > 0) {
            ::raise(SIGKILL);
        }
    });
    if(!watcher) {
        ::close(queue);
        return watcher.error();
    }
    watcher->detach();
    return {};
#endif
}

}  // namespace kotatsu::sys
