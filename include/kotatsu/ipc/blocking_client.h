#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

#include "kotatsu/ipc/codec.h"
#include "kotatsu/ipc/framing.h"

namespace kotatsu::ipc {

namespace detail {

/// A pipe or a socket, read and written in the frames of the LSP base
/// protocol, each call waiting until it is done; closed when this goes.
class BlockingPipe {
public:
#ifdef _WIN32
    using native_handle_type = void*;
#else
    using native_handle_type = int;
#endif

    /// A frame whose payload is larger than `max_payload` is skipped.
    BlockingPipe(native_handle_type handle, std::size_t max_payload) noexcept;

    BlockingPipe(BlockingPipe&& other) noexcept;

    BlockingPipe& operator=(BlockingPipe&&) = delete;

    ~BlockingPipe();

    /// Writes `payload` in a frame; what the system failed the write with.
    std::error_code write(std::string_view payload);

    /// The payload of the next frame: Closed once the input has ended or a
    /// read failed, else as FrameParser reads it.
    std::expected<std::string, ReadError> read();

private:
    native_handle_type handle;
#ifdef __APPLE__
    /// macOS would not have the socket's writes fail rather than raise
    /// SIGPIPE, as it will not once its remote has gone: they fail untried.
    bool raises_sigpipe = false;
#endif
    FrameParser parser;
    /// What the last read brought past the frame it ended.
    std::string unread;
};

/// What a message that is no answer to the request sent is, in words.
std::string not_the_answer(const IncomingMessage& message);

}  // namespace detail

/// Sends requests over a pipe or a socket and waits for each one's answer,
/// blocking the thread: for a program without an event loop, such as a
/// library loaded into another process, that asks a Peer. One request is
/// answered before the next is sent, so the remote writes nothing but the
/// answer to the request sent: anything else, or a read or a write that
/// fails, ends the conversation, and that request and every one after fail
/// with ConnectionClosed and what ended it.
template <typename Codec>
class BlockingClient {
public:
    using native_handle_type = detail::BlockingPipe::native_handle_type;

    /// Over `handle`, which it reads and writes, and closes when it goes: on
    /// Windows a pipe's HANDLE, open to read and write without
    /// FILE_FLAG_OVERLAPPED; elsewhere the descriptor of a connected stream
    /// socket, such as one to what kotatsu::async::pipe::listen() listens on, without
    /// O_NONBLOCK. A write to a remote that has gone fails, rather than
    /// raise SIGPIPE. An answer whose payload is larger than `max_payload`
    /// is skipped, as StreamTransport skips one.
    explicit BlockingClient(native_handle_type handle,
                            std::size_t max_payload = default_max_payload,
                            Codec codec = {}) :
        pipe(handle, max_payload), codec(std::move(codec)) {}

    /// Sends the request `method` with `params` and waits for its result.
    /// It fails with the error the remote answered, with MessageTooLarge
    /// for an answer over the limit, which ends nothing, with
    /// ConnectionClosed once the conversation has ended, or with what
    /// encoding the params or decoding the result failed with.
    template <typename ResultT, typename Params>
    Result<ResultT> send_request(std::string_view method, const Params& params) {
        if(ended) {
            return outcome_error(*ended);
        }
        protocol::RequestID id{next_id++};
        auto request = codec.encode_request(id, method, params);
        if(!request) {
            return outcome_error(std::move(request).error());
        }
        auto result = exchange(id, *request);
        if(!result) {
            return outcome_error(std::move(result).error());
        }
        return codec.template deserialize_value<ResultT>(*result);
    }

private:
    /// Writes `request`, the request `id`, and reads its answer: its result
    /// as the codec read it, or the error it was answered with.
    Result<PayloadSlice> exchange(const protocol::RequestID& id, std::string_view request) {
        if(auto failed = pipe.write(request)) {
            return outcome_error(end(failed.message()));
        }
        auto frame = pipe.read();
        if(!frame) {
            auto& failure = frame.error();
            // Skipped whole: the answer of this request fails it alone.
            if(failure.kind == ReadError::Kind::Oversized) {
                auto head = codec.peek(failure.prefix);
                if(head.kind == MessageHead::Kind::Response && head.id == id) {
                    return outcome_error(
                        Error(protocol::ErrorCode::MessageTooLarge, std::move(failure.message)));
                }
            }
            return outcome_error(end(std::move(failure.message)));
        }
        auto message = codec.parse_message(std::move(*frame));
        if(auto* answer = std::get_if<IncomingResponse>(&message); answer && answer->id == id) {
            return std::move(answer->result);
        }
        if(auto* refusal = std::get_if<IncomingErrorResponse>(&message);
           refusal && refusal->id == id && !refusal->unreadable) {
            return outcome_error(std::move(refusal->error));
        }
        return outcome_error(end(detail::not_the_answer(message)));
    }

    /// Ends the conversation for `why`, which the request that ended it and
    /// every later one fail with.
    Error end(std::string why) {
        ended = Error(protocol::ErrorCode::ConnectionClosed, std::move(why));
        return *ended;
    }

    detail::BlockingPipe pipe;
    Codec codec;
    std::int64_t next_id = 1;
    /// Why the conversation ended, once it has.
    std::optional<Error> ended;
};

}  // namespace kotatsu::ipc
