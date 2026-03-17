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

struct Response {
    std::string jsonrpc;
    protocol::RequestID id;
    std::optional<AddResult> result = {};
};

struct ErrorResponse {
    std::string jsonrpc;
    protocol::RequestID id;
    protocol::Error error;
};

struct Request {
    std::string jsonrpc;
    protocol::RequestID id;
    std::string method;
    AddParams params;
};

struct Notification {
    std::string jsonrpc;
    std::string method;
    NoteParams params;
};

struct CancelParams {
    protocol::RequestID id;
};

struct CancelNotification {
    std::string jsonrpc;
    std::string method;
    CancelParams params;
};

// Types with multi-word fields for camelCase rename testing
struct RangeAddParams {
    std::int64_t first_value = 0;
    std::int64_t second_value = 0;
};

struct RangeAddResult {
    std::int64_t computed_sum = 0;
};

struct StatusNoteParams {
    std::string display_name;
    std::int64_t retry_count = 0;
};

using RequestContext = JsonPeer::RequestContext;

struct PendingAddResult {
    Result<AddResult> value = outcome_error(Error("request not completed"));
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

template <>
struct RequestTraits<RangeAddParams> {
    using Result = RangeAddResult;
    constexpr inline static std::string_view method = "test/rangeAdd";
};

template <>
struct NotificationTraits<StatusNoteParams> {
    constexpr inline static std::string_view method = "test/statusNote";
};

}  // namespace eventide::ipc::protocol
