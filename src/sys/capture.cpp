#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>

#include "child.h"
#include "os.h"
#include "kota/sys/spawn.h"

#ifndef _WIN32
#include <cerrno>
#include <poll.h>
#endif

namespace kota::sys {

namespace {

/// What one read of a pipe asks for at most.
constexpr std::size_t chunk = 64 * 1024;

#ifdef _WIN32

/// A pipe of the child read into a buffer, a read at a time, through
/// overlapped I/O: the pipes are opened for it, as an event loop takes them.
struct Reader {
    Handle pipe;

    buffer_ref sink;

    Handle event{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};

    OVERLAPPED overlapped{};

    /// What the read under way writes, appended to `sink` once it is done:
    /// the other reader may resize the same buffer meanwhile.
    std::unique_ptr<char[]> bytes = std::make_unique_for_overwrite<char[]>(chunk);

    bool pending = false;

    /// Starts the next read: an error when it cannot.
    std::error_code start() {
        overlapped = {};
        overlapped.hEvent = event.handle;
        // One that completes at once signals the event as well.
        if(::ReadFile(pipe.handle, bytes.get(), chunk, nullptr, &overlapped) ||
           ::GetLastError() == ERROR_IO_PENDING) {
            pending = true;
            return {};
        }
        return end(::GetLastError());
    }

    /// Takes in the read that completed, and starts the next.
    std::error_code finish() {
        pending = false;
        DWORD count = 0;
        if(!::GetOverlappedResult(pipe.handle, &overlapped, &count, FALSE)) {
            return end(::GetLastError());
        }
        sink.append(std::string_view(bytes.get(), count));
        return start();
    }

    /// The end of the pipe, or the error that ended a read early.
    static std::error_code end(DWORD error) {
        return error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF
                   ? std::error_code()
                   : fs::windows::to_error_code(error);
    }

    ~Reader() {
        // The read under way writes into `bytes` until it is done.
        if(pending) {
            ::CancelIoEx(pipe.handle, &overlapped);
            DWORD count = 0;
            ::GetOverlappedResult(pipe.handle, &overlapped, &count, TRUE);
        }
    }
};

std::error_code read_both(Child& child, buffer_ref out, buffer_ref err) {
    std::array<Reader, 2> readers = {
        Reader{.pipe = Handle(child.take_pipe(1)), .sink = out},
        Reader{.pipe = Handle(child.take_pipe(2)), .sink = err},
    };
    for(auto& reader: readers) {
        if(auto error = reader.start()) {
            return error;
        }
    }
    while(true) {
        std::array<HANDLE, 2> events;
        std::array<Reader*, 2> waiting;
        DWORD count = 0;
        for(auto& reader: readers) {
            if(reader.pending) {
                events[count] = reader.event.handle;
                waiting[count++] = &reader;
            }
        }
        if(count == 0) {
            return {};
        }
        auto woken = ::WaitForMultipleObjects(count, events.data(), FALSE, INFINITE);
        if(woken >= WAIT_OBJECT_0 + count) {
            return fs::windows::last_error();
        }
        if(auto error = waiting[woken - WAIT_OBJECT_0]->finish()) {
            return error;
        }
    }
}

#else

std::error_code read_both(Child& child, buffer_ref out, buffer_ref err) {
    std::array<pollfd, 2> polls = {
        pollfd{.fd = child.take_pipe(1), .events = POLLIN, .revents = 0},
        pollfd{.fd = child.take_pipe(2), .events = POLLIN, .revents = 0},
    };
    std::array<buffer_ref, 2> sinks = {out, err};

    // The pipes close when this goes, read to their end or not.
    struct Closer {
        std::array<pollfd, 2>& polls;

        ~Closer() {
            for(auto& poll: polls) {
                if(poll.fd >= 0) {
                    ::close(poll.fd);
                }
            }
        }
    } closer{polls};

    auto bytes = std::make_unique_for_overwrite<char[]>(chunk);
    while(polls[0].fd >= 0 || polls[1].fd >= 0) {
        if(::poll(polls.data(), polls.size(), -1) < 0) {
            if(errno == EINTR) {
                continue;
            }
            return errno_code();
        }
        for(std::size_t i = 0; i < polls.size(); ++i) {
            if(polls[i].fd < 0 || polls[i].revents == 0) {
                continue;
            }
            auto count = ::read(polls[i].fd, bytes.get(), chunk);
            if(count > 0) {
                sinks[i].append(std::string_view(bytes.get(), static_cast<std::size_t>(count)));
                continue;
            }
            if(count < 0) {
                if(errno == EINTR) {
                    continue;
                }
                return errno_code();
            }
            ::close(std::exchange(polls[i].fd, -1));
        }
    }
    return {};
}

#endif

}  // namespace

std::expected<ExitStatus, std::error_code> capture(buffer_ref out,
                                                   buffer_ref err,
                                                   SpawnOptions options) {
    options.streams = {Stdio::ignore(), Stdio::pipe(), Stdio::pipe()};
    auto child = spawn(options);
    if(!child) {
        return std::unexpected(child.error());
    }

    // Ends the child unless its streams were read to their end, and its
    // group with it when it leads one: what it started may hold the pipes.
    struct Ender {
        Child& child;

        bool group;

        bool read = false;

        ~Ender() {
            if(read) {
                return;
            }
            [[maybe_unused]] auto killed = group ? child.kill_tree() : child.kill();
            [[maybe_unused]] auto reaped = child.wait();
        }
    } ender{*child, options.creation.process_group || options.creation.detached};

    out.clear();
    err.clear();
    if(auto error = read_both(*child, out, err)) {
        return std::unexpected(error);
    }
    ender.read = true;
    return child->wait();
}

}  // namespace kota::sys
