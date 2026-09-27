#pragma once

#include <optional>
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
    /// Absent when the response names no request, as the answer to a message
    /// its sender could not read does; such a response is never answered.
    std::optional<protocol::RequestID> id;
    Error error;
};

/// A message that is no valid request, notification or response: it is
/// answered with `error`, under the id of the request it was meant to be when
/// that id could be read.
struct IncomingParseError {
    std::optional<protocol::RequestID> id;
    Error error;
};

using IncomingMessage = std::variant<IncomingRequest,
                                     IncomingNotification,
                                     IncomingResponse,
                                     IncomingErrorResponse,
                                     IncomingParseError>;

}  // namespace kota::ipc
