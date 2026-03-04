#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "eventide/jsonrpc/peer.h"
#include "eventide/zest/zest.h"
#include "eventide/common/compiler.h"
#include "eventide/async/loop.h"
#include "eventide/async/stream.h"
#include "eventide/async/sync.h"
#include "eventide/async/watcher.h"
#include "eventide/serde/json/simd_deserializer.h"

#ifdef _WIN32
#include <BaseTsd.h>
#include <fcntl.h>
#include <io.h>
using ssize_t = SSIZE_T;
#else
#include <unistd.h>
#endif

namespace eventide::jsonrpc {

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
    protocol::ResponseID id;
    protocol::ResponseError error;
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

class FakeTransport final : public Transport {
public:
    explicit FakeTransport(std::vector<std::string> incoming) :
        incoming_messages(std::move(incoming)) {}

    task<std::optional<std::string>> read_message() override {
        if(read_index >= incoming_messages.size()) {
            co_return std::nullopt;
        }
        co_return incoming_messages[read_index++];
    }

    task<bool> write_message(std::string_view payload) override {
        outgoing_messages.emplace_back(payload);
        co_return true;
    }

    const std::vector<std::string>& outgoing() const {
        return outgoing_messages;
    }

private:
    std::vector<std::string> incoming_messages;
    std::vector<std::string> outgoing_messages;
    std::size_t read_index = 0;
};

class ScriptedTransport final : public Transport {
public:
    using WriteHook = std::function<void(std::string_view, ScriptedTransport&)>;

    ScriptedTransport(std::vector<std::string> incoming, WriteHook hook) :
        incoming_messages(std::move(incoming)), write_hook(std::move(hook)) {
        if(!incoming_messages.empty()) {
            readable.set();
        }
    }

    task<std::optional<std::string>> read_message() override {
        while(read_index >= incoming_messages.size()) {
            if(closed) {
                co_return std::nullopt;
            }

            co_await readable.wait();
            readable.reset();
        }

        co_return incoming_messages[read_index++];
    }

    task<bool> write_message(std::string_view payload) override {
        outgoing_messages.emplace_back(payload);
        if(write_hook) {
            write_hook(payload, *this);
        }
        co_return true;
    }

    void push_incoming(std::string payload) {
        incoming_messages.push_back(std::move(payload));
        readable.set();
    }

    void close() {
        closed = true;
        readable.set();
    }

    const std::vector<std::string>& outgoing() const {
        return outgoing_messages;
    }

private:
    std::vector<std::string> incoming_messages;
    std::vector<std::string> outgoing_messages;
    std::size_t read_index = 0;
    WriteHook write_hook;
    event readable;
    bool closed = false;
};

struct PendingAddResult {
    Result<AddResult> value = std::unexpected("request not completed");
};

namespace {

#ifdef _WIN32
int create_pipe_fds(int fds[2]) {
    return _pipe(fds, 4096, _O_BINARY);
}

int close_fd(int fd) {
    return _close(fd);
}

ssize_t write_fd(int fd, const char* data, size_t len) {
    return _write(fd, data, static_cast<unsigned int>(len));
}
#else
int create_pipe_fds(int fds[2]) {
    return ::pipe(fds);
}

int close_fd(int fd) {
    return ::close(fd);
}

ssize_t write_fd(int fd, const char* data, size_t len) {
    return ::write(fd, data, len);
}
#endif

std::string frame(std::string_view payload) {
    std::string out;
    out.reserve(payload.size() + 32);
    out.append("Content-Length: ");
    out.append(std::to_string(payload.size()));
    out.append("\r\n\r\n");
    out.append(payload);
    return out;
}

task<> complete_request(Peer& peer, PendingAddResult& out) {
    out.value =
        co_await peer.send_request<AddResult>("worker/build", CustomAddParams{.a = 2, .b = 3});
    if(!peer.close_output() && out.value.has_value()) {
        out.value = std::unexpected("failed to close peer output");
    }
    co_return;
}

task<> write_notification_then_response(int fd, event_loop& loop) {
    co_await sleep(std::chrono::milliseconds{1}, loop);

    const auto note = frame(R"({"jsonrpc":"2.0","method":"test/note","params":{"text":"first"}})");
    auto note_written = write_fd(fd, note.data(), note.size());
    if(note_written != static_cast<ssize_t>(note.size())) {
        close_fd(fd);
        co_return;
    }

    co_await sleep(std::chrono::milliseconds{1}, loop);

    const auto response = frame(R"({"jsonrpc":"2.0","id":1,"result":{"sum":9}})");
    auto response_written = write_fd(fd, response.data(), response.size());
    if(response_written != static_cast<ssize_t>(response.size())) {
        close_fd(fd);
        co_return;
    }

    close_fd(fd);
    co_return;
}

}  // namespace

}  // namespace eventide::jsonrpc

