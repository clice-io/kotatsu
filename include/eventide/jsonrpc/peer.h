#pragma once

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "eventide/jsonrpc/protocol.h"
#include "eventide/jsonrpc/transport.h"
#include "eventide/async/cancellation.h"
#include "eventide/async/loop.h"
#include "eventide/async/task.h"

namespace eventide::jsonrpc {

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

struct RPCError {
    protocol::integer code = static_cast<protocol::integer>(protocol::ErrorCode::RequestFailed);
    std::string message;
    std::optional<protocol::Value> data = {};

    RPCError() = default;

    RPCError(protocol::integer code,
             std::string message,
             std::optional<protocol::Value> data = {}) :
        code(code), message(std::move(message)), data(std::move(data)) {}

    RPCError(protocol::ErrorCode code,
             std::string message,
             std::optional<protocol::Value> data = {}) :
        RPCError(static_cast<protocol::integer>(code), std::move(message), std::move(data)) {}

    RPCError(std::string message) : message(std::move(message)) {}

    RPCError(const char* message) : message(message == nullptr ? "" : message) {}
};

template <typename T>
using Result = std::expected<T, RPCError>;

template <typename Params, typename ResultT = typename protocol::RequestTraits<Params>::Result>
using RequestResult = task<Result<ResultT>>;

class Peer {
public:
    Peer(event_loop& loop, std::unique_ptr<Transport> transport);

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

    task<Result<std::string>> send_request_json(std::string_view method, std::string params_json);

    task<Result<std::string>> send_request_json(std::string_view method,
                                                std::string params_json,
                                                cancellation_token token);

    task<Result<std::string>> send_request_json(std::string_view method,
                                                std::string params_json,
                                                std::chrono::milliseconds timeout);

    Result<void> send_notification_json(std::string_view method, std::string params_json);

private:
    struct Self;
    std::unique_ptr<Self> self;
};

}  // namespace eventide::jsonrpc

#define EVENTIDE_JSONRPC_PEER_INL_FROM_HEADER
#include "eventide/jsonrpc/peer.inl"
#undef EVENTIDE_JSONRPC_PEER_INL_FROM_HEADER
