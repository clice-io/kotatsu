#include "eventide/ipc/peer.h"

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "eventide/ipc/json_codec.h"
#include "eventide/async/cancellation.h"
#include "eventide/async/sync.h"
#include "eventide/async/watcher.h"

namespace eventide::ipc {

namespace {

task<> cancel_after_timeout(std::chrono::milliseconds timeout,
                            std::shared_ptr<cancellation_source> timeout_source,
                            event_loop& loop) {
    co_await sleep(timeout, loop);
    timeout_source->cancel();
}

}  // namespace

struct Peer::Self {
    struct PendingRequest {
        // Signaled when a response is available.
        event ready;

        // Serialized result or RPCError.
        std::optional<Result<std::string>> response;
    };

    // Shared event loop used to schedule all peer coroutines.
    event_loop& loop;

    // Owning transport endpoint for I/O.
    std::unique_ptr<Transport> transport;

    // Codec for message envelope serialization.
    std::unique_ptr<Codec> codec;

    // Buffered outbound payloads.
    std::deque<std::string> outgoing_queue;

    // Monotonic local request id generator.
    protocol::RequestID::value_type next_request_id = 1;

    // method -> request handler
    std::unordered_map<std::string, RequestCallback> request_callbacks;
    // method -> notification handler
    std::unordered_map<std::string, NotificationCallback> notification_callbacks;

    // Outbound requests waiting for response.
    std::unordered_map<protocol::RequestID, std::shared_ptr<PendingRequest>> pending_requests;
    // Inbound running handlers, cancellable by id.
    std::unordered_map<protocol::RequestID, std::shared_ptr<cancellation_source>> incoming_requests;

    // Peer read loop state flag.
    bool running = false;

    // Outbound writer coroutine state flag.
    bool writer_running = false;

    explicit Self(event_loop& external_loop) : loop(external_loop) {}

    // Outbound pipeline: enqueue message and flush through transport writer.
    void enqueue_outgoing(std::string payload) {
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
                outgoing_queue.clear();
                fail_pending_requests("transport write failed");
                break;
            }
        }

        writer_running = false;
    }

    void send_error(const protocol::ResponseID& id, const RPCError& error) {
        auto response = codec->encode_error_response(id, error);
        if(response) {
            enqueue_outgoing(std::move(*response));
        }
    }

    // Pending outbound request bookkeeping.
    void complete_pending_request(const protocol::RequestID& id, Result<std::string> response) {
        auto it = pending_requests.find(id);
        if(it == pending_requests.end()) {
            return;
        }

        auto pending = std::move(it->second);
        pending_requests.erase(it);
        pending->response = std::move(response);
        pending->ready.set();
    }

    void fail_pending_requests(const std::string& message) {
        if(pending_requests.empty()) {
            return;
        }

        std::vector<std::shared_ptr<PendingRequest>> pending;
        pending.reserve(pending_requests.size());
        for(auto& pending_entry: pending_requests) {
            pending.push_back(pending_entry.second);
        }
        pending_requests.clear();

        for(auto& state: pending) {
            state->response = std::unexpected(RPCError(message));
            state->ready.set();
        }
    }

    // Inbound dispatch helpers.
    void dispatch_notification(const std::string& method, std::string_view params) {
        if(method == "$/cancelRequest") {
            auto cancel_id = codec->parse_cancel_id(params);
            if(cancel_id) {
                auto it = incoming_requests.find(*cancel_id);
                if(it != incoming_requests.end() && it->second) {
                    it->second->cancel();
                }
            }
            return;
        }

        if(auto it = notification_callbacks.find(method); it != notification_callbacks.end()) {
            it->second(params);
        }
    }

    void dispatch_request(const std::string& method,
                          const protocol::RequestID& id,
                          std::string_view params) {
        if(incoming_requests.contains(id)) {
            send_error(protocol::ResponseID{id},
                       RPCError(protocol::ErrorCode::InvalidRequest, "duplicate request id"));
            return;
        }

        auto it = request_callbacks.find(method);
        if(it == request_callbacks.end()) {
            send_error(
                protocol::ResponseID{id},
                RPCError(protocol::ErrorCode::MethodNotFound, "method not found: " + method));
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
        auto guarded_result = co_await with_token(token, callback(id, params, token));
        incoming_requests.erase(id);

        if(!guarded_result.has_value()) {
            send_error(protocol::ResponseID{id},
                       RPCError(protocol::ErrorCode::RequestCancelled, "request cancelled"));
            co_return;
        }

        auto result_payload = std::move(*guarded_result);
        if(!result_payload) {
            send_error(protocol::ResponseID{id}, result_payload.error());
            co_return;
        }

        auto response = codec->encode_success_response(id, *result_payload);
        if(!response) {
            send_error(protocol::ResponseID{id},
                       RPCError(protocol::ErrorCode::InternalError, response.error().message));
            co_return;
        }

        enqueue_outgoing(std::move(*response));
    }

    void dispatch_response(const protocol::RequestID& id, const IncomingMessage& msg) {
        if(msg.error.has_value()) {
            complete_pending_request(id, std::unexpected(*msg.error));
            return;
        }

        if(msg.result.empty()) {
            complete_pending_request(id,
                                     std::unexpected(RPCError(protocol::ErrorCode::InvalidRequest,
                                                              "response is missing result")));
            return;
        }

        complete_pending_request(id, std::string(msg.result));
    }

    void dispatch_incoming_message(std::string_view payload) {
        auto msg = codec->parse_message(payload);

        if(msg.parse_error.has_value()) {
            // Route parse errors to pending requests when possible (id present, no method).
            if(!msg.method.has_value() && msg.id.has_value()) {
                complete_pending_request(*msg.id, std::unexpected(*msg.parse_error));
                return;
            }

            protocol::ResponseID response_id = std::nullopt;
            if(msg.id.has_value()) {
                response_id = *msg.id;
            }
            send_error(response_id, *msg.parse_error);
            return;
        }

        if(msg.method.has_value()) {
            auto params = std::string_view(msg.params);
            if(msg.id.has_value()) {
                dispatch_request(*msg.method, *msg.id, params);
            } else if(msg.id_is_null) {
                send_error(
                    std::nullopt,
                    RPCError(protocol::ErrorCode::InvalidRequest, "request id must be integer"));
            } else {
                dispatch_notification(*msg.method, params);
            }
            return;
        }

        if(msg.id.has_value()) {
            if(!msg.result.empty() == msg.error.has_value()) {
                complete_pending_request(
                    *msg.id,
                    std::unexpected(
                        RPCError(protocol::ErrorCode::InvalidRequest,
                                 "response must contain exactly one of result or error")));
                return;
            }

            dispatch_response(*msg.id, msg);
            return;
        }

        if(msg.id_is_null) {
            return;
        }

        send_error(
            std::nullopt,
            RPCError(protocol::ErrorCode::InvalidRequest, "message must contain method or id"));
    }
};

