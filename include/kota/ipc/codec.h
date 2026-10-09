#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "kota/ipc/protocol.h"
#include "kota/support/naming.h"
#include "kota/async/async.h"

namespace kota::ipc {

using Error = protocol::Error;

/// LSP's member names: lower camel case, as JSONCodec writes them.
struct lsp_config {
    using field_rename = naming::rename_policy::lower_camel;
};

template <typename T>
using Result = outcome<T, Error>;

/// The params or result of a message as it came in: `size` bytes from
/// `offset` in the payload, which it keeps rather than a copy of them. The
/// codec decodes it in place, writing over what follows it in the payload.
struct PayloadSlice {
    std::string payload;
    std::size_t offset = 0;
    std::size_t size = 0;

    std::string_view text() const {
        return std::string_view(payload).substr(offset, size);
    }
};

/// Typed incoming message alternatives (codec-agnostic). Empty params are
/// those of a method that takes none.
struct IncomingRequest {
    protocol::RequestID id;
    std::string method;
    PayloadSlice params;
};

struct IncomingNotification {
    std::string method;
    PayloadSlice params;
};

struct IncomingResponse {
    protocol::RequestID id;
    PayloadSlice result;
};

struct IncomingErrorResponse {
    /// Absent when the response names no request, as the answer to a message
    /// its sender could not read does; such a response is never answered.
    std::optional<protocol::RequestID> id;
    Error error;
};

/// A message that is no valid request, notification or response: it is
/// answered with `error`, under the id of the request it was meant to be when
/// that id could be read. A notification that cannot be read is never
/// answered; the error is only logged.
struct IncomingParseError {
    std::optional<protocol::RequestID> id;
    Error error;
    bool notification = false;
};

/// What the first bytes of a message tell of it, when the rest is not read.
struct MessageHead {
    enum class Kind : std::uint8_t {
        Unknown,
        Request,
        Notification,
        Response,
    };

    Kind kind = Kind::Unknown;
    /// The id of a request, or of the request a response answers. A
    /// Response without one names no request: its id is null.
    std::optional<protocol::RequestID> id = {};
};

using IncomingMessage = std::variant<IncomingRequest,
                                     IncomingNotification,
                                     IncomingResponse,
                                     IncomingErrorResponse,
                                     IncomingParseError>;

}  // namespace kota::ipc
