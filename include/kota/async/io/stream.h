#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "kota/async/io/endpoint.h"
#include "kota/async/io/loop.h"
#include "kota/async/runtime/task.h"
#include "kota/async/vocab/error.h"
#include "kota/async/vocab/owned.h"

namespace kota {

template <typename Stream>
class acceptor;

class tcp;

/// Stream handle classification for file descriptors.
enum class handle_type : std::uint8_t { unknown, file, tty, pipe, tcp, udp };

/// Guess the handle type for a file descriptor.
handle_type guess_handle(int fd);

/// What pipe, tcp and console share: a byte stream.
///
/// Reading is buffered: from the first read on, the stream reads ahead into
/// a 64 KiB buffer of its own until stop(), the end of the stream, or a full
/// buffer, and a full buffer is read into again once a reader has drained
/// it. The end of the stream or a read error is reported once the bytes
/// read before it are consumed, and from then on to every read. One read
/// may be pending at a time; a second fails with
/// error::resource_busy_or_locked. Cancelling a read only withdraws it: what
/// arrives stays buffered for the next one.
///
/// Writes may overlap: libuv sends them in the order they were made.
///
/// Destroying the stream ends a pending read, and the writes and the shutdown
/// that have not gone out yet, with error::operation_aborted.
///
/// A default-constructed or moved-from stream is inert: what can fail fails
/// with error::invalid_argument.
class stream {
public:
    stream() noexcept;

    stream(const stream&) = delete;
    stream& operator=(const stream&) = delete;

    stream(stream&& other) noexcept;
    stream& operator=(stream&& other) noexcept;

    ~stream();

    /// Reads what is buffered, waiting for data if nothing is.
    task<std::string, error> read();

    /// Reads up to dst.size() bytes into dst; returns how many, 0 at the end
    /// of the stream or for an empty dst.
    task<std::size_t, error> read_some(std::span<char> dst);

    using chunk = std::span<const char>;

    /// Shows buffered bytes, waiting for data if nothing is buffered; they
    /// stay buffered until consume() drops them.
    task<chunk, error> read_chunk();

    /// Drops the first `n` buffered bytes; `n` must not exceed what
    /// read_chunk() showed.
    void consume(std::size_t n);

    /// Stops reading ahead and ends a pending read with
    /// error::operation_aborted. What is buffered stays; the next read
    /// starts reading again.
    error stop();

    /// Writes `data`, which must stay alive until the write completes. A
    /// cancelled write still goes out: libuv cannot take it back, and the
    /// task ends once it has. A write goes out whole: empty data fails with
    /// error::invalid_argument, and more than 4 GiB - 1 bytes with
    /// error::value_too_large_for_defined_data_type.
    task<void, error> write(std::span<const char> data);

    /// Writes what fits without waiting; returns how much that was, 0 for
    /// empty data.
    result<std::size_t> try_write(std::span<const char> data);

    /// Shuts the write side once the writes made before it have gone out:
    /// the peer reads them, then the end of the stream, and reading here
    /// goes on until the peer's own end. From the call on, write() fails
    /// with error::broken_pipe and shutdown() with
    /// error::socket_is_not_connected. Like a write, a cancelled shutdown
    /// still happens, and the task ends once it has.
    ///
    /// libuv emulates this for pipes on Windows, which cannot be half
    /// closed: it closes a pipe that only writes at once, and one that also
    /// reads once a read here has waited 50 ms for data; the peer cannot
    /// answer after that.
    task<void, error> shutdown();

    /// Whether the stream can be read from; false for an inert one.
    bool readable() const noexcept;

    /// Whether the stream can be written to; false for an inert one, and
    /// from shutdown() on.
    bool writable() const noexcept;

    /// Enable or disable blocking I/O on the stream.
    error set_blocking(bool enabled);

protected:
    struct Self;

    explicit stream(detail::unique_handle<Self> self) noexcept;

    detail::unique_handle<Self> self;

private:
    template <typename Stream>
    friend class acceptor;
};

/// A listener that hands out the connections it receives.
///
/// Connections that arrive while nobody accepts wait in the listen backlog.
/// One accept() may be pending at a time; a second fails with
/// error::resource_busy_or_locked. Cancelling an accept only withdraws it.
/// Destroying the acceptor ends a pending accept with
/// error::operation_aborted.
template <typename Stream>
class acceptor {
public:
    acceptor() noexcept;

