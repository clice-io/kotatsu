#pragma once

#ifndef ETD_IPC_PEER_INL_FROM_HEADER
#include "eventide/ipc/peer.h"
#endif

#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "eventide/common/function_traits.h"

// Lazy log macro: level check happens before std::format is evaluated.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ET_IPC_LOG(self_ptr, lvl, fmt, ...)                       \
    do {                                                          \
        if((self_ptr)->logger && (lvl) >= (self_ptr)->min_level)  \
            (self_ptr)->logger((lvl), std::format(fmt, __VA_ARGS__)); \
    } while(false)

namespace eventide::ipc {

namespace detail {

template <typename Params>
constexpr bool has_request_traits_v = requires {
    typename protocol::RequestTraits<Params>::Result;
    protocol::RequestTraits<Params>::method;
};

template <typename Params>
constexpr bool has_notification_traits_v =
    requires { protocol::NotificationTraits<Params>::method; };

template <typename Callback>
using request_callback_args_t = callable_args_t<std::remove_cvref_t<Callback>>;

template <typename Callback>
using request_callback_params_t =
    std::remove_cvref_t<std::tuple_element_t<1, request_callback_args_t<Callback>>>;

template <typename Callback>
using request_callback_return_t = callable_return_t<std::remove_cvref_t<Callback>>;

template <typename Callback>
using notification_callback_args_t = callable_args_t<std::remove_cvref_t<Callback>>;

template <typename Callback>
using notification_callback_params_t =
    std::remove_cvref_t<std::tuple_element_t<0, notification_callback_args_t<Callback>>>;

template <typename Callback>
using notification_callback_return_t = callable_return_t<std::remove_cvref_t<Callback>>;

template <typename Callback, typename PeerT>
consteval void validate_request_callback_signature() {
    using Args = request_callback_args_t<Callback>;
    static_assert(std::tuple_size_v<Args> == 2, "request callback should have two parameters");

    using Context = std::remove_cvref_t<std::tuple_element_t<0, Args>>;
    static_assert(std::is_same_v<Context, basic_request_context<PeerT>>,
                  "request callback first parameter should be RequestContext");
}

template <typename Callback>
consteval void validate_notification_callback_signature() {
    using Args = notification_callback_args_t<Callback>;
    static_assert(std::tuple_size_v<Args> == 1, "notification callback should have one parameter");

    using Ret = notification_callback_return_t<Callback>;
    static_assert(std::is_same_v<Ret, void>, "notification callback should return void");
}

inline task<> cancel_after_timeout(std::chrono::milliseconds timeout,
                                   std::shared_ptr<cancellation_source> timeout_source,
                                   event_loop& loop) {
    co_await sleep(timeout, loop);
    timeout_source->cancel();
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Peer<CodecT>::Self
// ---------------------------------------------------------------------------

template <typename CodecT>
struct Peer<CodecT>::Self {
    struct PendingRequest {
        event ready;
        std::optional<Result<std::string>> response;
    };

    event_loop& loop;
    std::unique_ptr<Transport> transport;
    CodecT codec;

    std::deque<std::string> outgoing_queue;
    std::int64_t next_request_id = 1;

    std::unordered_map<std::string, RequestCallback> request_callbacks;
    std::unordered_map<std::string, NotificationCallback> notification_callbacks;

    std::unordered_map<protocol::RequestID, std::shared_ptr<PendingRequest>> pending_requests;
    std::unordered_map<protocol::RequestID, std::shared_ptr<cancellation_source>> incoming_requests;

    bool running = false;
    bool writer_running = false;

    LogCallback logger;
    LogLevel min_level = LogLevel::info;

    explicit Self(event_loop& external_loop, CodecT codec_arg) :
        loop(external_loop), codec(std::move(codec_arg)) {}

    void enqueue_outgoing(std::string payload) {
        ET_IPC_LOG(this, LogLevel::trace, "send: {}", payload);
        outgoing_queue.push_back(std::move(payload));
        if(!writer_running) {
            writer_running = true;
            loop.schedule(write_loop());
        }
    }

    task<> write_loop() {
        while(!outgoing_queue.empty()) {
            auto payload = std::move(outgoing_queue.front());
            outgoing_queue.pop_front();

            if(!transport) {
                break;
            }

            auto written = co_await transport->write_message(payload);
            if(!written) {
                ET_IPC_LOG(this, LogLevel::error,
                    "transport write failed: {}", written.error().message);
                outgoing_queue.clear();
                fail_pending_requests(written.error().message);
                break;
            }
        }

        writer_running = false;
    }

    void send_error(const protocol::RequestID& id, const Error& error) {
        ET_IPC_LOG(this, LogLevel::error, "error response: {}", error.message);
        auto response = codec.encode_error_response(id, error);
        if(response) {
            enqueue_outgoing(std::move(*response));
        }
    }

    void complete_pending_request(const protocol::RequestID& id, Result<std::string>&& response) {
        auto it = pending_requests.find(id);
        if(it == pending_requests.end()) {
            ET_IPC_LOG(this, LogLevel::warn, "orphan response for id={}", id);
            return;
        }

        ET_IPC_LOG(this, LogLevel::debug, "response received for id={}", id);

        auto pending = std::move(it->second);
        pending_requests.erase(it);
        pending->response = std::move(response);
        pending->ready.set();
    }

    void fail_pending_requests(const std::string& message) {
        if(pending_requests.empty()) {
            return;
        }

        ET_IPC_LOG(this, LogLevel::error,
            "failing {} pending request(s): {}", pending_requests.size(), message);

        auto values = pending_requests | std::views::values;
        std::vector<std::shared_ptr<PendingRequest>> pending(values.begin(), values.end());
        pending_requests.clear();

        for(auto& state: pending) {
            state->response = outcome_error(Error(message));
            state->ready.set();
        }
    }

    void dispatch_notification(const std::string& method, std::string_view params) {
        ET_IPC_LOG(this, LogLevel::debug, "notification: {}", method);

        if(method == "$/cancelRequest") {
            auto parsed = codec.template deserialize_value<protocol::CancelRequestParams>(params);
            if(parsed) {
                auto it = incoming_requests.find(parsed->id);
                if(it != incoming_requests.end() && it->second) {
                    // Copy the shared_ptr before calling cancel(). cancel() may
                    // synchronously resume the request coroutine, which on completion
                    // erases its entry from incoming_requests — invalidating `it` and
                    // potentially destroying the cancellation_source. The local copy
                    // keeps the source alive through the entire cancel() call.
                    auto source = it->second;
                    source->cancel();
                }
            }
            return;
        }

        if(auto it = notification_callbacks.find(method); it != notification_callbacks.end()) {
            it->second(params);
        } else {
            ET_IPC_LOG(this, LogLevel::warn, "unhandled notification: {}", method);
        }
    }

    void dispatch_request(const std::string& method,
                          const protocol::RequestID& id,
                          std::string_view params) {
        ET_IPC_LOG(this, LogLevel::debug, "request: {} id={}", method, id);

        if(incoming_requests.contains(id)) {
            send_error(id, Error(protocol::ErrorCode::InvalidRequest, "duplicate request id"));
            return;
        }

        auto it = request_callbacks.find(method);
        if(it == request_callbacks.end()) {
            send_error(id,
                       Error(protocol::ErrorCode::MethodNotFound, "method not found: " + method));
            return;
        }

        auto callback = it->second;
        auto cancel_source = std::make_shared<cancellation_source>();
        incoming_requests.insert_or_assign(id, cancel_source);
        auto task =
            run_request(id, std::move(callback), std::string(params), cancel_source->token());
        loop.schedule(std::move(task));
    }

    task<> run_request(protocol::RequestID id,
                       RequestCallback callback,
                       std::string params,
                       cancellation_token token) {
        auto guarded_result = co_await with_token(callback(id, params, token), token);
        incoming_requests.erase(id);

        if(guarded_result.is_cancelled()) {
            send_error(id, Error(protocol::ErrorCode::RequestCancelled, "request cancelled"));
            co_return;
        }

        if(guarded_result.has_error()) {
            send_error(id, guarded_result.error());
            co_return;
        }

        auto response = codec.encode_success_response(id, *guarded_result);
        if(!response) {
            send_error(id, Error(protocol::ErrorCode::InternalError, response.error().message));
            co_return;
        }

        enqueue_outgoing(std::move(*response));
    }

    void dispatch_incoming_message(std::string_view payload) {
        ET_IPC_LOG(this, LogLevel::trace, "recv: {}", payload);
        auto msg = codec.parse_message(payload);
        std::visit(
            [&](auto& m) {
                using T = std::remove_cvref_t<decltype(m)>;
                if constexpr(std::is_same_v<T, IncomingRequest>) {
                    dispatch_request(m.method, m.id, m.params);
                } else if constexpr(std::is_same_v<T, IncomingNotification>) {
                    dispatch_notification(m.method, m.params);
                } else if constexpr(std::is_same_v<T, IncomingResponse>) {
                    complete_pending_request(m.id, Result<std::string>(std::move(m.result)));
                } else if constexpr(std::is_same_v<T, IncomingErrorResponse>) {
                    complete_pending_request(m.id, outcome_error(std::move(m.error)));
                } else if constexpr(std::is_same_v<T, IncomingParseError>) {
                    send_error(protocol::RequestID{}, m.error);
                }
            },
            msg);
    }
};

// ---------------------------------------------------------------------------
// Peer<CodecT> non-template methods
// ---------------------------------------------------------------------------

template <typename CodecT>
Peer<CodecT>::Peer(event_loop& loop, std::unique_ptr<Transport> transport, CodecT codec) :
    self(std::make_unique<Self>(loop, std::move(codec))) {
    self->transport = std::move(transport);
}

template <typename CodecT>
Peer<CodecT>::~Peer() = default;

template <typename CodecT>
task<> Peer<CodecT>::run() {
    if(!self || !self->transport || self->running) {
        co_return;
    }

    self->running = true;
    ET_IPC_LOG(self.get(), LogLevel::info, "{}", "read loop started");

    while(self->transport) {
        auto payload = co_await self->transport->read_message();
        if(!payload.has_value()) {
            self->fail_pending_requests("transport closed");
            break;
        }

        self->dispatch_incoming_message(*payload);
    }

    ET_IPC_LOG(self.get(), LogLevel::info, "{}", "read loop ended");
    self->running = false;
}

template <typename CodecT>
Result<void> Peer<CodecT>::close_output() {
    if(!self || !self->transport) {
        return outcome_error(Error("transport is null"));
    }

    return self->transport->close_output();
}

template <typename CodecT>
void Peer<CodecT>::set_logger(LogCallback callback, LogLevel min_level) {
    self->logger = std::move(callback);
    self->min_level = min_level;
}

template <typename CodecT>
void Peer<CodecT>::register_request_callback(std::string_view method, RequestCallback callback) {
    self->request_callbacks.insert_or_assign(std::string(method), std::move(callback));
}

template <typename CodecT>
void Peer<CodecT>::register_notification_callback(std::string_view method,
                                                  NotificationCallback callback) {
    self->notification_callbacks.insert_or_assign(std::string(method), std::move(callback));
}

template <typename CodecT>
task<std::string, Error> Peer<CodecT>::send_request_impl(std::string_view method,
                                                         std::string params,
                                                         request_options opts) {
    std::shared_ptr<cancellation_source> timeout_source;

    if(opts.timeout.has_value()) {
        if(*opts.timeout <= std::chrono::milliseconds::zero()) {
            co_await fail(protocol::ErrorCode::RequestCancelled, "request timed out");
        }

        timeout_source = std::make_shared<cancellation_source>();
        if(self) {
            self->loop.schedule(
                detail::cancel_after_timeout(*opts.timeout, timeout_source, self->loop));
        }
    }

    if(!self || !self->transport) {
        co_await fail("transport is null");
    }

    if(opts.token && opts.token->cancelled()) {
        co_await fail(protocol::ErrorCode::RequestCancelled, "request cancelled");
    }

    protocol::RequestID request_id{self->next_request_id++};

    auto pending = std::make_shared<typename Self::PendingRequest>();
    self->pending_requests.insert_or_assign(request_id, pending);

    auto request_encoded = self->codec.encode_request(request_id, method, params);
    if(!request_encoded) {
        self->pending_requests.erase(request_id);
        co_await fail(request_encoded.error());
    }

    self->enqueue_outgoing(std::move(*request_encoded));

    auto wait_pending = [](const std::shared_ptr<typename Self::PendingRequest>& state) -> task<> {
        co_await state->ready.wait();
    };
    auto wait_task = wait_pending(pending);
    outcome<void, void, cancellation> wait_result = outcome_value();
    if(opts.token && timeout_source) {
        wait_result =
            co_await with_token(std::move(wait_task), *opts.token, timeout_source->token());
    } else if(opts.token) {
        wait_result = co_await with_token(std::move(wait_task), *opts.token);
    } else if(timeout_source) {
        wait_result = co_await with_token(std::move(wait_task), timeout_source->token());
    } else {
        co_await std::move(wait_task);
    }
    if(!wait_result.has_value()) {
        if(auto it = self->pending_requests.find(request_id); it != self->pending_requests.end()) {
            self->pending_requests.erase(it);
            auto cancel_params_serialized =
                self->codec.serialize_value(protocol::CancelRequestParams{request_id});
            if(cancel_params_serialized) {
                auto cancel_encoded =
                    self->codec.encode_notification("$/cancelRequest", *cancel_params_serialized);
                if(cancel_encoded) {
                    self->enqueue_outgoing(std::move(*cancel_encoded));
                }
            }
        }

        if(opts.token && opts.token->cancelled()) {
            co_await fail(protocol::ErrorCode::RequestCancelled, "request cancelled");
        }
        co_await fail(protocol::ErrorCode::RequestCancelled, "request timed out");
    }

    if(!pending->response.has_value()) {
        co_await fail("request was not completed");
    }

    co_return co_await or_fail(std::move(*pending->response));
}

template <typename CodecT>
Result<void> Peer<CodecT>::send_notification_impl(std::string_view method, std::string params) {
    if(!self || !self->transport) {
        return outcome_error(Error("transport is null"));
    }

    auto notification_encoded = self->codec.encode_notification(method, params);
    if(!notification_encoded) {
        return outcome_error(notification_encoded.error());
    }

    self->enqueue_outgoing(std::move(*notification_encoded));
    return {};
}

// ---------------------------------------------------------------------------
// Peer<CodecT> template methods
// ---------------------------------------------------------------------------

template <typename CodecT>
template <typename Params>
RequestResult<Params> Peer<CodecT>::send_request(const Params& params, request_options opts) {
    static_assert(detail::has_request_traits_v<Params>,
                  "send_request(params) requires RequestTraits<Params>");
    using Traits = protocol::RequestTraits<Params>;

    auto serialized_params = co_await or_fail(self->codec.serialize_value(params));
    auto raw_result =
        co_await send_request_impl(Traits::method, std::move(serialized_params), std::move(opts))
            .or_fail();
    co_return co_await or_fail(
        self->codec.template deserialize_value<typename Traits::Result>(raw_result));
}

template <typename CodecT>
template <typename ResultT, typename Params>
task<ResultT, Error> Peer<CodecT>::send_request(std::string_view method,
                                                const Params& params,
                                                request_options opts) {
    auto serialized_params = co_await or_fail(self->codec.serialize_value(params));
    auto raw_result =
        co_await send_request_impl(method, std::move(serialized_params), std::move(opts)).or_fail();
    co_return co_await or_fail(self->codec.template deserialize_value<ResultT>(raw_result));
}

template <typename CodecT>
template <typename Params>
Result<void> Peer<CodecT>::send_notification(const Params& params) {
    static_assert(detail::has_notification_traits_v<Params>,
                  "send_notification(params) requires NotificationTraits<Params>");
    using Traits = protocol::NotificationTraits<Params>;

    auto serialized_params = self->codec.serialize_value(params);
    if(!serialized_params) {
        return outcome_error(serialized_params.error());
    }
    return send_notification_impl(Traits::method, std::move(*serialized_params));
}

template <typename CodecT>
template <typename Params>
Result<void> Peer<CodecT>::send_notification(std::string_view method, const Params& params) {
    auto serialized_params = self->codec.serialize_value(params);
    if(!serialized_params) {
        return outcome_error(serialized_params.error());
    }
    return send_notification_impl(method, std::move(*serialized_params));
}

template <typename CodecT>
template <typename Callback>
void Peer<CodecT>::on_request(Callback&& callback) {
    detail::validate_request_callback_signature<Callback, Peer>();

    using Params = detail::request_callback_params_t<Callback>;
    static_assert(detail::has_request_traits_v<Params>,
                  "on_request(callback) requires RequestTraits<Params>");

    using Ret = detail::request_callback_return_t<Callback>;
    static_assert(std::is_same_v<Ret, RequestResult<Params>>,
                  "request callback return type should be RequestResult<Params>");

    bind_request_callback<Params>(protocol::RequestTraits<Params>::method,
                                  std::forward<Callback>(callback));
}

template <typename CodecT>
template <typename Callback>
void Peer<CodecT>::on_request(std::string_view method, Callback&& callback) {
    detail::validate_request_callback_signature<Callback, Peer>();

    using Params = detail::request_callback_params_t<Callback>;
    bind_request_callback<Params>(method, std::forward<Callback>(callback));
}

template <typename CodecT>
template <typename Callback>
void Peer<CodecT>::on_notification(Callback&& callback) {
    detail::validate_notification_callback_signature<Callback>();

    using Params = detail::notification_callback_params_t<Callback>;
    static_assert(detail::has_notification_traits_v<Params>,
                  "on_notification(callback) requires NotificationTraits<Params>");

    bind_notification_callback<Params>(protocol::NotificationTraits<Params>::method,
                                       std::forward<Callback>(callback));
}

template <typename CodecT>
template <typename Callback>
void Peer<CodecT>::on_notification(std::string_view method, Callback&& callback) {
    detail::validate_notification_callback_signature<Callback>();

    using Params = detail::notification_callback_params_t<Callback>;
    bind_notification_callback<Params>(method, std::forward<Callback>(callback));
}

template <typename CodecT>
template <typename Params, typename Callback>
void Peer<CodecT>::bind_request_callback(std::string_view method, Callback&& callback) {
    auto wrapped = [cb = std::forward<Callback>(callback),
                    method_name = std::string(method),
                    peer = this](const protocol::RequestID& request_id,
                                 std::string_view params_raw,
                                 cancellation_token token) -> task<std::string, Error> {
        auto parsed_params = peer->self->codec.template deserialize_value<Params>(
            params_raw,
            protocol::ErrorCode::InvalidParams);
        if(!parsed_params) {
            ET_IPC_LOG(peer->self.get(), LogLevel::warn,
                "request '{}' params deserialization failed: {}",
                method_name, parsed_params.error().message);
            co_await fail(parsed_params.error());
        }

        typename Peer::RequestContext context(*peer, request_id, std::move(token));
        context.method = method_name;

        auto result = co_await std::invoke(cb, context, *parsed_params).or_fail();
        auto serialized = peer->self->codec.serialize_value(result);
        if(!serialized) {
            co_await fail(
                Error(protocol::ErrorCode::InternalError, serialized.error().message));
        }

        co_return std::move(*serialized);
    };

    register_request_callback(method, std::move(wrapped));
}

template <typename CodecT>
template <typename Params, typename Callback>
void Peer<CodecT>::bind_notification_callback(std::string_view method, Callback&& callback) {
    auto wrapped = [cb = std::forward<Callback>(callback),
                    peer = this](std::string_view params_raw) {
        auto parsed_params = peer->self->codec.template deserialize_value<Params>(params_raw);
        if(!parsed_params) {
            ET_IPC_LOG(peer->self.get(), LogLevel::warn,
                "notification params deserialization failed: {}",
                parsed_params.error().message);
            return;
        }
        std::invoke(cb, *parsed_params);
    };

    register_notification_callback(method, std::move(wrapped));
}

}  // namespace eventide::ipc

#undef ET_IPC_LOG