namespace eventide::jsonrpc::protocol {

template <>
struct RequestTraits<AddParams> {
    using Result = AddResult;
    constexpr inline static std::string_view method = "test/add";
};

template <>
struct NotificationTraits<NoteParams> {
    constexpr inline static std::string_view method = "test/note";
};

}  // namespace eventide::jsonrpc::protocol

namespace eventide::jsonrpc {

TEST_SUITE(jsonrpc_peer) {

TEST_CASE(traits_dispatch_order) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":2,"b":3}})",
        R"({"jsonrpc":"2.0","method":"test/note","params":{"text":"first"}})",
        R"({"jsonrpc":"2.0","method":"test/note","params":{"text":"second"}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));
    std::vector<std::string> order;
    bool second_saw_first = false;
    bool first_seen = false;

    peer.on_request([&](RequestContext&, const AddParams& params) -> RequestResult<AddParams> {
        order.emplace_back("request");
        co_return AddResult{.sum = params.a + params.b};
    });

    peer.on_notification([&](const NoteParams& params) {
        if(params.text == "first") {
            first_seen = true;
            order.emplace_back("note:first");
            return;
        }
        if(params.text == "second") {
            second_saw_first = first_seen;
            order.emplace_back("note:second");
        }
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(order.size(), 3U);
    EXPECT_EQ(order[0], "note:first");
    EXPECT_EQ(order[1], "note:second");
    EXPECT_EQ(order[2], "request");
    EXPECT_TRUE(second_saw_first);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response = serde::json::simd::from_json<RPCResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    EXPECT_EQ(response->id.value, 1);
    ASSERT_TRUE(response->result.has_value());
    EXPECT_EQ(response->result->sum, 5);
}

TEST_CASE(stream_note_response) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    event_loop loop;

    int incoming_fds[2] = {-1, -1};
    int outgoing_fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe_fds(incoming_fds), 0);
    ASSERT_EQ(create_pipe_fds(outgoing_fds), 0);

    auto input = pipe::open(incoming_fds[0], pipe::options{}, loop);
    ASSERT_TRUE(input.has_value());
    auto output = pipe::open(outgoing_fds[1], pipe::options{}, loop);
    ASSERT_TRUE(output.has_value());

    auto transport =
        std::make_unique<StreamTransport>(stream(std::move(*input)), stream(std::move(*output)));
    Peer peer(loop, std::move(transport));

    std::vector<std::string> seen_notes;
    peer.on_notification("test/note",
                         [&](const NoteParams& params) { seen_notes.push_back(params.text); });

    PendingAddResult request_result;
    auto request = complete_request(peer, request_result);
    auto remote = write_notification_then_response(incoming_fds[1], loop);

    loop.schedule(peer.run());
    loop.schedule(request);
    loop.schedule(remote);

    EXPECT_EQ(loop.run(), 0);

    ASSERT_TRUE(request_result.value.has_value());
    EXPECT_EQ(request_result.value->sum, 9);
    ASSERT_EQ(seen_notes.size(), 1U);
    EXPECT_EQ(seen_notes.front(), "first");

    ASSERT_EQ(close_fd(outgoing_fds[0]), 0);
}

TEST_CASE(peers_share_loop) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    event_loop loop;

