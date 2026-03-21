#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../test_transport.h"
#include "eventide/ipc/peer.h"
#include "eventide/zest/zest.h"
#include "eventide/async/async.h"
#include "eventide/serde/json/deserializer.h"
#include "eventide/ipc/lsp/protocol.h"

namespace eventide::ipc::lsp {

namespace ipc = eventide::ipc;

using ipc::FakeTransport;
using ipc::ScriptedTransport;

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
    protocol::optional<AddResult> result = {};
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

}  // namespace eventide::ipc::lsp

template <>
struct eventide::ipc::protocol::RequestTraits<eventide::ipc::lsp::AddParams> {
    using Result = eventide::ipc::lsp::AddResult;
    constexpr inline static std::string_view method = "test/add";
};

template <>
struct eventide::ipc::protocol::NotificationTraits<eventide::ipc::lsp::NoteParams> {
    constexpr inline static std::string_view method = "test/note";
};

namespace eventide::ipc::lsp {

TEST_SUITE(language_jsonrpc_traits) {

TEST_CASE(traits_dispatch_order) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":2,"b":3}})",
        R"({"jsonrpc":"2.0","method":"test/note","params":{"text":"first"}})",
        R"({"jsonrpc":"2.0","method":"test/note","params":{"text":"second"}})",
    });
    auto* transport_ptr = transport.get();

    eventide::event_loop loop;
    ipc::JsonPeer peer(loop, std::move(transport));
    std::vector<std::string> order;
    bool second_saw_first = false;
    bool first_seen = false;

    peer.on_request([&](ipc::JsonPeer::RequestContext&,
                        const AddParams& params) -> ipc::RequestResult<AddParams> {
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
    auto response = serde::json::from_json<Response>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(response->id), 1);
    ASSERT_TRUE(response->result.has_value());
    EXPECT_EQ(response->result->sum, 5);
}

TEST_CASE(explicit_method) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":2,"method":"custom/add","params":{"a":7,"b":8}})",
        R"({"jsonrpc":"2.0","method":"custom/note","params":{"text":"hello"}})",
    });
    auto* transport_ptr = transport.get();

    eventide::event_loop loop;
    ipc::JsonPeer peer(loop, std::move(transport));
    std::string request_method;
    std::vector<std::string> notifications;

    peer.on_request("custom/add",
                    [&](ipc::JsonPeer::RequestContext& context,
                        const AddParams& params) -> ipc::RequestResult<AddParams> {
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
    auto response = serde::json::from_json<Response>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(std::get<std::int64_t>(response->id), 2);
    ASSERT_TRUE(response->result.has_value());
    EXPECT_EQ(response->result->sum, 15);
}

TEST_CASE(request_notify_apis) {
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{
            R"({"jsonrpc":"2.0","id":7,"method":"test/add","params":{"a":2,"b":3}})",
        },
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("method":"client/add/context")") != std::string_view::npos) {
                channel.push_incoming(R"({"jsonrpc":"2.0","id":1,"result":{"sum":9}})");
                return;
            }

            if(payload.find(R"("method":"client/add/server")") != std::string_view::npos) {
                channel.push_incoming(R"({"jsonrpc":"2.0","id":2,"result":{"sum":4}})");
                return;
            }

            if(payload.find(R"("id":7)") != std::string_view::npos &&
               payload.find(R"("result")") != std::string_view::npos) {
                channel.close();
            }
        });
    auto* transport_ptr = transport.get();

    eventide::event_loop loop;
    ipc::JsonPeer peer(loop, std::move(transport));
    std::string request_method;
    protocol::integer request_id = 0;

    peer.on_request([&](ipc::JsonPeer::RequestContext& context,
                        const AddParams& params) -> ipc::RequestResult<AddParams> {
        request_method = std::string(context.method);
        request_id = static_cast<protocol::integer>(std::get<std::int64_t>(context.id));

        co_await or_fail(
            context->send_notification("client/note/context", CustomNoteParams{.text = "context"}));
        co_await or_fail(
            peer.send_notification("client/note/server", CustomNoteParams{.text = "server"}));

        auto context_result =
            co_await context
                ->send_request<AddResult>("client/add/context",
                                          CustomAddParams{.a = params.a, .b = params.b})
                .or_fail();

        auto server_result = co_await peer
                                 .send_request<AddResult>("client/add/server",
                                                          CustomAddParams{.a = params.b, .b = 1})
                                 .or_fail();

        co_return AddResult{.sum = context_result.sum + server_result.sum};
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    EXPECT_EQ(request_method, "test/add");
    EXPECT_EQ(request_id, 7);

    const auto& outgoing = transport_ptr->outgoing();
    ASSERT_EQ(outgoing.size(), 5U);

    auto note_from_context = serde::json::from_json<Notification>(outgoing[0]);
    ASSERT_TRUE(note_from_context.has_value());
    EXPECT_EQ(note_from_context->jsonrpc, "2.0");
    EXPECT_EQ(note_from_context->method, "client/note/context");
    EXPECT_EQ(note_from_context->params.text, "context");

    auto note_from_server = serde::json::from_json<Notification>(outgoing[1]);
    ASSERT_TRUE(note_from_server.has_value());
    EXPECT_EQ(note_from_server->jsonrpc, "2.0");
    EXPECT_EQ(note_from_server->method, "client/note/server");
    EXPECT_EQ(note_from_server->params.text, "server");

    auto request_from_context = serde::json::from_json<Request>(outgoing[2]);
    ASSERT_TRUE(request_from_context.has_value());
    EXPECT_EQ(request_from_context->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(request_from_context->id), 1);
    EXPECT_EQ(request_from_context->method, "client/add/context");
    EXPECT_EQ(request_from_context->params.a, 2);
    EXPECT_EQ(request_from_context->params.b, 3);

    auto request_from_server = serde::json::from_json<Request>(outgoing[3]);
    ASSERT_TRUE(request_from_server.has_value());
    EXPECT_EQ(request_from_server->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(request_from_server->id), 2);
    EXPECT_EQ(request_from_server->method, "client/add/server");
    EXPECT_EQ(request_from_server->params.a, 3);
    EXPECT_EQ(request_from_server->params.b, 1);

    auto final_response = serde::json::from_json<Response>(outgoing[4]);
    ASSERT_TRUE(final_response.has_value());
    EXPECT_EQ(final_response->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(final_response->id), 7);
    ASSERT_TRUE(final_response->result.has_value());
    EXPECT_EQ(final_response->result->sum, 13);
}

};  // TEST_SUITE(language_jsonrpc_traits)

}  // namespace eventide::ipc::lsp
