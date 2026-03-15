#pragma once

#include <string>
#include <variant>

#include "eventide/ipc/protocol.h"
#include "eventide/async/async.h"

namespace eventide::ipc {

using RPCError = protocol::RPCError;

template <typename T>
using Result = outcome<T, RPCError>;

/// Typed incoming message alternatives (codec-agnostic).
struct IncomingRequest {
    protocol::RequestID id;
    std::string method;
    std::string params;
};

struct IncomingNotification {
    std::string method;
    std::string params;
};

struct IncomingResponse {
    protocol::RequestID id;
    std::string result;
};

struct IncomingErrorResponse {
    protocol::RequestID id;
    RPCError error;
};

struct IncomingParseError {
    RPCError error;
};

using IncomingMessage = std::variant<IncomingRequest,
                                     IncomingNotification,
                                     IncomingResponse,
                                     IncomingErrorResponse,
                                     IncomingParseError>;

}  // namespace eventide::ipc