    auto transport1 = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":11,"method":"worker/one","params":{"a":2,"b":5}})",
    });
    auto* transport1_ptr = transport1.get();

    auto transport2 = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":22,"method":"worker/two","params":{"a":7,"b":3}})",
    });
    auto* transport2_ptr = transport2.get();

    Peer peer1(loop, std::move(transport1));
    Peer peer2(loop, std::move(transport2));

    peer1.on_request("worker/one",
                     [](RequestContext&, const AddParams& params) -> RequestResult<AddParams> {
                         co_return AddResult{.sum = params.a + params.b};
                     });

    peer2.on_request("worker/two",
                     [](RequestContext&, const AddParams& params) -> RequestResult<AddParams> {
                         co_return AddResult{.sum = params.a * params.b};
                     });

    loop.schedule(peer1.run());
    loop.schedule(peer2.run());

    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(transport1_ptr->outgoing().size(), 1U);
    auto response1 = serde::json::simd::from_json<RPCResponse>(transport1_ptr->outgoing().front());
    ASSERT_TRUE(response1.has_value());
    EXPECT_EQ(response1->id.value, 11);
    ASSERT_TRUE(response1->result.has_value());
    EXPECT_EQ(response1->result->sum, 7);

    ASSERT_EQ(transport2_ptr->outgoing().size(), 1U);
    auto response2 = serde::json::simd::from_json<RPCResponse>(transport2_ptr->outgoing().front());
    ASSERT_TRUE(response2.has_value());
    EXPECT_EQ(response2->id.value, 22);
    ASSERT_TRUE(response2->result.has_value());
    EXPECT_EQ(response2->result->sum, 21);
}

TEST_CASE(explicit_method) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":2,"method":"custom/add","params":{"a":7,"b":8}})",
        R"({"jsonrpc":"2.0","method":"custom/note","params":{"text":"hello"}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));
    std::string request_method;
    std::vector<std::string> notifications;

    peer.on_request(
        "custom/add",
        [&](RequestContext& context, const AddParams& params) -> RequestResult<AddParams> {
            request_method = std::string(context.method);
            co_return AddResult{.sum = params.a + params.b};
        });

    peer.on_notification("custom/note",
                         [&](const NoteParams& params) { notifications.push_back(params.text); });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    EXPECT_EQ(request_method, "custom/add");
    ASSERT_EQ(notifications.size(), 1U);
    EXPECT_EQ(notifications.front(), "hello");

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response = serde::json::simd::from_json<RPCResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->id.value, 2);
    ASSERT_TRUE(response->result.has_value());
    EXPECT_EQ(response->result->sum, 15);
}

