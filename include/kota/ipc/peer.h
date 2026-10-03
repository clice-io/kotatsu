#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "kota/ipc/codec.h"
#include "kota/ipc/logger.h"
#include "kota/ipc/transport.h"
#include "kota/async/async.h"
#include "kota/codec/visit/common.h"

namespace kota::ipc {

template <typename Codec>
class Peer;

/// What a request handler knows of the request it answers.
template <typename PeerT>
struct basic_request_context {
    std::string_view method;
    protocol::RequestID id;
    PeerT& peer;
    /// Fires when the remote cancels the request or the peer closes.
    cancellation_token cancellation;

    basic_request_context(PeerT& peer,
                          std::string_view method,
                          const protocol::RequestID& id,
                          cancellation_token token) :
        method(method), id(id), peer(peer), cancellation(std::move(token)) {}

    bool cancelled() const noexcept {
        return cancellation.cancelled();
    }

    PeerT* operator->() noexcept {
        return &peer;
    }

    const PeerT* operator->() const noexcept {
        return &peer;
    }
};

template <typename Params, typename ResultT = typename protocol::RequestTraits<Params>::Result>
using RequestResult = task<ResultT, Error>;

struct request_options {
    /// Cancels the request: the remote is sent $/cancelRequest, and the
    /// request gives what the remote answers then, as it is: the result, if
    /// it finished first, or the error, as a rule RequestCancelled. A token
    /// that fired before the request is sent fails it at once with
    /// RequestCancelled, and nothing is sent.
    std::optional<cancellation_token> token = std::nullopt;
    /// How long the request waits for its answer, counted from the send, a
    /// cancel or not. Once it passes, the remote is sent $/cancelRequest, if
    /// it was not already, and the request fails with RequestCancelled; an
    /// answer that comes later is dropped.
    std::optional<std::chrono::milliseconds> timeout = std::nullopt;
};

/// One end of a JSON-RPC connection over a transport, in a codec's
/// encoding: it dispatches the requests and notifications it reads to their
/// handlers, answers the requests, and sends requests and notifications of
/// its own.
///
/// A handler returns RequestResult<Params> or, for a result it has encoded
/// itself, task<codec::RawValue, Error>; a request whose result type is
/// codec::RawValue gets the result as the codec wrote it.
///
/// A request's handler starts once the messages read with the request are
/// dispatched, so that a notification sent after the request reaches its
/// handler first, and a $/cancelRequest for it cancels it before it starts.
///
/// A request whose awaiting task is cancelled sends the remote
/// $/cancelRequest and waits for its answer, then ends cancelled: a cancelled
/// task ends once what it awaits has ended. A remote that ignores
/// $/cancelRequest and never answers keeps the canceller waiting until the
/// request's timeout, if it has one, or until the peer closes or its input
/// ends.
///
/// Nothing larger than the transport's max_payload() is written, since the
/// remote would skip it unread: a request or notification that large fails
/// with MessageTooLarge, and an answer that large is replaced by a
/// MessageTooLarge error. The limit is this end's, so both ends should use
/// the same one.
template <typename Codec>
class Peer {
public:
    using RequestContext = basic_request_context<Peer>;

    /// `transport` must not be null.
    Peer(event_loop& loop, std::unique_ptr<Transport> transport, Codec codec = {});

    Peer(const Peer&) = delete;
    Peer& operator=(const Peer&) = delete;
    Peer(Peer&&) = delete;
    Peer& operator=(Peer&&) = delete;

    /// run(), if it was called, has returned: the Peer must outlive it.
    ~Peer();

    /// Reads and dispatches messages and writes what is sent, until the input
    /// ends and every handler has finished, or until close(). Every pending
    /// request has failed by the time it returns. Called once.
    ///
    /// The Peer must outlive it: destroy the Peer only once run() has
    /// returned, cancelled or not, as an owner that awaits run() and then
    /// lets the Peer go does. A debug build asserts it.
    task<> run();

    /// Shuts the peer down: cancels the running handlers, fails pending
    /// requests with ConnectionClosed, discards queued messages and closes
    /// the transport, so that run() returns. Later sends fail; calls after
    /// the first do nothing.
    Result<void> close();

    /// Half-closes: what is queued is still written, then the transport's
    /// output closes, which the remote reads as the end of its input. Sends
    /// fail from now on; the input stays open, so handlers keep running, but
    /// their answers are dropped.
    void close_output();

    void set_logger(LogCallback callback, LogLevel min_level = LogLevel::info);

    /// Sends the request RequestTraits<Params> names.
    template <typename Params>
    RequestResult<Params> send_request(const Params& params, request_options opts = {});

    template <typename ResultT, typename Params>
    task<ResultT, Error> send_request(std::string_view method,
                                      const Params& params,
                                      request_options opts = {});

    /// Sends the notification NotificationTraits<Params> names.
    template <typename Params>
    Result<void> send_notification(const Params& params);

    template <typename Params>
    Result<void> send_notification(std::string_view method, const Params& params);

    /// Handles the requests RequestTraits of the callback's params names;
    /// the callback is `(RequestContext&, const Params&) -> RequestResult<Params>`.
    template <typename Callback>
    void on_request(Callback&& callback);

    template <typename Callback>
    void on_request(std::string_view method, Callback&& callback);

    /// Handles the notifications NotificationTraits of the callback's params
    /// names; the callback is `(const Params&) -> void`.
    template <typename Callback>
    void on_notification(Callback&& callback);

    template <typename Callback>
    void on_notification(std::string_view method, Callback&& callback);

private:
    task<std::string, Error> send_request_impl(std::string_view method,
                                               std::string params,
                                               request_options opts);

    Result<void> send_notification_impl(std::string_view method, std::string params);

    /// Register a callback whose signature the caller has checked.
    template <typename Callback>
    void on_request_impl(std::string_view method, Callback&& callback);

    template <typename Callback>
    void on_notification_impl(std::string_view method, Callback&& callback);

    struct Self;
    std::unique_ptr<Self> self;
};

}  // namespace kota::ipc

#define KOTA_IPC_PEER_INL_FROM_HEADER
#include "kota/ipc/peer.inl"
#undef KOTA_IPC_PEER_INL_FROM_HEADER
