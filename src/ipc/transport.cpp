#include "kota/ipc/transport.h"

#include <span>
#include <string>
#include <string_view>
#include <utility>

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

    return std::make_unique<StreamTransport>(std::move(*input), std::move(*output), max_payload);
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
    return {};
}

Result<void> StreamTransport::close() {
    read_stream.stop();
    read_stream = stream{};
    if(!shared_stream) {
        write_stream = stream{};
    }
    return {};
}

}  // namespace kota::ipc