TEST_CASE(request_notify_apis) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{
            R"({"jsonrpc":"2.0","id":7,"method":"test/add","params":{"a":2,"b":3}})",
        },
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("method":"client/add/context")") != std::string_view::npos) {
                channel.push_incoming(R"({"jsonrpc":"2.0","id":1,"result":{"sum":9}})");
                return;
            }

            if(payload.find(R"("method":"client/add/peer")") != std::string_view::npos) {
                channel.push_incoming(R"({"jsonrpc":"2.0","id":2,"result":{"sum":4}})");
                return;
            }

            if(payload.find(R"("id":7)") != std::string_view::npos &&
               (payload.find(R"("result")") != std::string_view::npos ||
                payload.find(R"("error")") != std::string_view::npos)) {
                channel.close();
            }
        });
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));
    std::string request_method;
    protocol::integer request_id = 0;

    peer.on_request([&](RequestContext& context,
                        const AddParams& params) -> RequestResult<AddParams> {
        request_method = std::string(context.method);
        request_id = static_cast<protocol::integer>(context.id.value);

        auto notify_from_context =
            context->send_notification("client/note/context", CustomNoteParams{.text = "context"});
        if(!notify_from_context) {
            co_return std::unexpected(notify_from_context.error());
        }

        auto notify_from_peer =
            peer.send_notification("client/note/peer", CustomNoteParams{.text = "peer"});
        if(!notify_from_peer) {
            co_return std::unexpected(notify_from_peer.error());
        }

        auto context_result = co_await context->send_request<AddResult>(
            "client/add/context",
            CustomAddParams{.a = params.a, .b = params.b});
        if(!context_result) {
            co_return std::unexpected(context_result.error());
        }

        auto peer_result =
            co_await peer.send_request<AddResult>("client/add/peer",
                                                  CustomAddParams{.a = params.b, .b = 1});
        if(!peer_result) {
            co_return std::unexpected(peer_result.error());
        }

        co_return AddResult{.sum = context_result->sum + peer_result->sum};
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    EXPECT_EQ(request_method, "test/add");
    EXPECT_EQ(request_id, 7);

    const auto& outgoing = transport_ptr->outgoing();
    ASSERT_EQ(outgoing.size(), 5U);

    auto note_from_context = serde::json::simd::from_json<RPCNotification>(outgoing[0]);
    ASSERT_TRUE(note_from_context.has_value());
    EXPECT_EQ(note_from_context->jsonrpc, "2.0");
    EXPECT_EQ(note_from_context->method, "client/note/context");
    EXPECT_EQ(note_from_context->params.text, "context");

    auto note_from_peer = serde::json::simd::from_json<RPCNotification>(outgoing[1]);
    ASSERT_TRUE(note_from_peer.has_value());
    EXPECT_EQ(note_from_peer->jsonrpc, "2.0");
    EXPECT_EQ(note_from_peer->method, "client/note/peer");
    EXPECT_EQ(note_from_peer->params.text, "peer");

    auto request_from_context = serde::json::simd::from_json<RPCRequest>(outgoing[2]);
    ASSERT_TRUE(request_from_context.has_value());
    EXPECT_EQ(request_from_context->jsonrpc, "2.0");
    EXPECT_EQ(request_from_context->id.value, 1);
    EXPECT_EQ(request_from_context->method, "client/add/context");
    EXPECT_EQ(request_from_context->params.a, 2);
    EXPECT_EQ(request_from_context->params.b, 3);

    auto request_from_peer = serde::json::simd::from_json<RPCRequest>(outgoing[3]);
    ASSERT_TRUE(request_from_peer.has_value());
    EXPECT_EQ(request_from_peer->jsonrpc, "2.0");
    EXPECT_EQ(request_from_peer->id.value, 2);
    EXPECT_EQ(request_from_peer->method, "client/add/peer");
    EXPECT_EQ(request_from_peer->params.a, 3);
    EXPECT_EQ(request_from_peer->params.b, 1);

    auto final_response = serde::json::simd::from_json<RPCResponse>(outgoing[4]);
    ASSERT_TRUE(final_response.has_value());
    EXPECT_EQ(final_response->jsonrpc, "2.0");
    EXPECT_EQ(final_response->id.value, 7);
    ASSERT_TRUE(final_response->result.has_value());
    EXPECT_EQ(final_response->result->sum, 13);
}

TEST_CASE(request_error_code) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":10,"method":"test/add","params":{"a":2,"b":3}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));

    peer.on_request([&](RequestContext&, const AddParams&) -> RequestResult<AddParams> {
        co_return std::unexpected(
            RPCError(protocol::ErrorCode::InvalidParams, "forced invalid params"));
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response =
        serde::json::simd::from_json<RPCErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    ASSERT_TRUE(response->id.has_value());
    EXPECT_EQ(response->id->value, 10);
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::InvalidParams));
    EXPECT_EQ(response->error.message, "forced invalid params");
}

