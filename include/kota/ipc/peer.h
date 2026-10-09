#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

/// A request the peer has read and not answered yet, as
/// Peer::incoming_requests() lists it.
struct UnansweredRequest {
    /// The method its handler was registered for; it lasts as long as the
    /// Peer.
    std::string_view method;
    protocol::RequestID id;
};

template <typename Params, typename ResultT = typename protocol::RequestTraits<Params>::Result>
using RequestResult = task<ResultT, Error>;

struct request_options {
    /// Once it fires, the remote is sent $/cancelRequest, and the request
    /// ends with the remote's answer: usually RequestCancelled, or the result
    /// it had already. A cancel of the task awaiting the request does the
    /// same, and that task ends cancelled once the answer is in. A remote
    /// that never answers keeps the request waiting until the connection
    /// closes or the timeout passes; one that cannot be told, the
    /// $/cancelRequest failing to be sent, ends it with RequestCancelled at
    /// once.
    cancellation_token token = {};
    /// Ends the request with RequestCancelled once it has waited this long,
    /// a cancel or not, and sends the remote $/cancelRequest; it counts from
    /// the send, and ends a request while run() runs.
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
/// A request is handled in two steps. First the peer calls the handler as it
/// dispatches the request, in the order the messages were read, notifications
/// included: what the handler does before it returns sees no message read
/// after the request, so that is where it takes what its answer should
/// reflect, such as the version of a document. Then the task it returned
/// starts, once the messages read together with the request are dispatched,
/// in the order the requests were read. The task sees a change read with its
/// request, and a $/cancelRequest read with it, or close(), keeps it from
/// starting; a message read later may come once the task has started. A
/// coroutine handler returns before any of its body runs, so all of it runs
/// in the second step.
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

    ~Peer();

    /// Reads and dispatches messages and writes what is sent, until the input
    /// ends and every handler has finished, or until close(). Every pending
    /// request has failed by the time it returns. Called once. The Peer must
    /// outlive it: destroying the Peer before run() has ended is undefined,
    /// and a debug build asserts. A request still pending when the Peer goes
    /// fails with ConnectionClosed.
    task<> run();

    /// Shuts the peer down: fails pending requests, cancels the running
    /// handlers, discards queued messages and closes the transport, so that
    /// run() returns. Later sends fail; calls after the first do nothing.
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
    /// the callback is `(RequestContext&, const Params&) -> RequestResult<Params>`,
    /// called and its task started in the two steps the class comment
    /// describes. The task may refer to the context and the params, which
    /// outlive it, but not to the callback's locals: a capturing lambda
    /// coroutine made in the callback is returned as `co_invoke(lambda)`, not
    /// `lambda()`. A callback that is no coroutine fails with
    /// `return outcome_error(...)`.
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

    /// The requests read and not answered yet, in no particular order. A
    /// request is listed while its handler answers it: from its dispatch,
    /// before the handler is called, until the task the handler returned has
    /// ended and its answer is queued, or dropped when the output takes no
    /// more. So a handler finds its own request listed, and a request off the
    /// list has its answer queued ahead of anything sent after. A
    /// $/cancelRequest leaves a request listed until its handler ends; one
    /// answered at once without its handler, such as a duplicate id, is never
    /// listed; close() or a failure of the output empties the list.
    std::vector<UnansweredRequest> incoming_requests() const;

private:
    /// Sends `request`, the encoded request `id`, and waits for its result.
    task<PayloadSlice, Error> send_request_impl(protocol::RequestID id,
                                                std::string request,
                                                request_options opts);

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
