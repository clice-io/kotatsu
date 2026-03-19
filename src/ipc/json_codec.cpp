#include "eventide/ipc/json_codec.h"

#include <string>
#include <string_view>

#include "eventide/serde/json/json.h"

namespace eventide::ipc {

namespace {

template <typename T>
Result<std::string>
    serialize_json_value(const T& value,
                         protocol::ErrorCode code = protocol::ErrorCode::InternalError) {
    auto serialized = serde::json::to_string(value);
    if(!serialized) {
        return outcome_error(
            Error(code, std::string(serde::json::error_message(serialized.error()))));
    }
    return std::move(*serialized);
}

// --- Outgoing message structs (use RawValue to avoid double serialization) ---

struct outgoing_request_message {
    std::string jsonrpc = "2.0";
    protocol::RequestID id;
    std::string method;
    serde::RawValue params;
};

struct outgoing_notification_message {
    std::string jsonrpc = "2.0";
    std::string method;
    serde::RawValue params;
};

struct outgoing_success_response_message {
    std::string jsonrpc = "2.0";
    protocol::RequestID id;
    serde::RawValue result;
};

struct outgoing_error_response_message {
    std::string jsonrpc = "2.0";
    protocol::RequestID id;
    Error error;
};

// --- Incoming message envelope (deserialized via serde) ---

struct json_rpc_incoming {
    std::optional<protocol::RequestID> id;
    std::optional<std::string> method;
    std::optional<serde::RawValue> params;
    serde::RawValue result;
    std::optional<Error> error;
};

}  // namespace

IncomingMessage JsonCodec::parse_message(std::string_view payload) {
    auto envelope = serde::json::parse<json_rpc_incoming>(payload);
    if(!envelope) {
        return IncomingParseError{Error(protocol::ErrorCode::ParseError,
                                        std::string(serde::json::error_message(envelope.error())))};
    }

    auto raw_params =
        envelope->params.has_value() ? std::move(envelope->params->data) : std::string{};

    // Has method → request or notification
    if(envelope->method.has_value()) {
        if(envelope->id.has_value()) {
            return IncomingRequest{*envelope->id,
                                   std::move(*envelope->method),
                                   std::move(raw_params)};
        }
        return IncomingNotification{std::move(*envelope->method), std::move(raw_params)};
    }

    // No method + has id → response
    if(envelope->id.has_value()) {
        auto has_result = !envelope->result.empty();
        auto has_error = envelope->error.has_value();

        if(has_error && !has_result) {
            return IncomingErrorResponse{*envelope->id, std::move(*envelope->error)};
        }
        if(has_result && !has_error) {
            return IncomingResponse{*envelope->id, std::move(envelope->result.data)};
        }
        return IncomingErrorResponse{*envelope->id,
                                     Error(protocol::ErrorCode::InvalidRequest,
                                           "response must contain exactly one of result or error")};
    }

    // No method + no valid id → invalid
    return IncomingParseError{
        Error(protocol::ErrorCode::InvalidRequest, "message must contain method or id")};
}

Result<std::string> JsonCodec::encode_request(const protocol::RequestID& id,
                                              std::string_view method,
                                              std::string_view params) {
    return serialize_json_value(outgoing_request_message{
        .id = id,
        .method = std::string(method),
        .params = serde::RawValue{std::string(params)},
    });
}

Result<std::string> JsonCodec::encode_notification(std::string_view method,
                                                   std::string_view params) {
    return serialize_json_value(outgoing_notification_message{
        .method = std::string(method),
        .params = serde::RawValue{std::string(params)},
    });
}

Result<std::string> JsonCodec::encode_success_response(const protocol::RequestID& id,
                                                       std::string_view result) {
    return serialize_json_value(outgoing_success_response_message{
        .id = id,
        .result = serde::RawValue{std::string(result)},
    });
}

Result<std::string> JsonCodec::encode_error_response(const protocol::RequestID& id,
                                                     const Error& error) {
    return serialize_json_value(outgoing_error_response_message{
        .id = id,
        .error = error,
    });
}

}  // namespace eventide::ipc
