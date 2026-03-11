#include "eventide/ipc/json_codec.h"

#include <string>
#include <string_view>

#include "eventide/serde/json/json.h"

namespace eventide::ipc {

namespace {

template <typename T>
Result<T> parse_json_value(std::string_view json,
                           protocol::ErrorCode code = protocol::ErrorCode::RequestFailed) {
    auto parsed = serde::json::parse<T>(json);
    if(!parsed) {
        return outcome_error(
            RPCError(code, std::string(serde::json::error_message(parsed.error()))));
    }
    return std::move(*parsed);
}

template <typename T>
Result<std::string>
    serialize_json_value(const T& value,
                         protocol::ErrorCode code = protocol::ErrorCode::InternalError) {
    auto serialized = serde::json::to_string(value);
    if(!serialized) {
        return outcome_error(
            RPCError(code, std::string(serde::json::error_message(serialized.error()))));
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
    protocol::ResponseError error;
};

// --- Incoming message envelope (deserialized via serde) ---

struct json_rpc_incoming {
    protocol::RequestID id;
    std::optional<std::string> method;
    std::optional<serde::RawValue> params;
    std::optional<serde::RawValue> result;
    std::optional<protocol::ResponseError> error;
};

}  // namespace

IncomingMessage JsonCodec::parse_message(std::string_view payload) {
    IncomingMessage msg;

    auto envelope = serde::json::parse<json_rpc_incoming>(payload);
    if(!envelope) {
        msg.parse_error = RPCError(protocol::ErrorCode::ParseError,
                                   std::string(serde::json::error_message(envelope.error())));
        return msg;
    }

    msg.id = envelope->id;

    if(envelope->method.has_value()) {
        msg.method = std::move(*envelope->method);
    }

    if(envelope->params.has_value()) {
        msg.params = std::move(envelope->params->data);
    }

    if(envelope->result.has_value()) {
        msg.result = std::move(envelope->result->data);
    }

    if(envelope->error.has_value()) {
        msg.error = RPCError(envelope->error->code,
                             std::move(envelope->error->message),
                             std::move(envelope->error->data));
    }

    return msg;
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
                                                     const RPCError& error) {
    return serialize_json_value(outgoing_error_response_message{
        .id = id,
        .error = protocol::ResponseError{error.code, error.message, error.data},
    });
}

std::optional<protocol::RequestID> JsonCodec::parse_cancel_id(std::string_view params) {
    struct cancel_request_params {
        protocol::RequestID id;
    };

    auto parsed =
        parse_json_value<cancel_request_params>(params, protocol::ErrorCode::InvalidParams);
    if(!parsed) {
        return std::nullopt;
    }
    return parsed->id;
}

}  // namespace eventide::ipc
