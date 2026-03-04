#include "eventide/jsonrpc/peer.h"

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "eventide/async/cancellation.h"
#include "eventide/async/sync.h"
#include "eventide/async/watcher.h"

namespace eventide::jsonrpc {

namespace {

template <typename T>
Result<T> parse_json_value(std::string_view json,
                           protocol::ErrorCode code = protocol::ErrorCode::RequestFailed) {
    auto parsed = serde::json::parse<T>(json);
    if(!parsed) {
        return std::unexpected(
            RPCError(code, std::string(serde::json::error_message(parsed.error()))));
    }
    return std::move(*parsed);
}

struct parsed_incoming_id {
    bool present = false;
    bool is_null = false;
    protocol::RequestID request_id{};
};

struct cancel_request_params {
    protocol::RequestID id;
};

task<> cancel_after_timeout(std::chrono::milliseconds timeout,
                            std::shared_ptr<cancellation_source> timeout_source,
                            event_loop& loop) {
    co_await sleep(timeout, loop);
    timeout_source->cancel();
}

struct outgoing_request_message {
    std::string jsonrpc = "2.0";
    protocol::RequestID id;
    std::string method;
    protocol::Value params;
};

struct outgoing_notification_message {
    std::string jsonrpc = "2.0";
    std::string method;
    protocol::Value params;
};

struct outgoing_success_response_message {
    std::string jsonrpc = "2.0";
    protocol::RequestID id;
    protocol::Value result;
};

struct outgoing_error_response_message {
    std::string jsonrpc = "2.0";
    protocol::ResponseID id;
    protocol::ResponseError error;
};

}  // namespace

struct Peer::Self {
    struct PendingRequest {
        // Signaled when a response is available.
        event ready;

        // Serialized JSON result or RPCError.
        std::optional<Result<std::string>> response;
    };

    // Shared event loop used to schedule all peer coroutines.
    event_loop& loop;

    // Owning transport endpoint for I/O.
    std::unique_ptr<Transport> transport;

    // Buffered outbound JSON payloads.
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
        auto response = detail::serialize_json(outgoing_error_response_message{
            .id = id,
            .error = protocol::ResponseError{error.code, error.message, error.data},
        });
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
    void dispatch_notification(const std::string& method, std::string_view params_json) {
        if(method == "$/cancelRequest") {
            auto parsed_params =
                parse_json_value<cancel_request_params>(params_json,
                                                        protocol::ErrorCode::InvalidParams);
            if(!parsed_params) {
                return;
            }

            auto it = incoming_requests.find(parsed_params->id);
            if(it != incoming_requests.end() && it->second) {
                it->second->cancel();
            }
            return;
        }

        if(auto it = notification_callbacks.find(method); it != notification_callbacks.end()) {
            it->second(params_json);
        }
    }

    void dispatch_request(const std::string& method,
                          const protocol::RequestID& id,
                          std::string_view params_json) {
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
            run_request(id, std::move(callback), std::string(params_json), cancel_source->token());
        loop.schedule(std::move(task));
    }

    task<> run_request(protocol::RequestID id,
                       RequestCallback callback,
                       std::string params_json,
                       cancellation_token token) {
        auto guarded_result = co_await with_token(token, callback(id, params_json, token));
        incoming_requests.erase(id);

        if(!guarded_result.has_value()) {
            send_error(protocol::ResponseID{id},
                       RPCError(protocol::ErrorCode::RequestCancelled, "request cancelled"));
            co_return;
        }

        auto result_json = std::move(*guarded_result);
        if(!result_json) {
            send_error(protocol::ResponseID{id}, result_json.error());
            co_return;
        }

        auto result =
            parse_json_value<protocol::Value>(*result_json, protocol::ErrorCode::InternalError);
        if(!result) {
            send_error(protocol::ResponseID{id},
                       RPCError(protocol::ErrorCode::InternalError, result.error().message));
            co_return;
        }

        auto response = detail::serialize_json(outgoing_success_response_message{
            .id = id,
            .result = std::move(*result),
        });
        if(!response) {
            send_error(protocol::ResponseID{id},
                       RPCError(protocol::ErrorCode::InternalError, response.error().message));
            co_return;
        }

        enqueue_outgoing(std::move(*response));
    }

    void dispatch_response(const protocol::RequestID& id,
                           const std::optional<std::string_view>& result_json,
                           const std::optional<std::string_view>& error_json) {
        if(error_json.has_value()) {
            auto parsed_error =
                parse_json_value<protocol::ResponseError>(*error_json,
                                                          protocol::ErrorCode::InvalidRequest);
            if(!parsed_error) {
                complete_pending_request(id, std::unexpected(parsed_error.error()));
                return;
            }

            auto response_error = std::move(*parsed_error);
            complete_pending_request(id,
                                     std::unexpected(RPCError(response_error.code,
                                                              std::move(response_error.message),
                                                              std::move(response_error.data))));
            return;
        }

        if(!result_json.has_value()) {
            complete_pending_request(id,
                                     std::unexpected(RPCError(protocol::ErrorCode::InvalidRequest,
                                                              "response is missing result")));
            return;
        }

        complete_pending_request(id, std::string(*result_json));
    }