TEST_CASE(request_error_data) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":12,"method":"test/add","params":{"a":2,"b":3}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));

    peer.on_request([&](RequestContext&, const AddParams&) -> RequestResult<AddParams> {
        protocol::Object data;
        data.insert_or_assign("detail", protocol::Value(std::string("invalid payload")));
        data.insert_or_assign("index", protocol::Value(std::int64_t{-3}));
        co_return std::unexpected(RPCError(protocol::ErrorCode::InvalidParams,
                                           "forced invalid params",
                                           protocol::Value(std::move(data))));
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response =
        serde::json::simd::from_json<RPCErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    ASSERT_TRUE(response->id.has_value());
    EXPECT_EQ(response->id->value, 12);
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::InvalidParams));
    EXPECT_EQ(response->error.message, "forced invalid params");
    ASSERT_TRUE(response->error.data.has_value());

    const auto& data_variant = static_cast<const protocol::Variant&>(*response->error.data);
    const auto* object = std::get_if<protocol::Object>(&data_variant);
    ASSERT_TRUE(object != nullptr);

    auto detail_it = object->find("detail");
    ASSERT_TRUE(detail_it != object->end());
    const auto& detail_variant = static_cast<const protocol::Variant&>(detail_it->second);
    ASSERT_TRUE(std::holds_alternative<std::string>(detail_variant));
    EXPECT_EQ(std::get<std::string>(detail_variant), "invalid payload");

    auto index_it = object->find("index");
    ASSERT_TRUE(index_it != object->end());
    const auto& index_variant = static_cast<const protocol::Variant&>(index_it->second);
    ASSERT_TRUE(std::holds_alternative<std::int64_t>(index_variant));
    EXPECT_EQ(std::get<std::int64_t>(index_variant), -3);
}

TEST_CASE(outbound_error_data) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{},
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("method":"worker/build")") == std::string_view::npos) {
                return;
            }

            channel.push_incoming(
                R"({"jsonrpc":"2.0","id":1,"error":{"code":-32001,"message":"remote failed","data":{"detail":"bad state","attempt":-1}}})");
            channel.close();
        });

    event_loop loop;
    Peer peer(loop, std::move(transport));
    Result<AddResult> request_result = std::unexpected("request did not complete");

    auto requester = [&]() -> task<> {
        request_result =
            co_await peer.send_request<AddResult>("worker/build", CustomAddParams{.a = 5, .b = 6});
        co_return;
    };

    auto request_task = requester();
    loop.schedule(peer.run());
    loop.schedule(request_task);
    EXPECT_EQ(loop.run(), 0);

    ASSERT_FALSE(request_result.has_value());
    EXPECT_EQ(request_result.error().code, -32001);
    EXPECT_EQ(request_result.error().message, "remote failed");
    ASSERT_TRUE(request_result.error().data.has_value());

    const auto& data_variant = static_cast<const protocol::Variant&>(*request_result.error().data);
    const auto* object = std::get_if<protocol::Object>(&data_variant);
    ASSERT_TRUE(object != nullptr);

    auto detail_it = object->find("detail");
    ASSERT_TRUE(detail_it != object->end());
    const auto& detail_variant = static_cast<const protocol::Variant&>(detail_it->second);
    ASSERT_TRUE(std::holds_alternative<std::string>(detail_variant));
    EXPECT_EQ(std::get<std::string>(detail_variant), "bad state");

    auto attempt_it = object->find("attempt");
    ASSERT_TRUE(attempt_it != object->end());
    const auto& attempt_variant = static_cast<const protocol::Variant&>(attempt_it->second);
    ASSERT_TRUE(std::holds_alternative<std::int64_t>(attempt_variant));
    EXPECT_EQ(std::get<std::int64_t>(attempt_variant), -1);
}

