#include "kota/ipc/transport.h"

#include <cerrno>
#include <fcntl.h>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace kota::ipc {

namespace {

/// `opened` as a stream, or its error as an ipc error.
template <typename Handle>
Result<stream> as_stream(result<Handle> opened) {
    if(!opened) {
        return outcome_error(Error(std::string(opened.error().message())));
    }
    return stream(std::move(*opened));
}

Result<stream> open_stdio_stream(int fd, bool readable, event_loop& loop) {
    switch(guess_handle(fd)) {
        case handle_type::tty:
            return as_stream(console::open(fd, console::options{readable}, loop));
        case handle_type::pipe:
        case handle_type::file:
        case handle_type::unknown: return as_stream(pipe::open(fd, pipe::options{}, loop));
        case handle_type::tcp: return as_stream(tcp::open(fd, loop));
        default: return outcome_error(Error("unsupported stdio handle type"));
    }
}

}  // namespace

StreamTransport::StreamTransport(stream input, stream output, std::size_t max_payload) :
    read_stream(std::move(input)), write_stream(std::move(output)), parser(max_payload) {}

StreamTransport::StreamTransport(stream stream, std::size_t max_payload) :
    read_stream(std::move(stream)), shared_stream(true), parser(max_payload) {}

Result<std::unique_ptr<StreamTransport>> StreamTransport::open_stdio(event_loop& loop,
                                                                     std::size_t max_payload) {
    auto input = open_stdio_stream(0, true, loop);
    if(!input) {
        return outcome_error(input.error());
    }

    auto output = open_stdio_stream(1, false, loop);
    if(!output) {
        return outcome_error(output.error());
    }

    auto transport =
        std::make_unique<StreamTransport>(std::move(*input), std::move(*output), max_payload);
    transport->over_stdout = true;
    return transport;
}

task<std::unique_ptr<StreamTransport>, Error>
    StreamTransport::connect_tcp(std::string_view host,
                                 int port,
                                 event_loop& loop,
                                 std::size_t max_payload) {
    auto connected = co_await tcp::connect(host, port, loop);
    co_return std::make_unique<StreamTransport>(co_await or_fail(as_stream(std::move(connected))),
                                                max_payload);
}

task<std::string, ReadError> StreamTransport::read_message() {
    while(true) {
        auto chunk = co_await read_stream.read_chunk();
        if(!chunk) {
            co_await fail(ReadError{
                .kind = ReadError::Kind::Closed,
                .message = std::string(chunk.error().message()),
            });
        }

        auto step = parser.feed(std::string_view(chunk->data(), chunk->size()));
        read_stream.consume(step.consumed);
        if(!step.frame) {
            continue;
        }
        if(!*step.frame) {
            co_await fail(std::move(*step.frame).error());
        }
        co_return std::move(**step.frame);
    }
}

task<void, Error> StreamTransport::write_message(std::string_view payload) {
    auto framed = frame(payload);
    auto& stream = shared_stream ? read_stream : write_stream;
    auto status = co_await stream.write(std::span<const char>(framed.data(), framed.size()));
    if(status.has_error()) {
        co_await fail(std::string(status.error().message()));
    }
}

Result<void> StreamTransport::close_output() {
    if(shared_stream) {
        return close();
    }

    write_stream = stream{};
    return release_stdout();
}

// Stopping the read may resume the read loop at once, which can end the
// peer's run() and let its owner destroy the peer and this transport: the
// read stream is moved out first, and the stop comes last. The write stream
// goes before stdout is released: closing it takes fd 1 out of the loop's
// poll set by number, which would no longer find it once fd 1 is the null
// device, and the pipe left in the set would wake the loop for good.
// Destroying a stream resumes nothing at once; its close callbacks come
// later.
Result<void> StreamTransport::close() {
    auto reading = std::move(read_stream);
    if(shared_stream) {
        reading.stop();
        return {};
    }
    write_stream = stream{};
    auto released = release_stdout();
    reading.stop();
    return released;
}

// libuv never closes fds 0 to 2 when it closes a stream over one (on Windows
// it closes a duplicate of the handle), so the pipe or file behind stdout
// stays open until fd 1 lets go of it: pointing fd 1 at the null device does.
Result<void> StreamTransport::release_stdout() {
    if(!over_stdout) {
        return {};
    }
    over_stdout = false;
    // Not inherited by a child spawned meanwhile by another thread; dup2
    // leaves fd 1 inheritable, as stdout is.
#ifdef _WIN32
    const int null = _open("NUL", _O_WRONLY | _O_NOINHERIT);
#else
    const int null = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
#endif
    if(null < 0) {
        return outcome_error(
            Error("opening the null device failed: " + std::generic_category().message(errno)));
    }
#ifdef _WIN32
    const int replaced = _dup2(null, 1);
    const int error = errno;
    _close(null);
#else
    const int replaced = ::dup2(null, 1);
    const int error = errno;
    ::close(null);
#endif
    if(replaced < 0) {
        return outcome_error(
            Error("releasing stdout failed: " + std::generic_category().message(error)));
    }
    return {};
}

}  // namespace kota::ipc
