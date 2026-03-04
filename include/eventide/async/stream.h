#pragma once

#include <coroutine>
#include <cstddef>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "error.h"
#include "owned.h"
#include "task.h"

namespace eventide {

class event_loop;

template <typename Stream>
class acceptor;

/// Stream handle classification for file descriptors.
enum class handle_type { unknown, file, tty, pipe, tcp, udp };

/// Guess the handle type for a file descriptor.
handle_type guess_handle(int fd);

/// Base stream wrapper for pipe, TCP, and console handles.
class stream {
public:
    stream() noexcept;

    stream(const stream&) = delete;
    stream& operator=(const stream&) = delete;

    stream(stream&& other) noexcept;
    stream& operator=(stream&& other) noexcept;

    ~stream();

    struct Self;
    Self* operator->() noexcept;

    /// Raw libuv handle pointer, or nullptr if invalid.
    void* handle() noexcept;

    /// Raw libuv handle pointer, or nullptr if invalid.
    const void* handle() const noexcept;

    /// Read available data into a std::string; waits for at least one read if empty.
    task<result<std::string>> read();

    /// Read up to dst.size() bytes into dst; returns bytes read, 0 on EOF, or an error.
    task<result<std::size_t>> read_some(std::span<char> dst);

    using chunk = std::span<const char>;

    /// Read a chunk view into the internal buffer; call consume() after processing.
    task<result<chunk>> read_chunk();

    /// Consume bytes from the internal buffer.
    void consume(std::size_t n);

    /// Write data to the stream; only one writer at a time.
    task<error> write(std::span<const char> data);

    /// Try a non-blocking write; returns bytes written or error.
    result<std::size_t> try_write(std::span<const char> data);

    /// Check whether the stream is readable.
    bool readable() const noexcept;

    /// Check whether the stream is writable.
    bool writable() const noexcept;

    /// Enable or disable blocking I/O on the stream.
    error set_blocking(bool enabled);

protected:
    explicit stream(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

template <typename Stream>
class acceptor {
public:
    acceptor() noexcept;

    acceptor(const acceptor&) = delete;
    acceptor& operator=(const acceptor&) = delete;

    acceptor(acceptor&& other) noexcept;
    acceptor& operator=(acceptor&& other) noexcept;

    ~acceptor();

    struct Self;
    /// Internal access; null when invalid.
    Self* operator->() noexcept;

    /// Accept one connection; only one pending accept is allowed at a time.
    task<result<Stream>> accept();

    /// Stop pending accept which will complete with error::operation_aborted. If no accept is
    /// pending, the next accept() will complete with error instead.
    error stop();

private:
    friend class pipe;
    friend class tcp_socket;

    explicit acceptor(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

/// Pipe/socket wrapper (named pipe on Windows, Unix domain socket on Unix).
class pipe : public stream {
public:
    pipe() noexcept = default;

    using acceptor = eventide::acceptor<pipe>;

    struct options {
        /// Enable IPC handle passing.
        bool ipc = false;

        /// Do not truncate long pipe names (UV_PIPE_NO_TRUNCATE when supported).
        bool no_truncate = false;

        /// Listen backlog size.
        int backlog = 128;

        constexpr options(bool ipc = false, bool no_truncate = false, int backlog = 128) :
            ipc(ipc), no_truncate(no_truncate), backlog(backlog) {}
    };

    /// Wrap an existing file descriptor.
    static result<pipe> open(int fd,
                             options opts = options(),
                             event_loop& loop = event_loop::current());

    /// Connect to a named pipe.
    static task<result<pipe>> connect(std::string_view name,
                                      options opts = options(),
                                      event_loop& loop = event_loop::current());

    /// Listen on a named pipe.
    static result<acceptor> listen(std::string_view name,
                                   options opts = options(),
                                   event_loop& loop = event_loop::current());

    explicit pipe(unique_handle<Self> self) noexcept;

private:
    friend class process;

    static result<pipe> create(options opts = options(), event_loop& loop = event_loop::current());
};

/// TCP socket wrapper.
class tcp_socket : public stream {
public:
    tcp_socket() noexcept = default;

    explicit tcp_socket(unique_handle<Self> self) noexcept;

    using acceptor = eventide::acceptor<tcp_socket>;

    struct options {
        /// Restrict socket to IPv6 only (ignore IPv4-mapped addresses).
        bool ipv6_only = false;

        /// Enable SO_REUSEPORT when supported.
        bool reuse_port = false;

        /// Listen backlog size.
        int backlog = 128;

        constexpr options(bool ipv6_only = false, bool reuse_port = false, int backlog = 128) :
            ipv6_only(ipv6_only), reuse_port(reuse_port), backlog(backlog) {}
    };

    /// Wrap an existing socket descriptor.
    static result<tcp_socket> open(int fd, event_loop& loop = event_loop::current());

    /// Connect to a TCP host/port.
    static task<result<tcp_socket>> connect(std::string_view host,
                                            int port,
                                            event_loop& loop = event_loop::current());

    /// Listen on a TCP host/port.
    static result<acceptor> listen(std::string_view host,
                                   int port,
                                   options opts = options(),
                                   event_loop& loop = event_loop::current());
};

/// TTY/console wrapper.
class console : public stream {
public:
    console() noexcept = default;

    struct winsize {
        /// Console width in columns.
        int width = 0;

        /// Console height in rows.
        int height = 0;
    };

    enum class mode { normal, raw, io, raw_vt };

    enum class vterm_state { supported, unsupported };

    struct options {
        /// Whether the TTY is readable (stdin).
        bool readable = false;

        constexpr options(bool readable = false) : readable(readable) {}
    };

    /// Wrap a console file descriptor.
    static result<console> open(int fd,
                                options opts = options(),
                                event_loop& loop = event_loop::current());

    /// Set TTY/console mode.
    error set_mode(mode value);

    /// Reset TTY/console mode.
    static error reset_mode();

    /// Fetch terminal dimensions.
    result<winsize> get_winsize() const;

    /// Set global virtual terminal processing state.
    static void set_vterm_state(vterm_state state);

    /// Query global virtual terminal processing state.
    static result<vterm_state> get_vterm_state();

private:
    explicit console(unique_handle<Self> self) noexcept;
};

}  // namespace eventide
