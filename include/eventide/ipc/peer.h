#pragma once

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "eventide/ipc/bincode_codec.h"
#include "eventide/ipc/codec.h"
#include "eventide/ipc/json_codec.h"
#include "eventide/ipc/transport.h"
#include "eventide/async/cancellation.h"
#include "eventide/async/loop.h"
#include "eventide/async/task.h"

namespace eventide::ipc {

template <typename Codec>
class Peer;

template <typename PeerT>
struct basic_request_context {
    std::string_view method{};
    protocol::RequestID id;
    PeerT& peer;
    cancellation_token cancellation = {};

    basic_request_context(PeerT& peer,
                          const protocol::RequestID& id,
                          cancellation_token token = {}) :
        id(id), peer(peer), cancellation(std::move(token)) {}

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
using RequestResult = task<Result<ResultT>>;

struct request_options {
    cancellation_token token = {};
    std::optional<std::chrono::milliseconds> timeout = std::nullopt;
};

template <typename Codec>
class Peer {
public:
    using RequestContext = basic_request_context<Peer>;

    Peer(event_loop& loop, std::unique_ptr<Transport> transport, Codec codec = {});

    Peer(const Peer&) = delete;
    Peer& operator=(const Peer&) = delete;
    Peer(Peer&&) = delete;
    Peer& operator=(Peer&&) = delete;

    ~Peer();

    task<> run();

    Result<void> close_output();

    template <typename Params>
    RequestResult<Params> send_request(const Params& params, request_options opts = {});

    template <typename ResultT, typename Params>
    task<Result<ResultT>> send_request(std::string_view method,
                                       const Params& params,
                                       request_options opts = {});

    template <typename Params>
    Result<void> send_notification(const Params& params);

    template <typename Params>
    Result<void> send_notification(std::string_view method, const Params& params);

    template <typename Callback>
    void on_request(Callback&& callback);

    template <typename Callback>
    void on_request(std::string_view method, Callback&& callback);

    template <typename Callback>
    void on_notification(Callback&& callback);

    template <typename Callback>
    void on_notification(std::string_view method, Callback&& callback);

private:
    template <typename Params, typename Callback>
    void bind_request_callback(std::string_view method, Callback&& callback);

    template <typename Params, typename Callback>
    void bind_notification_callback(std::string_view method, Callback&& callback);

    using RequestCallback = std::function<task<Result<std::string>>(const protocol::RequestID&,
                                                                    std::string_view,
                                                                    cancellation_token)>;
    using NotificationCallback = std::function<void(std::string_view)>;

    void register_request_callback(std::string_view method, RequestCallback callback);

    void register_notification_callback(std::string_view method, NotificationCallback callback);

    task<Result<std::string>> send_request_impl(std::string_view method,
                                                std::string params,
                                                request_options opts);

    Result<void> send_notification_impl(std::string_view method, std::string params);

    struct Self;
    std::unique_ptr<Self> self;
};

using JsonPeer = Peer<JsonCodec>;
using BincodePeer = Peer<BincodeCodec>;

using JsonRequestContext = JsonPeer::RequestContext;
using BincodeRequestContext = BincodePeer::RequestContext;

}  // namespace eventide::ipc

#define EVENTIDE_IPC_PEER_INL_FROM_HEADER
#include "eventide/ipc/peer.inl"
#undef EVENTIDE_IPC_PEER_INL_FROM_HEADER

namespace eventide::ipc {

extern template class Peer<JsonCodec>;
extern template class Peer<BincodeCodec>;

}  // namespace eventide::ipc
