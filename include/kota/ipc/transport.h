#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "kota/ipc/codec.h"
#include "kota/async/async.h"

namespace kota::ipc {

class Transport {
public:
    virtual ~Transport() = default;

    /// Read one framed message.
    ///
    /// An empty value means the peer ended the stream at a message boundary.
    /// A frame that cannot be delivered — truncated, malformed, or above the
    /// payload limit — is reported through the error channel instead, so a
    /// refused message stays distinguishable from a dead peer.
    virtual task<std::optional<std::string>, Error> read_message() = 0;

    virtual task<void, Error> write_message(std::string_view payload) = 0;

    virtual Result<void> close_output();

    /// Close both input and output, aborting any pending read.
    virtual Result<void> close();
};

class StreamTransport : public Transport {
public:
    /// Payload limit applied when none is configured.
    constexpr static std::size_t default_max_payload_bytes = 64 * 1024 * 1024;

    StreamTransport(stream input, stream output);
    explicit StreamTransport(stream stream);

    static Result<std::unique_ptr<StreamTransport>> open_stdio(event_loop& loop);

    static task<std::unique_ptr<StreamTransport>, Error> connect_tcp(std::string_view host,
                                                                     int port,
                                                                     event_loop& loop);

    static Result<std::unique_ptr<StreamTransport>> open_tcp(int fd, event_loop& loop);

    task<std::optional<std::string>, Error> read_message() override;

    task<void, Error> write_message(std::string_view payload) override;

    Result<void> close_output() override;

    Result<void> close() override;

    /// Largest payload accepted from a single frame; a frame announcing more
    /// fails the read instead of being buffered. Raise it before the first
    /// read_message() when the protocol carries bigger messages.
    std::size_t max_payload_bytes = default_max_payload_bytes;

private:
    stream read_stream;
    stream write_stream;
    bool shared_stream = false;
};

}  // namespace kota::ipc
