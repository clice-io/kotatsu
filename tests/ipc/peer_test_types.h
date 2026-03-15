#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "test_transport.h"
#include "../common/fd_helpers.h"
#include "eventide/ipc/peer.h"
#include "eventide/common/config.h"
#include "eventide/async/async.h"
#include "eventide/serde/json/deserializer.h"

namespace eventide::ipc {

struct AddParams {
    std::int64_t a = 0;
    std::int64_t b = 0;
};

struct AddResult {
    std::int64_t sum = 0;
};

struct NoteParams {
    std::string text;
};

struct CustomAddParams {
    std::int64_t a = 0;
    std::int64_t b = 0;
};

struct CustomNoteParams {
    std::string text;
};

struct RPCResponse {
    std::string jsonrpc;
    protocol::RequestID id;
    std::optional<AddResult> result = {};
};

struct RPCErrorResponse {
    std::string jsonrpc;
    protocol::RequestID id;
    protocol::RPCError error;
};

struct RPCRequest {
    std::string jsonrpc;
    protocol::RequestID id;
    std::string method;
    AddParams params;
};

struct RPCNotification {
    std::string jsonrpc;
    std::string method;
    NoteParams params;
};

struct CancelParams {
    protocol::RequestID id;
};

struct RPCCancelNotification {
    std::string jsonrpc;
    std::string method;
    CancelParams params;
};

using RequestContext = JsonPeer::RequestContext;

struct PendingAddResult {
    Result<AddResult> value = outcome_error(RPCError("request not completed"));
};

using test::create_pipe;
using test::close_fd;
using test::write_fd;

}  // namespace eventide::ipc

namespace eventide::ipc::protocol {

template <>
struct RequestTraits<AddParams> {
    using Result = AddResult;
    constexpr inline static std::string_view method = "test/add";
};

template <>
struct NotificationTraits<NoteParams> {
    constexpr inline static std::string_view method = "test/note";
};

}  // namespace eventide::ipc::protocol
