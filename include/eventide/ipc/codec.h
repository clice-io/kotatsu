#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "eventide/ipc/protocol.h"

namespace eventide::ipc {

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

/// Parsed incoming message envelope (codec-agnostic).
struct IncomingMessage {
    std::optional<std::string> method;
    std::optional<protocol::RequestID> id;
    bool id_is_null = false;
    std::string params;                   // raw serialized params (empty if absent)
    std::string result;                   // raw serialized result (empty if absent)
    std::optional<RPCError> error;        // parsed error from response envelope
    std::optional<RPCError> parse_error;  // set when the message itself is malformed
};

/// Abstract codec for serializing/deserializing message envelopes.
class Codec {
public:
    virtual ~Codec() = default;

    /// Parse an incoming transport frame into a structured message.
    /// Always returns an IncomingMessage. If parsing fails, parse_error is set
    /// with whatever partial state (id, method) was extracted before the error.
    virtual IncomingMessage parse_message(std::string_view payload) = 0;

    /// Encode an outgoing request envelope.
    virtual Result<std::string> encode_request(const protocol::RequestID& id,
                                               std::string_view method,
                                               std::string_view params) = 0;

    /// Encode an outgoing notification envelope.
    virtual Result<std::string> encode_notification(std::string_view method,
                                                    std::string_view params) = 0;

    /// Encode a success response envelope. `result` is the already-serialized result payload.
    virtual Result<std::string> encode_success_response(const protocol::RequestID& id,
                                                        std::string_view result) = 0;

    /// Encode an error response envelope.
    virtual Result<std::string> encode_error_response(const protocol::ResponseID& id,
                                                      const RPCError& error) = 0;

    /// Parse a $/cancelRequest params blob to extract the request ID.
    virtual std::optional<protocol::RequestID> parse_cancel_id(std::string_view params) = 0;
};

}  // namespace eventide::ipc
