#include "kota/codec/json/json.h"

#include <string>
#include <string_view>

#include "kota/ipc/codec/json.h"
#include "kota/codec/dyn/dyn.h"
#include "kota/codec/macro.h"

namespace kota::ipc {

namespace {

struct outgoing_request_message {
    std::string jsonrpc = "2.0";
    protocol::RequestID id;
    std::string method;
    codec::RawValue params;
};

struct outgoing_notification_message {
    std::string jsonrpc = "2.0";
    std::string method;
    codec::RawValue params;
};

struct outgoing_success_response_message {
    std::string jsonrpc = "2.0";
    protocol::RequestID id;
    codec::RawValue result;
};

struct outgoing_error_response_message {
    std::string jsonrpc = "2.0";
    /// Written as null when the error answers a message whose id is unknown.
    std::optional<protocol::RequestID> id;
    Error error;
};

struct json_rpc_incoming {
    // RawValue, not optional<RequestID>, so that a null id stays apart from
    // a missing one: absent → empty(), null → "null" text.
    KOTATSU_ANNOTATE(defaulted = true)
    <codec::RawValue> id;
    std::optional<std::string> method;
    std::optional<codec::RawValue> params;
    // Not optional<RawValue> because "result": null is a valid success
    // response — optional would lose it as nullopt.
    KOTATSU_ANNOTATE(defaulted = true)
    <codec::RawValue> result;
    std::optional<Error> error;
};

/// The request id `raw`, an id member as written, holds: nothing for a null,
/// or for a value that is neither an integer nor a string.
std::optional<protocol::RequestID> read_id(std::string_view raw) {
    auto id = codec::json::from_string<protocol::RequestID>(raw);
    if(!id) {
        return std::nullopt;
    }
    return std::move(*id);
}

/// What to make of a message whose envelope did not decode. Text that is no
/// JSON is a parse error; JSON that is no message object is an invalid
/// request (batches are not supported). An object is read again, leniently,
/// for the id it names: a request (it has a method) is answered as invalid
/// under that id, and a response fails the request it answers, or is only
/// logged when its id cannot be read.
IncomingMessage read_malformed(std::string_view payload, std::string reason) {
    auto document = codec::json::from_string<codec::dyn::Value>(payload);
    if(!document) {
        return IncomingParseError{std::nullopt,
                                  Error(protocol::ErrorCode::ParseError, std::move(reason))};
    }
    const auto* object = document->get_object();
    if(object == nullptr) {
        return IncomingParseError{std::nullopt,
                                  Error(protocol::ErrorCode::InvalidRequest,
                                        document->is_array() ? "batch messages are not supported"
                                                             : "message must be an object")};
    }

    std::optional<protocol::RequestID> id;
    if(const auto* member = object->find("id")) {
        if(auto number = member->get_int()) {
            id = *number;
        } else if(auto text = member->get_string()) {
            id = std::string(*text);
        }
    }
    if(object->contains("method")) {
        return IncomingParseError{std::move(id),
                                  Error(protocol::ErrorCode::InvalidRequest, std::move(reason))};
    }
    return IncomingErrorResponse{
        std::move(id),
        Error(protocol::ErrorCode::InvalidRequest, "malformed response: " + reason)};
}

}  // namespace

IncomingMessage JsonCodec::parse_message(std::string_view payload) {
    auto envelope = codec::json::from_string<json_rpc_incoming>(payload);
    if(!envelope) {
        return read_malformed(payload, envelope.error().to_string());
    }

    const bool has_id = !envelope->id.empty();
    auto id = has_id ? read_id(envelope->id.data) : std::nullopt;

    if(envelope->method.has_value()) {
        auto params =
            envelope->params.has_value() ? std::move(envelope->params->data) : std::string{};
        if(!has_id) {
            return IncomingNotification{std::move(*envelope->method), std::move(params)};
        }
        if(!id) {
            return IncomingParseError{std::nullopt,
                                      Error(protocol::ErrorCode::InvalidRequest,
                                            "request id must be an integer or a string")};
        }
        return IncomingRequest{std::move(*id), std::move(*envelope->method), std::move(params)};
    }

    const bool has_result = !envelope->result.empty();
    const bool has_error = envelope->error.has_value();
    if(!has_id && !has_result && !has_error) {
        return IncomingParseError{
            std::nullopt,
            Error(protocol::ErrorCode::InvalidRequest, "message must contain method or id")};
    }
    if(has_result == has_error) {
        return IncomingErrorResponse{std::move(id),
                                     Error(protocol::ErrorCode::InvalidRequest,
                                           "response must contain exactly one of result or error")};
    }
    if(has_error) {
        return IncomingErrorResponse{std::move(id), std::move(*envelope->error)};
    }
    if(!id) {
        return IncomingErrorResponse{std::nullopt,
                                     Error(protocol::ErrorCode::InvalidRequest,
                                           "response id must be an integer or a string")};
    }
    return IncomingResponse{std::move(*id), std::move(envelope->result.data)};
}

Result<std::string> JsonCodec::encode_request(const protocol::RequestID& id,
                                              std::string_view method,
                                              std::string_view params) {
    return serialize_value(outgoing_request_message{
        .id = id,
        .method = std::string(method),
        .params = codec::RawValue{std::string(params)},
    });
}

Result<std::string> JsonCodec::encode_notification(std::string_view method,
                                                   std::string_view params) {
    return serialize_value(outgoing_notification_message{
        .method = std::string(method),
        .params = codec::RawValue{std::string(params)},
    });
}

Result<std::string> JsonCodec::encode_success_response(const protocol::RequestID& id,
                                                       std::string_view result) {
    return serialize_value(outgoing_success_response_message{
        .id = id,
        .result = codec::RawValue{std::string(result)},
    });
}

Result<std::string> JsonCodec::encode_error_response(const std::optional<protocol::RequestID>& id,
                                                     const Error& error) {
    return serialize_value(outgoing_error_response_message{
        .id = id,
        .error = error,
    });
}

template class Peer<JsonCodec>;

}  // namespace kota::ipc
