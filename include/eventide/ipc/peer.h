#pragma once

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "eventide/ipc/codec.h"
#include "eventide/ipc/transport.h"
#include "eventide/async/cancellation.h"
#include "eventide/async/loop.h"
#include "eventide/async/task.h"

namespace eventide::ipc {

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

using RequestContext = basic_request_context<Peer>;

template <typename Params, typename ResultT = typename protocol::RequestTraits<Params>::Result>
using RequestResult = task<Result<ResultT>>;

class Peer {
public:
    Peer(event_loop& loop, std::unique_ptr<Transport> transport);
    Peer(event_loop& loop, std::unique_ptr<Transport> transport, std::unique_ptr<Codec> codec);

    Peer(const Peer&) = delete;
    Peer& operator=(const Peer&) = delete;
    Peer(Peer&&) = delete;
    Peer& operator=(Peer&&) = delete;

    ~Peer();

    task<> run();

    Result<void> close_output();

    template <typename Params>
    RequestResult<Params> send_request(const Params& params);

    template <typename Params>
    RequestResult<Params> send_request(const Params& params, cancellation_token token);

    template <typename Params>
    RequestResult<Params> send_request(const Params& params, std::chrono::milliseconds timeout);

    template <typename ResultT, typename Params>
    task<Result<ResultT>> send_request(std::string_view method, const Params& params);

    template <typename ResultT, typename Params>
    task<Result<ResultT>> send_request(std::string_view method,
                                       const Params& params,
                                       cancellation_token token);

    template <typename ResultT, typename Params>
    task<Result<ResultT>> send_request(std::string_view method,
                                       const Params& params,
                                       std::chrono::milliseconds timeout);

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

    task<Result<std::string>> send_request_impl(std::string_view method, std::string params);

    task<Result<std::string>> send_request_impl(std::string_view method,
                                                std::string params,
                                                cancellation_token token);

    task<Result<std::string>> send_request_impl(std::string_view method,
                                                std::string params,
                                                std::chrono::milliseconds timeout);

    Result<void> send_notification_impl(std::string_view method, std::string params);

private:
    struct Self;
    std::unique_ptr<Self> self;
};

}  // namespace eventide::ipc

#include "eventide/ipc/bincode_codec.h"
#include "eventide/ipc/json_codec.h"

namespace eventide::ipc {

inline auto make_json_peer(event_loop& loop, std::unique_ptr<Transport> transport)
    -> std::unique_ptr<Peer> {
    return std::make_unique<Peer>(loop, std::move(transport), make_json_codec());
}

inline auto make_bincode_peer(event_loop& loop, std::unique_ptr<Transport> transport)
    -> std::unique_ptr<Peer> {
    return std::make_unique<Peer>(loop, std::move(transport), make_bincode_codec());
}

}  // namespace eventide::ipc

#define EVENTIDE_IPC_PEER_INL_FROM_HEADER
#include "eventide/ipc/peer.inl"
#undef EVENTIDE_IPC_PEER_INL_FROM_HEADER