TEST_CASE(bad_response_silent) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{},
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("method":"worker/build")") == std::string_view::npos) {
                return;
            }

            channel.push_incoming(R"({"jsonrpc":"2.0","id":1,"\uD800":0})");
            channel.close();
        });
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));
    Result<AddResult> request_result = std::unexpected("request did not complete");

    auto requester = [&]() -> task<> {
        request_result =
            co_await peer.send_request<AddResult>("worker/build", CustomAddParams{.a = 5, .b = 6});
        co_return;
    };

    auto request_task = requester();
    loop.schedule(peer.run());
    loop.schedule(request_task);
    EXPECT_EQ(loop.run(), 0);

    ASSERT_FALSE(request_result.has_value());
    EXPECT_EQ(request_result.error().code,
              static_cast<protocol::integer>(protocol::ErrorCode::InvalidRequest));
    EXPECT_FALSE(request_result.error().message.empty());

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto request = serde::json::simd::from_json<RPCRequest>(transport_ptr->outgoing().front());
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->method, "worker/build");
}

TEST_CASE(bad_params_invalid) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":11,"method":"test/add","params":"invalid"})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));
    bool invoked = false;

    peer.on_request([&](RequestContext&, const AddParams&) -> RequestResult<AddParams> {
        invoked = true;
        co_return AddResult{.sum = 0};
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    EXPECT_FALSE(invoked);
    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response =
        serde::json::simd::from_json<RPCErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    ASSERT_TRUE(response->id.has_value());
    EXPECT_EQ(response->id->value, 11);
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::InvalidParams));
    EXPECT_FALSE(response->error.message.empty());
}

TEST_CASE(malformed_parse_null) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/add")",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response =
        serde::json::simd::from_json<RPCErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    EXPECT_FALSE(response->id.has_value());
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::ParseError));
    EXPECT_FALSE(response->error.message.empty());
}

TEST_CASE(invalid_request_null) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response =
        serde::json::simd::from_json<RPCErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    EXPECT_FALSE(response->id.has_value());
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::InvalidRequest));
    EXPECT_EQ(response->error.message, "message must contain method or id");
}

TEST_CASE(invalid_id_type) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":"11","method":"test/note","params":{"text":"x"}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));
    bool invoked = false;

    peer.on_notification([&](const NoteParams&) { invoked = true; });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    EXPECT_FALSE(invoked);
    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response =
        serde::json::simd::from_json<RPCErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    EXPECT_FALSE(response->id.has_value());
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::InvalidRequest));
    EXPECT_EQ(response->error.message, "request id must be integer or null");
}

TEST_CASE(cancel_inflight_request) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":21,"method":"test/add","params":{"a":2,"b":3}})",
        R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":21}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));
    bool finished = false;

    peer.on_request([&](RequestContext&, const AddParams& params) -> RequestResult<AddParams> {
        co_await sleep(std::chrono::milliseconds{10}, loop);
        finished = true;
        co_return AddResult{.sum = params.a + params.b};
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    EXPECT_FALSE(finished);
    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response =
        serde::json::simd::from_json<RPCErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    ASSERT_TRUE(response->id.has_value());
    EXPECT_EQ(response->id->value, 21);
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT_EQ(response->error.message, "request cancelled");
}

TEST_CASE(cancel_running_handler) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{
            R"({"jsonrpc":"2.0","id":22,"method":"test/add","params":{"a":2,"b":3}})",
        },
        nullptr);
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));
    bool started = false;
    bool completed = false;
    event handler_started;

    peer.on_request([&](RequestContext&, const AddParams& params) -> RequestResult<AddParams> {
        started = true;
        handler_started.set();
        co_await sleep(std::chrono::milliseconds{20}, loop);
        completed = true;
        co_return AddResult{.sum = params.a + params.b};
    });

    auto canceler = [&]() -> task<> {
        co_await handler_started.wait();
        co_await sleep(std::chrono::milliseconds{1}, loop);
        transport_ptr->push_incoming(
            R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":22}})");
        co_await sleep(std::chrono::milliseconds{5}, loop);
        transport_ptr->close();
    };

    auto cancel_task = canceler();
    loop.schedule(peer.run());
    loop.schedule(cancel_task);
    EXPECT_EQ(loop.run(), 0);

    EXPECT_TRUE(started);
    EXPECT_FALSE(completed);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response =
        serde::json::simd::from_json<RPCErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    ASSERT_TRUE(response->id.has_value());
    EXPECT_EQ(response->id->value, 22);
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT_EQ(response->error.message, "request cancelled");
}

