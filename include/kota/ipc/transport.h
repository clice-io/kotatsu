#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "kota/ipc/codec.h"
#include "kota/ipc/framing.h"
#include "kota/async/async.h"

namespace kota::ipc {

/// Carries whole messages between a Peer and its remote.
class Transport {
public:
    virtual ~Transport() = default;

    /// The next message. A message too large to read is an Oversized error,
    /// after which reading goes on; Closed and Malformed end the input.
    virtual task<std::string, ReadError> read_message() = 0;

    virtual task<void, Error> write_message(std::string_view payload) = 0;

    /// Ends the output; the remote reads the end of its input.
    virtual Result<void> close_output() = 0;

    /// Closes both input and output, ending any pending read.
    virtual Result<void> close() = 0;
};

/// Messages framed as the LSP base protocol frames them, over streams. A
/// message whose payload is larger than `max_payload` is skipped.
class StreamTransport : public Transport {
public:
    StreamTransport(stream input, stream output, std::size_t max_payload = default_max_payload);

    explicit StreamTransport(stream stream, std::size_t max_payload = default_max_payload);

    static Result<std::unique_ptr<StreamTransport>>
        open_stdio(event_loop& loop, std::size_t max_payload = default_max_payload);

    static task<std::unique_ptr<StreamTransport>, Error>
        connect_tcp(std::string_view host,
                    int port,
                    event_loop& loop,
                    std::size_t max_payload = default_max_payload);

    task<std::string, ReadError> read_message() override;

    task<void, Error> write_message(std::string_view payload) override;

    /// The remote reads the end of its input. A transport over one stream
    /// (connect_tcp) cannot close only one direction of it: there this is
    /// close(), and the input ends too.
    Result<void> close_output() override;

    Result<void> close() override;

private:
    stream read_stream;
    stream write_stream;
    bool shared_stream = false;
    FrameParser parser;
};

}  // namespace kota::ipc