    acceptor(const acceptor&) = delete;
    acceptor& operator=(const acceptor&) = delete;

    acceptor(acceptor&& other) noexcept;
    acceptor& operator=(acceptor&& other) noexcept;

    ~acceptor();

    /// Accepts the next connection.
    task<Stream, error> accept();

    /// Ends a pending accept() with error::operation_aborted; the listener
    /// goes on listening for the next accept().
    error stop();

    /// The address and port the listener is bound to.
    result<endpoint> getsockname() const
        requires std::same_as<Stream, tcp>;

private:
    friend class pipe;
    friend class tcp;

    struct Self;

    explicit acceptor(detail::unique_handle<Self> self) noexcept;

    detail::unique_handle<Self> self;
};

/// Pipe/socket wrapper (named pipe on Windows, Unix domain socket on Unix).
class pipe : public stream {
public:
    pipe() noexcept = default;

    using acceptor = kota::acceptor<pipe>;

    struct options {
        /// Enable IPC handle passing.
        bool ipc = false;

        /// Fail with invalid_argument instead of truncating a Unix socket
        /// path longer than sun_path; Windows never truncates pipe names.
        bool no_truncate = false;

        /// Listen backlog size.
        int backlog = 128;
    };

    // The functions taking options come in pairs: a nested struct with
    // default member initializers cannot be a default argument within its
    // enclosing class.

    /// Wrap an existing file descriptor. One the loop cannot watch, such as a
    /// regular file or /dev/null, fails with
    /// error::socket_operation_on_non_socket: on Windows any that is no pipe,
    /// on Linux one open for reading. On macOS libuv reads it through a
    /// thread of its own.
    static result<pipe> open(int fd, event_loop& loop = event_loop::current());

    static result<pipe> open(int fd, options opts, event_loop& loop = event_loop::current());

    /// Connect to a named pipe.
    static task<pipe, error> connect(std::string_view name,
                                     event_loop& loop = event_loop::current());

    static task<pipe, error> connect(std::string_view name,
                                     options opts,
                                     event_loop& loop = event_loop::current());

    /// Listen on a named pipe.
    static result<acceptor> listen(std::string_view name, event_loop& loop = event_loop::current());

    static result<acceptor> listen(std::string_view name,
                                   options opts,
                                   event_loop& loop = event_loop::current());

private:
    friend class kota::acceptor<pipe>;
    friend class process;

    explicit pipe(detail::unique_handle<Self> self) noexcept;

    static pipe create(options opts, event_loop& loop);
};

/// TCP socket wrapper. Hosts are numeric IPv4 or IPv6 addresses; names are
/// not looked up.
class tcp : public stream {
public:
    tcp() noexcept = default;

    using acceptor = kota::acceptor<tcp>;

    struct options {
        /// Restrict socket to IPv6 only (ignore IPv4-mapped addresses).
        bool ipv6_only = false;

        /// Enable SO_REUSEPORT when supported.
        bool reuse_port = false;

        /// Listen backlog size.
        int backlog = 128;
    };

    /// Wrap an existing socket descriptor.
    static result<tcp> open(int fd, event_loop& loop = event_loop::current());

    /// Connect to a TCP host/port.
    static task<tcp, error> connect(std::string_view host,
                                    int port,
                                    event_loop& loop = event_loop::current());

    /// Listen on a TCP host/port.
    static result<acceptor> listen(std::string_view host,
                                   int port,
                                   event_loop& loop = event_loop::current());

    static result<acceptor> listen(std::string_view host,
                                   int port,
                                   options opts,
                                   event_loop& loop = event_loop::current());

private:
    friend class kota::acceptor<tcp>;

    explicit tcp(detail::unique_handle<Self> self) noexcept;

    static tcp create(event_loop& loop);
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

    enum class mode : std::uint8_t { normal, raw, io, raw_vt };

    enum class vterm_state : std::uint8_t { supported, unsupported };

    struct options {
        /// Whether the TTY is readable (stdin).
        bool readable = false;
    };

    /// Wrap a console file descriptor.
    static result<console> open(int fd, event_loop& loop = event_loop::current());

    static result<console> open(int fd, options opts, event_loop& loop = event_loop::current());

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
    explicit console(detail::unique_handle<Self> self) noexcept;
};

}  // namespace kota
