#pragma once

#include <expected>
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
    protocol::RequestID id;               // default absent
    std::string params;                   // raw serialized params (empty if absent)
    std::string result;                   // raw serialized result (empty if absent)
    std::optional<RPCError> error;        // parsed error from response envelope
    std::optional<RPCError> parse_error;  // set when the message itself is malformed
};

}  // namespace eventide::ipc
