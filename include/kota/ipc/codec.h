#pragma once

#include <string>
#include <variant>

#include "kota/ipc/protocol.h"
#include "kota/async/async.h"

namespace kota::ipc {

using Error = protocol::Error;

template <typename T>
using Result = outcome<T, Error>;

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
    Error error;
};

struct IncomingParseError {
    Error error;
};

using IncomingMessage = std::variant<IncomingRequest,
                                     IncomingNotification,
                                     IncomingResponse,
                                     IncomingErrorResponse,
                                     IncomingParseError>;

}  // namespace kota::ipc