    void dispatch_incoming_message(std::string_view payload) {
        auto send_protocol_error =
            [this](const protocol::ResponseID& id, protocol::ErrorCode code, std::string message) {
                send_error(id, RPCError(code, std::move(message)));
            };

        simdjson::ondemand::parser parser;
        simdjson::padded_string json(payload);

        simdjson::ondemand::document document{};
        auto document_result = parser.iterate(json);
        auto document_error = std::move(document_result).get(document);
        if(document_error != simdjson::SUCCESS) {
            send_protocol_error(std::nullopt,
                                protocol::ErrorCode::ParseError,
                                std::string(simdjson::error_message(document_error)));
            return;
        }

        simdjson::ondemand::object object{};
        auto object_result = document.get_object();
        auto object_error = std::move(object_result).get(object);
        if(object_error != simdjson::SUCCESS) {
            auto code = object_error == simdjson::INCORRECT_TYPE
                            ? protocol::ErrorCode::InvalidRequest
                            : protocol::ErrorCode::ParseError;
            send_protocol_error(std::nullopt,
                                code,
                                std::string(simdjson::error_message(object_error)));
            return;
        }

        std::optional<std::string> method;
        parsed_incoming_id id;
        std::optional<std::string_view> params_json;
        std::optional<std::string_view> result_json;
        std::optional<std::string_view> error_json;
        auto response_id = [&id]() -> protocol::ResponseID {
            if(id.present && !id.is_null) {
                return id.request_id;
            }
            return std::nullopt;
        };
        auto fail_message =
            [this, &method, &id, &response_id, &send_protocol_error](protocol::ErrorCode code,
                                                                     std::string message) {
                if(!method.has_value() && id.present && !id.is_null) {
                    complete_pending_request(id.request_id,
                                             std::unexpected(RPCError(code, std::move(message))));
                    return;
                }

                send_protocol_error(response_id(), code, std::move(message));
            };

        for(auto field_result: object) {
            simdjson::ondemand::field field{};
            auto field_error = std::move(field_result).get(field);
            if(field_error != simdjson::SUCCESS) {
                fail_message(protocol::ErrorCode::InvalidRequest,
                             std::string(simdjson::error_message(field_error)));
                return;
            }

            std::string_view key;
            auto key_error = field.unescaped_key().get(key);
            if(key_error != simdjson::SUCCESS) {
                fail_message(protocol::ErrorCode::InvalidRequest,
                             std::string(simdjson::error_message(key_error)));
                return;
            }

            auto value = field.value();
            if(key == "method") {
                std::string_view method_text;
                auto method_error = value.get_string().get(method_text);
                if(method_error != simdjson::SUCCESS) {
                    send_protocol_error(response_id(),
                                        protocol::ErrorCode::InvalidRequest,
                                        std::string(simdjson::error_message(method_error)));
                    return;
                }
                method = std::string(method_text);
                continue;
            }

            if(key == "id") {
                simdjson::ondemand::json_type id_type{};
                auto id_type_error = value.type().get(id_type);
                if(id_type_error != simdjson::SUCCESS) {
                    send_protocol_error(std::nullopt,
                                        protocol::ErrorCode::InvalidRequest,
                                        std::string(simdjson::error_message(id_type_error)));
                    return;
                }

                if(id_type == simdjson::ondemand::json_type::number) {
                    std::int64_t integer_id = 0;
                    auto integer_error = value.get_int64().get(integer_id);
                    if(integer_error != simdjson::SUCCESS) {
                        send_protocol_error(std::nullopt,
                                            protocol::ErrorCode::InvalidRequest,
                                            "request id must be integer");
                        return;
                    }

                    id.present = true;
                    id.is_null = false;
                    id.request_id = protocol::RequestID{integer_id};
                    continue;
                }

                if(id_type == simdjson::ondemand::json_type::null) {
                    id.present = true;
                    id.is_null = true;
                    continue;
                }

                send_protocol_error(std::nullopt,
                                    protocol::ErrorCode::InvalidRequest,
                                    "request id must be integer or null");
                return;
            }

            std::string_view raw_json;
            auto raw_error = value.raw_json().get(raw_json);
            if(raw_error != simdjson::SUCCESS) {
                fail_message(protocol::ErrorCode::InvalidRequest,
                             std::string(simdjson::error_message(raw_error)));
                return;
            }

            if(key == "params") {
                params_json = raw_json;
                continue;
            }

            if(key == "result") {
                result_json = raw_json;
                continue;
            }

            if(key == "error") {
                error_json = raw_json;
                continue;
            }
        }

        if(!document.at_end()) {
            fail_message(protocol::ErrorCode::InvalidRequest,
                         "trailing content after JSON-RPC message");
            return;
        }

        if(method.has_value()) {
            auto params = params_json.value_or(std::string_view{});
            if(id.present) {
                if(id.is_null) {
                    send_protocol_error(std::nullopt,
                                        protocol::ErrorCode::InvalidRequest,
                                        "request id must be integer");
                    return;
                }

                dispatch_request(*method, id.request_id, params);
            } else {
                dispatch_notification(*method, params);
            }
            return;
        }

        if(id.present) {
            if(id.is_null) {
                return;
            }

            if(result_json.has_value() == error_json.has_value()) {
                complete_pending_request(
                    id.request_id,
                    std::unexpected(
                        RPCError(protocol::ErrorCode::InvalidRequest,
                                 "response must contain exactly one of result or error")));
                return;
            }

            dispatch_response(id.request_id, result_json, error_json);
            return;
        }

        send_protocol_error(std::nullopt,
                            protocol::ErrorCode::InvalidRequest,
                            "message must contain method or id");
    }
};

Peer::Peer(event_loop& loop, std::unique_ptr<Transport> transport) :
    self(std::make_unique<Self>(loop)) {
    self->transport = std::move(transport);
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

task<Result<std::string>> Peer::send_request_json(std::string_view method,
                                                  std::string params_json) {
    co_return co_await send_request_json(method, std::move(params_json), cancellation_token{});
}

task<Result<std::string>> Peer::send_request_json(std::string_view method,
                                                  std::string params_json,
                                                  cancellation_token token) {
    if(!self || !self->transport) {
        co_return std::unexpected("transport is null");
    }

    if(token.cancelled()) {
        co_return std::unexpected(
            RPCError(protocol::ErrorCode::RequestCancelled, "request cancelled"));
    }

    auto params =
        parse_json_value<protocol::Value>(params_json, protocol::ErrorCode::InternalError);
    if(!params) {
        co_return std::unexpected(params.error());
    }

    auto request_id = protocol::RequestID{self->next_request_id++};

    auto pending = std::make_shared<Self::PendingRequest>();
    self->pending_requests.insert_or_assign(request_id, pending);

    auto request_json = detail::serialize_json(outgoing_request_message{
        .id = request_id,
        .method = std::string(method),
        .params = std::move(*params),
    });
    if(!request_json) {
        self->pending_requests.erase(request_id);
        co_return std::unexpected(request_json.error());
    }

    self->enqueue_outgoing(std::move(*request_json));

    auto wait_pending = [](const std::shared_ptr<Self::PendingRequest>& state) -> task<> {
        co_await state->ready.wait();
    };
    auto wait_result = co_await with_token(token, wait_pending(pending));
    if(!wait_result.has_value()) {
        if(auto it = self->pending_requests.find(request_id); it != self->pending_requests.end()) {
            self->pending_requests.erase(it);
            protocol::Object cancel_params;
            cancel_params.insert_or_assign("id", protocol::Value{request_id.value});
            auto cancel_request = detail::serialize_json(outgoing_notification_message{
                .method = "$/cancelRequest",
                .params = protocol::Value(std::move(cancel_params)),
            });
            if(cancel_request) {
                self->enqueue_outgoing(std::move(*cancel_request));
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

task<Result<std::string>> Peer::send_request_json(std::string_view method,
                                                  std::string params_json,
                                                  std::chrono::milliseconds timeout) {
    if(timeout <= std::chrono::milliseconds::zero()) {
        co_return std::unexpected(
            RPCError(protocol::ErrorCode::RequestCancelled, "request timed out"));
    }

    auto timeout_source = std::make_shared<cancellation_source>();
    if(self) {
        self->loop.schedule(cancel_after_timeout(timeout, timeout_source, self->loop));
    }

    auto result =
        co_await send_request_json(method, std::move(params_json), timeout_source->token());
    if(!result &&
       result.error().code ==
           static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled) &&
       timeout_source->cancelled()) {
        co_return std::unexpected(
            RPCError(protocol::ErrorCode::RequestCancelled, "request timed out"));
    }

    co_return result;
}

Result<void> Peer::send_notification_json(std::string_view method, std::string params_json) {
    if(!self || !self->transport) {
        return std::unexpected("transport is null");
    }

    auto params =
        parse_json_value<protocol::Value>(params_json, protocol::ErrorCode::InternalError);
    if(!params) {
        return std::unexpected(params.error());
    }

    auto notification_json = detail::serialize_json(outgoing_notification_message{
        .method = std::string(method),
        .params = std::move(*params),
    });
    if(!notification_json) {
        return std::unexpected(notification_json.error());
    }

    self->enqueue_outgoing(std::move(*notification_json));
    return {};
}

}  // namespace eventide::jsonrpc