TEST_CASE(context_token_propagates) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{
            R"({"jsonrpc":"2.0","id":31,"method":"test/add","params":{"a":4,"b":5}})",
        },
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("method":"client/add/context")") != std::string_view::npos) {
                channel.push_incoming(
                    R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":31}})");
                return;
            }

            if(payload.find(R"("method":"$/cancelRequest")") != std::string_view::npos) {
                channel.close();
            }
        });
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));
    bool started = false;

    peer.on_request([&](RequestContext& context,
                        const AddParams& params) -> RequestResult<AddParams> {
        started = true;

        auto nested_result =
            co_await context->send_request<AddResult>("client/add/context",
                                                      CustomAddParams{.a = params.a, .b = params.b},
                                                      context.cancellation);
        if(!nested_result) {
            co_return std::unexpected(nested_result.error());
        }

        co_return AddResult{.sum = nested_result->sum};
    });

    auto watchdog = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{20}, loop);
        transport_ptr->close();
    };

    auto watchdog_task = watchdog();
    loop.schedule(peer.run());
    loop.schedule(watchdog_task);
    EXPECT_EQ(loop.run(), 0);

    EXPECT_TRUE(started);

    const auto& outgoing = transport_ptr->outgoing();
    ASSERT_EQ(outgoing.size(), 3U);

    auto nested_request = serde::json::simd::from_json<RPCRequest>(outgoing[0]);
    ASSERT_TRUE(nested_request.has_value());
    EXPECT_EQ(nested_request->jsonrpc, "2.0");
    EXPECT_EQ(nested_request->id.value, 1);
    EXPECT_EQ(nested_request->method, "client/add/context");
    EXPECT_EQ(nested_request->params.a, 4);
    EXPECT_EQ(nested_request->params.b, 5);

    auto nested_cancel = serde::json::simd::from_json<RPCCancelNotification>(outgoing[1]);
    ASSERT_TRUE(nested_cancel.has_value());
    EXPECT_EQ(nested_cancel->jsonrpc, "2.0");
    EXPECT_EQ(nested_cancel->method, "$/cancelRequest");
    EXPECT_EQ(nested_cancel->params.id.value, 1);

    auto final_error = serde::json::simd::from_json<RPCErrorResponse>(outgoing[2]);
    ASSERT_TRUE(final_error.has_value());
    EXPECT_EQ(final_error->jsonrpc, "2.0");
    ASSERT_TRUE(final_error->id.has_value());
    EXPECT_EQ(final_error->id->value, 31);
    EXPECT_EQ(final_error->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT_EQ(final_error->error.message, "request cancelled");
}

TEST_CASE(outbound_cancel_request) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{},
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("method":"$/cancelRequest")") != std::string_view::npos) {
                channel.close();
            }
        });
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));
    cancellation_source source;
    Result<AddResult> request_result = std::unexpected("request did not complete");

    auto requester = [&]() -> task<> {
        request_result = co_await peer.send_request<AddResult>("worker/build",
                                                               CustomAddParams{.a = 5, .b = 6},
                                                               source.token());
        co_return;
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        source.cancel();
    };

    auto request_task = requester();
    auto cancel_task = canceler();

    loop.schedule(peer.run());
    loop.schedule(request_task);
    loop.schedule(cancel_task);
    EXPECT_EQ(loop.run(), 0);

    ASSERT_FALSE(request_result.has_value());
    EXPECT_EQ(request_result.error().code,
              static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT_EQ(request_result.error().message, "request cancelled");

    const auto& outgoing = transport_ptr->outgoing();
    ASSERT_EQ(outgoing.size(), 2U);

    auto request = serde::json::simd::from_json<RPCRequest>(outgoing[0]);
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->jsonrpc, "2.0");
    EXPECT_EQ(request->id.value, 1);
    EXPECT_EQ(request->method, "worker/build");

    auto cancel = serde::json::simd::from_json<RPCCancelNotification>(outgoing[1]);
    ASSERT_TRUE(cancel.has_value());
    EXPECT_EQ(cancel->jsonrpc, "2.0");
    EXPECT_EQ(cancel->method, "$/cancelRequest");
    EXPECT_EQ(cancel->params.id.value, 1);
}