Peer::Peer(event_loop& loop, std::unique_ptr<Transport> transport) :
    Peer(loop, std::move(transport), std::make_unique<JsonCodec>()) {}

Peer::Peer(event_loop& loop, std::unique_ptr<Transport> transport, std::unique_ptr<Codec> codec) :
    self(std::make_unique<Self>(loop)) {
    self->transport = std::move(transport);
    self->codec = std::move(codec);
}

Peer::~Peer() = default;

task<> Peer::run() {
    if(!self || !self->transport || self->running) {
        co_return;
    }

    self->running = true;

    while(self->transport) {
        auto payload = co_await self->transport->read_message();
        if(!payload.has_value()) {
            self->fail_pending_requests("transport closed");
            break;
        }

        self->dispatch_incoming_message(*payload);
    }

    self->running = false;
}

Result<void> Peer::close_output() {
    if(!self || !self->transport) {
        return std::unexpected("transport is null");
    }

    return self->transport->close_output();
}

void Peer::register_request_callback(std::string_view method, RequestCallback callback) {
    self->request_callbacks.insert_or_assign(std::string(method), std::move(callback));
}

void Peer::register_notification_callback(std::string_view method, NotificationCallback callback) {
    self->notification_callbacks.insert_or_assign(std::string(method), std::move(callback));
}

task<Result<std::string>> Peer::send_request_impl(std::string_view method, std::string params) {
    co_return co_await send_request_impl(method, std::move(params), cancellation_token{});
}

task<Result<std::string>> Peer::send_request_impl(std::string_view method,
                                                  std::string params,
                                                  cancellation_token token) {
    if(!self || !self->transport) {
        co_return std::unexpected("transport is null");
    }

    if(token.cancelled()) {
        co_return std::unexpected(
            RPCError(protocol::ErrorCode::RequestCancelled, "request cancelled"));
    }

    auto request_id = protocol::RequestID{self->next_request_id++};

    auto pending = std::make_shared<Self::PendingRequest>();
    self->pending_requests.insert_or_assign(request_id, pending);

    auto request_encoded = self->codec->encode_request(request_id, method, params);
    if(!request_encoded) {
        self->pending_requests.erase(request_id);
        co_return std::unexpected(request_encoded.error());
    }

    self->enqueue_outgoing(std::move(*request_encoded));

    auto wait_pending = [](const std::shared_ptr<Self::PendingRequest>& state) -> task<> {
        co_await state->ready.wait();
    };
    auto wait_result = co_await with_token(token, wait_pending(pending));
    if(!wait_result.has_value()) {
        if(auto it = self->pending_requests.find(request_id); it != self->pending_requests.end()) {
            self->pending_requests.erase(it);
            auto cancel_json = detail::serialize_json(protocol::Object{
                {"id", protocol::Value{request_id.value}},
            });
            if(cancel_json) {
                auto cancel_encoded =
                    self->codec->encode_notification("$/cancelRequest", *cancel_json);
                if(cancel_encoded) {
                    self->enqueue_outgoing(std::move(*cancel_encoded));
                }
            }
        }
        co_return std::unexpected(
            RPCError(protocol::ErrorCode::RequestCancelled, "request cancelled"));
    }

    if(!pending->response.has_value()) {
        co_return std::unexpected("request was not completed");
    }

    co_return std::move(*pending->response);
}

task<Result<std::string>> Peer::send_request_impl(std::string_view method,
                                                  std::string params,
                                                  std::chrono::milliseconds timeout) {
    if(timeout <= std::chrono::milliseconds::zero()) {
        co_return std::unexpected(
            RPCError(protocol::ErrorCode::RequestCancelled, "request timed out"));
    }

    auto timeout_source = std::make_shared<cancellation_source>();
    if(self) {
        self->loop.schedule(cancel_after_timeout(timeout, timeout_source, self->loop));
    }

    auto result = co_await send_request_impl(method, std::move(params), timeout_source->token());
    if(!result &&
       result.error().code ==
           static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled) &&
       timeout_source->cancelled()) {
        co_return std::unexpected(
            RPCError(protocol::ErrorCode::RequestCancelled, "request timed out"));
    }

    co_return result;
}

Result<void> Peer::send_notification_impl(std::string_view method, std::string params) {
    if(!self || !self->transport) {
        return std::unexpected("transport is null");
    }

    auto notification_encoded = self->codec->encode_notification(method, params);
    if(!notification_encoded) {
        return std::unexpected(notification_encoded.error());
    }

    self->enqueue_outgoing(std::move(*notification_encoded));
    return {};
}

}  // namespace eventide::ipc
