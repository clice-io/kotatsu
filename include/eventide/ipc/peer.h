#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "eventide/ipc/codec.h"
#include "eventide/ipc/codec/bincode.h"
#if ETD_IPC_ENABLE_JSON
#include "eventide/ipc/codec/json.h"
#endif
#include "eventide/ipc/logger.h"
#include "eventide/ipc/transport.h"
#include "eventide/async/async.h"

namespace eventide::ipc {

template <typename Codec>
class Peer;

namespace detail {

template <typename Tag>
constexpr bool has_tag_request_traits_v = requires {
    typename protocol::RequestTraits<Tag>::Params;
    typename protocol::RequestTraits<Tag>::Result;
    protocol::RequestTraits<Tag>::method;
};

template <typename Tag>
constexpr bool has_tag_notification_traits_v = requires {
    typename protocol::NotificationTraits<Tag>::Params;
    protocol::NotificationTraits<Tag>::method;
};

}  // namespace detail

template <typename PeerT>
struct basic_request_context {
    std::string_view method{};
    protocol::RequestID id;
    PeerT& peer;
    cancellation_token cancellation;

    basic_request_context(PeerT& peer, const protocol::RequestID& id, cancellation_token token) :
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
using RequestResult = task<ResultT, Error>;

struct request_options {
    std::optional<cancellation_token> token = std::nullopt;
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

    /// Gracefully shut down the peer: cancel in-flight incoming requests,
    /// fail pending outgoing requests, discard queued messages, and close
    /// the transport so that run() exits.
    Result<void> close();

    Result<void> close_output();

    void set_logger(LogCallback callback, LogLevel min_level = LogLevel::info);

    template <typename Params>
    RequestResult<Params> send_request(const Params& params, request_options opts = {});

    template <typename ResultT, typename Params>
    task<ResultT, Error> send_request(std::string_view method,
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

    template <typename Tag>
        requires detail::has_tag_request_traits_v<Tag>
    auto send_request(const typename protocol::RequestTraits<Tag>::Params& params,
                      request_options opts = {})
        -> task<typename protocol::RequestTraits<Tag>::Result, Error>;

    template <typename Tag>
        requires detail::has_tag_notification_traits_v<Tag>
    Result<void>
        send_notification(const typename protocol::NotificationTraits<Tag>::Params& params);

    template <typename Tag, typename Callback>
    void on_request(Callback&& callback);

    template <typename Tag, typename Callback>
    void on_notification(Callback&& callback);

private:
    template <typename Params, typename Callback>
    void bind_request_callback(std::string_view method, Callback&& callback);

    template <typename Params, typename Callback>
    void bind_notification_callback(std::string_view method, Callback&& callback);

    using RequestCallback = std::function<
        task<std::string, Error>(const protocol::RequestID&, std::string_view, cancellation_token)>;
    using NotificationCallback = std::function<void(std::string_view)>;

    void register_request_callback(std::string_view method, RequestCallback callback);

    void register_notification_callback(std::string_view method, NotificationCallback callback);

    task<std::string, Error> send_request_impl(std::string_view method,
                                               std::string params,
                                               request_options opts);

    Result<void> send_notification_impl(std::string_view method, std::string params);

    struct Self;
    std::unique_ptr<Self> self;
};

#if ETD_IPC_ENABLE_JSON
using JsonPeer = Peer<JsonCodec>;
#endif
using BincodePeer = Peer<BincodeCodec>;

}  // namespace eventide::ipc

#define ETD_IPC_PEER_INL_FROM_HEADER
#include "eventide/ipc/peer.inl"
#undef ETD_IPC_PEER_INL_FROM_HEADER

namespace eventide::ipc {

#if ETD_IPC_ENABLE_JSON
extern template class Peer<JsonCodec>;
#endif
extern template class Peer<BincodeCodec>;

}  // namespace eventide::ipc