TEST_CASE(outbound_precancel) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<ScriptedTransport>(std::vector<std::string>{}, nullptr);
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));
    cancellation_source source;
    source.cancel();
    Result<AddResult> request_result = std::unexpected("request did not complete");

    auto requester = [&]() -> task<> {
        request_result = co_await peer.send_request<AddResult>("worker/build",
                                                               CustomAddParams{.a = 1, .b = 2},
                                                               source.token());
        co_return;
    };

    auto closer = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        transport_ptr->close();
    };

    auto request_task = requester();
    auto close_task = closer();
    loop.schedule(peer.run());
    loop.schedule(request_task);
    loop.schedule(close_task);
    EXPECT_EQ(loop.run(), 0);

    ASSERT_FALSE(request_result.has_value());
    EXPECT_EQ(request_result.error().code,
              static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT_EQ(request_result.error().message, "request cancelled");
    EXPECT_TRUE(transport_ptr->outgoing().empty());
}

TEST_CASE(outbound_timeout_cancel) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{},
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("method":"$/cancelRequest")") != std::string_view::npos) {
                channel.close();
            }
        });
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));
    Result<AddResult> request_result = std::unexpected("request did not complete");

    auto requester = [&]() -> task<> {
        request_result = co_await peer.send_request<AddResult>("worker/build",
                                                               CustomAddParams{.a = 8, .b = 9},
                                                               std::chrono::milliseconds{1});
        co_return;
    };

    auto request_task = requester();
    loop.schedule(peer.run());
    loop.schedule(request_task);
    EXPECT_EQ(loop.run(), 0);

    ASSERT_FALSE(request_result.has_value());
    EXPECT_EQ(request_result.error().code,
              static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT_EQ(request_result.error().message, "request timed out");

    const auto& outgoing = transport_ptr->outgoing();
    ASSERT_EQ(outgoing.size(), 2U);

    auto request = serde::json::simd::from_json<RPCRequest>(outgoing[0]);
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->method, "worker/build");

    auto cancel = serde::json::simd::from_json<RPCCancelNotification>(outgoing[1]);
    ASSERT_TRUE(cancel.has_value());
    EXPECT_EQ(cancel->method, "$/cancelRequest");
}

TEST_CASE(zero_timeout_cancel) {
#if EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
    skip();
    return;
#endif
    auto transport = std::make_unique<ScriptedTransport>(std::vector<std::string>{}, nullptr);
    auto* transport_ptr = transport.get();

    event_loop loop;
    Peer peer(loop, std::move(transport));
    Result<AddResult> request_result = std::unexpected("request did not complete");

    auto requester = [&]() -> task<> {
        request_result = co_await peer.send_request<AddResult>("worker/build",
                                                               CustomAddParams{.a = 1, .b = 1},
                                                               std::chrono::milliseconds{0});
        co_return;
    };

    auto closer = [&]() -> task<> {
        co_await sleep(std::chrono::milliseconds{1}, loop);
        transport_ptr->close();
    };

    auto request_task = requester();
    auto close_task = closer();
    loop.schedule(peer.run());
    loop.schedule(request_task);
    loop.schedule(close_task);
    EXPECT_EQ(loop.run(), 0);

    ASSERT_FALSE(request_result.has_value());
    EXPECT_EQ(request_result.error().code,
              static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT_EQ(request_result.error().message, "request timed out");
    EXPECT_TRUE(transport_ptr->outgoing().empty());
}

};  // TEST_SUITE(jsonrpc_peer)

}  // namespace eventide::jsonrpc
