#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <span>
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

    /// Writes `payloads` in order, as write_message() writes each, until one
    /// fails; a transport that can sends them in one write.
    virtual task<void, Error> write_messages(std::span<const std::string> payloads);

    /// Ends the output once what was written has gone out; the remote reads
    /// the end of its input, and the input stays open.
    virtual task<void, Error> close_output() = 0;

    /// Closes both input and output, ending any pending read.
    virtual Result<void> close() = 0;

    /// The largest payload the remote reads: Peer sends no larger one. No
    /// limit unless the transport was told one.
    virtual std::size_t remote_max_payload() const noexcept {
        return std::numeric_limits<std::size_t>::max();
    }
};

/// Messages framed as the LSP base protocol frames them, over streams. A
/// message whose payload is larger than `max_payload` is skipped. What it
/// sends has no limit, unless set_remote_max_payload() says how much the
/// remote reads.
class StreamTransport : public Transport {
public:
    StreamTransport(stream input, stream output, std::size_t max_payload = default_max_payload);

    explicit StreamTransport(stream stream, std::size_t max_payload = default_max_payload);

    /// Over the process's stdin and stdout. stdin must be a pipe, a console
    /// or a socket: a file, a device or anything else fails. stdout may be a
    /// file or a device too, except on Windows, which opens a pipe's handle
    /// only.
    static Result<std::unique_ptr<StreamTransport>>
        open_stdio(event_loop& loop, std::size_t max_payload = default_max_payload);

    static task<std::unique_ptr<StreamTransport>, Error>
        connect_tcp(std::string_view host,
                    int port,
                    event_loop& loop,
                    std::size_t max_payload = default_max_payload);

    task<std::string, ReadError> read_message() override;

    task<void, Error> write_message(std::string_view payload) override;

    /// Frames the payloads as one write, or as few as the stream takes.
    task<void, Error> write_messages(std::span<const std::string> payloads) override;

    /// The remote reads the end of its input. A transport over one stream
    /// (connect_tcp) shuts its write side down, and goes on reading. Over
    /// stdio (open_stdio), the process's stdout is pointed at the null
    /// device, as closing a stream over it leaves it open; the remote reads
    /// the end once nothing else holds it, such as a child that inherited it,
    /// a stderr sharing it, or stdin when both are one socket.
    task<void, Error> close_output() override;

    Result<void> close() override;

    /// Has a Peer over it send no payload larger than `bytes`, as much as the
    /// remote reads: over a link between two StreamTransports, the
    /// `max_payload` the other end reads with. A remote with no limit, such
    /// as an editor, needs none.
    void set_remote_max_payload(std::size_t bytes) noexcept;

    std::size_t remote_max_payload() const noexcept override;

private:
    /// Writes each payload after its header, in as few writes as the stream
    /// takes, the payloads as they are.
    task<void, Error> write_frames(std::span<const std::string_view> payloads);

    /// Points stdout at the null device, once, if the output is stdout.
    Result<void> release_stdout();

    stream read_stream;
    stream write_stream;
    bool shared_stream = false;
    /// The output is the process's stdout (open_stdio).
    bool over_stdout = false;
    FrameParser parser;
    std::size_t remote_limit = std::numeric_limits<std::size_t>::max();
};

}  // namespace kota::ipc
