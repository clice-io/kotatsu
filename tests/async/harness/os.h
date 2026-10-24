#pragma once

// What system tests take from the operating system without kota::async:
// TempDir, read_text() and write_text() from fs's harness, ScopedVariable in
// the environment, stdin_reader() for a child that runs until its stdin
// closes and exit_status_of() for how it ended, open_fd(), create_pipe(),
// close_fd() and write_fd() on raw descriptors, and BusyPool, which holds
// libuv's thread pool busy.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <latch>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <basetsd.h>
#include <fcntl.h>
#include <io.h>
using ssize_t = SSIZE_T;
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::test {

/// A variable of this process's environment, set for as long as this lives.
struct ScopedVariable {
    ScopedVariable(std::string name, const std::string& value) : name(std::move(name)) {
#ifdef _WIN32
        ::_putenv_s(this->name.c_str(), value.c_str());
#else
        ::setenv(this->name.c_str(), value.c_str(), 1);
#endif
    }

    ScopedVariable(const ScopedVariable&) = delete;
    ScopedVariable& operator=(const ScopedVariable&) = delete;

    ~ScopedVariable() {
#ifdef _WIN32
        // An empty value removes it.
        ::_putenv_s(name.c_str(), "");
#else
        ::unsetenv(name.c_str());
#endif
    }

    const std::string name;
};

/// A child that reads its stdin pipe, so it runs until the pipe closes or it
/// is killed: cat, or more.com on Windows.
inline process::options stdin_reader() {
    process::options opts;
#ifdef _WIN32
    opts.file = "more.com";
#else
    opts.file = "/bin/cat";
#endif
    opts.streams = {process::stdio::pipe(true, false),
                    process::stdio::ignore(),
                    process::stdio::ignore()};
    return opts;
}

/// The exit status that run() reports for a process::wait(), if the wait
/// ended with one.
template <typename Waited>
std::optional<std::int64_t> exit_status_of(const Waited& waited) {
    if(!waited.has_value()) {
        return std::nullopt;
    }
    return waited->status;
}

// Windows pipes have a 4 KB buffer: writing more than that before the loop
// reads blocks write_fd() for good. Write from a std::thread when the data
// may exceed it.

#ifdef _WIN32
/// Opens the UTF-8 `path` with open()'s `flags` and `mode`, as a raw
/// descriptor the loop's handles take.
inline int open_fd(std::string_view path, int flags, int mode) {
    return ::_wopen(std_path(path).c_str(), flags | _O_BINARY, mode);
}

inline int create_pipe(int fds[2]) {
    return _pipe(fds, 4096, _O_BINARY);
}

inline int close_fd(int fd) {
    return _close(fd);
}

inline ssize_t write_fd(int fd, const char* data, std::size_t len) {
    return _write(fd, data, static_cast<unsigned int>(len));
}
#else
inline int open_fd(std::string_view path, int flags, int mode) {
    return ::open(std::string(path).c_str(), flags | O_CLOEXEC, mode);
}

inline int create_pipe(int fds[2]) {
    return ::pipe(fds);
}

inline int close_fd(int fd) {
    return ::close(fd);
}

inline ssize_t write_fd(int fd, const char* data, std::size_t len) {
    return ::write(fd, data, len);
}
#endif

/// Keeps every thread of libuv's pool busy until release(), so that work
/// queued meanwhile stays in the queue, where cancelling it dequeues it.
class BusyPool {
public:
    /// Takes every pool thread and sets `busy` once all are taken; ends once
    /// release() has let them go. Cancelling it releases them too.
    task<> hold(event& busy) {
        const int threads = threadpool_size();
        std::atomic<int> taken = 0;
        auto notify = event_loop::current().create_relay();
        std::vector<task<>> works;
        for(int i = 0; i < threads; ++i) {
            works.push_back(queue(
                [&] {
                    if(taken.fetch_add(1) + 1 == threads) {
                        notify.send([&] { busy.set(); });
                    }
                    gate.wait();
                },
                [this] { release(); }));
        }
        co_await when_all(std::move(works));
    }

    /// Lets the pool threads go; calls after the first do nothing.
    void release() {
        if(!released.exchange(true)) {
            gate.count_down();
        }
    }

private:
    std::latch gate{1};
    std::atomic<bool> released = false;

    /// Threads in libuv's pool, read the way libuv reads UV_THREADPOOL_SIZE.
    static int threadpool_size() {
#ifdef _WIN32
        char* value = nullptr;
        std::size_t length = 0;
        if(_dupenv_s(&value, &length, "UV_THREADPOOL_SIZE") != 0 || value == nullptr) {
            return 4;
        }
        auto threads = static_cast<unsigned>(std::atoi(value));
        std::free(value);
#else
        const char* value = std::getenv("UV_THREADPOOL_SIZE");
        if(value == nullptr) {
            return 4;
        }
        auto threads = static_cast<unsigned>(std::atoi(value));
#endif
        return static_cast<int>(std::clamp(threads, 1U, 1024U));
    }
};

}  // namespace kota::test
