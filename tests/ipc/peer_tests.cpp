#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "peer_test_types.h"
#include "eventide/zest/zest.h"
#include "eventide/async/async.h"
#include "eventide/serde/json/deserializer.h"

namespace eventide::ipc {

namespace {

task<> complete_request(JsonPeer& peer, PendingAddResult& out) {
    out.value =
        co_await peer.send_request<AddResult>("worker/build", CustomAddParams{.a = 2, .b = 3});
    if(!peer.close_output() && out.value.has_value()) {
        out.value = outcome_error(Error("failed to close peer output"));
    }
    co_return;
}

task<> write_notification_then_response(int fd, event_loop& loop) {
    co_await sleep(1, loop);

    const auto note = frame(R"({"jsonrpc":"2.0","method":"test/note","params":{"text":"first"}})");
    auto note_written = write_fd(fd, note.data(), note.size());
    if(note_written != static_cast<ssize_t>(note.size())) {
        close_fd(fd);
        co_return;
    }

    co_await sleep(1, loop);

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

}  // namespace eventide::ipc

namespace eventide::ipc {

TEST_SUITE(ipc_peer) {

TEST_CASE(traits_dispatch_order) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":2,"b":3}})",
        R"({"jsonrpc":"2.0","method":"test/note","params":{"text":"first"}})",
        R"({"jsonrpc":"2.0","method":"test/note","params":{"text":"second"}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
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
    auto response = serde::json::from_json<Response>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(response->id), 1);
    ASSERT_TRUE(response->result.has_value());
    EXPECT_EQ(response->result->sum, 5);
}

TEST_CASE(stream_note_response) {
    event_loop loop;

    int incoming_fds[2] = {-1, -1};
    int outgoing_fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(incoming_fds), 0);
    ASSERT_EQ(create_pipe(outgoing_fds), 0);

    auto input = pipe::open(incoming_fds[0], pipe::options{}, loop);
    ASSERT_TRUE(input.has_value());
    auto output = pipe::open(outgoing_fds[1], pipe::options{}, loop);
    ASSERT_TRUE(output.has_value());

    auto transport =
        std::make_unique<StreamTransport>(stream(std::move(*input)), stream(std::move(*output)));
    JsonPeer peer(loop, std::move(transport));

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
    event_loop loop;

    auto transport1 = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":11,"method":"worker/one","params":{"a":2,"b":5}})",
    });
    auto* transport1_ptr = transport1.get();

    auto transport2 = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":22,"method":"worker/two","params":{"a":7,"b":3}})",
    });
    auto* transport2_ptr = transport2.get();

    JsonPeer peer1(loop, std::move(transport1));
    JsonPeer peer2(loop, std::move(transport2));

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
    auto response1 = serde::json::from_json<Response>(transport1_ptr->outgoing().front());
    ASSERT_TRUE(response1.has_value());
    EXPECT_EQ(std::get<std::int64_t>(response1->id), 11);
    ASSERT_TRUE(response1->result.has_value());
    EXPECT_EQ(response1->result->sum, 7);

    ASSERT_EQ(transport2_ptr->outgoing().size(), 1U);
    auto response2 = serde::json::from_json<Response>(transport2_ptr->outgoing().front());
    ASSERT_TRUE(response2.has_value());
    EXPECT_EQ(std::get<std::int64_t>(response2->id), 22);
    ASSERT_TRUE(response2->result.has_value());
    EXPECT_EQ(response2->result->sum, 21);
}

TEST_CASE(explicit_method) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":2,"method":"custom/add","params":{"a":7,"b":8}})",
        R"({"jsonrpc":"2.0","method":"custom/note","params":{"text":"hello"}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
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
    JsonPeer peer(loop, std::move(transport));
    std::string request_method;
    protocol::integer request_id = 0;

    peer.on_request([&](RequestContext& context,
                        const AddParams& params) -> RequestResult<AddParams> {
        request_method = std::string(context.method);
        request_id = static_cast<protocol::integer>(std::get<std::int64_t>(context.id));

        co_await or_fail(
            context->send_notification("client/note/context", CustomNoteParams{.text = "context"}));
        co_await or_fail(
            peer.send_notification("client/note/peer", CustomNoteParams{.text = "peer"}));

        auto context_result =
            co_await context
                ->send_request<AddResult>("client/add/context",
                                          CustomAddParams{.a = params.a, .b = params.b})
                .or_fail();

        auto peer_result =
            co_await peer
                .send_request<AddResult>("client/add/peer", CustomAddParams{.a = params.b, .b = 1})
                .or_fail();

        co_return AddResult{.sum = context_result.sum + peer_result.sum};
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

    auto note_from_peer = serde::json::from_json<Notification>(outgoing[1]);
    ASSERT_TRUE(note_from_peer.has_value());
    EXPECT_EQ(note_from_peer->jsonrpc, "2.0");
    EXPECT_EQ(note_from_peer->method, "client/note/peer");
    EXPECT_EQ(note_from_peer->params.text, "peer");

    auto request_from_context = serde::json::from_json<Request>(outgoing[2]);
    ASSERT_TRUE(request_from_context.has_value());
    EXPECT_EQ(request_from_context->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(request_from_context->id), 1);
    EXPECT_EQ(request_from_context->method, "client/add/context");
    EXPECT_EQ(request_from_context->params.a, 2);
    EXPECT_EQ(request_from_context->params.b, 3);

    auto request_from_peer = serde::json::from_json<Request>(outgoing[3]);
    ASSERT_TRUE(request_from_peer.has_value());
    EXPECT_EQ(request_from_peer->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(request_from_peer->id), 2);
    EXPECT_EQ(request_from_peer->method, "client/add/peer");
    EXPECT_EQ(request_from_peer->params.a, 3);
    EXPECT_EQ(request_from_peer->params.b, 1);

    auto final_response = serde::json::from_json<Response>(outgoing[4]);
    ASSERT_TRUE(final_response.has_value());
    EXPECT_EQ(final_response->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(final_response->id), 7);
    ASSERT_TRUE(final_response->result.has_value());
    EXPECT_EQ(final_response->result->sum, 13);
}

TEST_CASE(request_notify_apis_failure) {
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{
            R"({"jsonrpc":"2.0","id":7,"method":"test/add","params":{"a":2,"b":3}})",
        },
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("method":"client/add/context")") != std::string_view::npos) {
                channel.push_incoming(R"({"jsonrpc":"2.0","id":1,"result":"oops"})");
                return;
            }

            if(payload.find(R"("id":7)") != std::string_view::npos &&
               payload.find(R"("error")") != std::string_view::npos) {
                channel.close();
            }
        });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    peer.on_request(
        [&](RequestContext& context, const AddParams& params) -> RequestResult<AddParams> {
            auto context_result =
                co_await context
                    ->send_request<AddResult>("client/add/context",
                                              CustomAddParams{.a = params.a, .b = params.b})
                    .or_fail();

            co_return AddResult{.sum = context_result.sum};
        });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    const auto& outgoing = transport_ptr->outgoing();
    ASSERT_EQ(outgoing.size(), 2U);

    auto nested_request = serde::json::from_json<Request>(outgoing[0]);
    ASSERT_TRUE(nested_request.has_value());
    EXPECT_EQ(nested_request->method, "client/add/context");

    auto final_response = serde::json::from_json<ErrorResponse>(outgoing[1]);
    ASSERT_TRUE(final_response.has_value());
    EXPECT_NE(outgoing[1].find(R"("error")"), std::string::npos);
    EXPECT_EQ(final_response->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(final_response->id), 7);
    EXPECT_EQ(final_response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::RequestFailed));
}

TEST_CASE(request_error_code) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":10,"method":"test/add","params":{"a":2,"b":3}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    peer.on_request([&](RequestContext&, const AddParams&) -> RequestResult<AddParams> {
        co_await fail(protocol::ErrorCode::InvalidParams, "forced invalid params");
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response = serde::json::from_json<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(response->id), 10);
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::InvalidParams));
    EXPECT_EQ(response->error.message, "forced invalid params");
}

TEST_CASE(request_error_data) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":12,"method":"test/add","params":{"a":2,"b":3}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    peer.on_request([&](RequestContext&, const AddParams&) -> RequestResult<AddParams> {
        protocol::Object data;
        data.insert_or_assign("detail", protocol::Value(std::string("invalid payload")));
        data.insert_or_assign("index", protocol::Value(std::int64_t{-3}));
        co_await fail(protocol::ErrorCode::InvalidParams,
                      "forced invalid params",
                      protocol::Value(std::move(data)));
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response = serde::json::from_json<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(response->id), 12);
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
    JsonPeer peer(loop, std::move(transport));
    Result<AddResult> request_result = outcome_error(Error("request did not complete"));

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
    JsonPeer peer(loop, std::move(transport));
    Result<AddResult> request_result = outcome_error(Error("request did not complete"));

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
    EXPECT_FALSE(request_result.error().message.empty());
}

TEST_CASE(bad_params_invalid) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":11,"method":"test/add","params":"invalid"})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    bool invoked = false;

    peer.on_request([&](RequestContext&, const AddParams&) -> RequestResult<AddParams> {
        invoked = true;
        co_return AddResult{.sum = 0};
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    EXPECT_FALSE(invoked);
    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response = serde::json::from_json<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(response->id), 11);
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::InvalidParams));
    EXPECT_FALSE(response->error.message.empty());
}

TEST_CASE(malformed_parse_null) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/add")",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response = serde::json::from_json<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(response->id), 0);
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::ParseError));
    EXPECT_FALSE(response->error.message.empty());
}

TEST_CASE(invalid_request_null) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response = serde::json::from_json<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(response->id), 0);
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::InvalidRequest));
    EXPECT_EQ(response->error.message, "message must contain method or id");
}

TEST_CASE(string_id_request) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":"abc","method":"test/add","params":{"a":2,"b":3}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    peer.on_request([](RequestContext&, const AddParams& p) -> RequestResult<AddParams> {
        co_return AddResult{.sum = p.a + p.b};
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto& out = transport_ptr->outgoing().front();
    EXPECT_TRUE(out.find(R"("id":"abc")") != std::string::npos);
    EXPECT_TRUE(out.find(R"("sum":5)") != std::string::npos);
}

TEST_CASE(cancel_inflight_request) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":21,"method":"test/add","params":{"a":2,"b":3}})",
        R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":21}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    bool finished = false;

    peer.on_request([&](RequestContext&, const AddParams& params) -> RequestResult<AddParams> {
        co_await sleep(10, loop);
        finished = true;
        co_return AddResult{.sum = params.a + params.b};
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    EXPECT_FALSE(finished);
    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response = serde::json::from_json<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(response->id), 21);
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT_EQ(response->error.message, "request cancelled");
}

TEST_CASE(cancel_running_handler) {
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{
            R"({"jsonrpc":"2.0","id":22,"method":"test/add","params":{"a":2,"b":3}})",
        },
        nullptr);
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    bool started = false;
    bool completed = false;
    event handler_started;

    peer.on_request([&](RequestContext&, const AddParams& params) -> RequestResult<AddParams> {
        started = true;
        handler_started.set();
        co_await sleep(20, loop);
        completed = true;
        co_return AddResult{.sum = params.a + params.b};
    });

    auto canceler = [&]() -> task<> {
        co_await handler_started.wait();
        co_await sleep(1, loop);
        transport_ptr->push_incoming(
            R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":22}})");
        co_await sleep(5, loop);
        transport_ptr->close();
    };

    auto cancel_task = canceler();
    loop.schedule(peer.run());
    loop.schedule(cancel_task);
    EXPECT_EQ(loop.run(), 0);

    EXPECT_TRUE(started);
    EXPECT_FALSE(completed);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response = serde::json::from_json<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(response->id), 22);
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT_EQ(response->error.message, "request cancelled");
}

TEST_CASE(context_token_propagates) {
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
    JsonPeer peer(loop, std::move(transport));
    bool started = false;

    peer.on_request(
        [&](RequestContext& context, const AddParams& params) -> RequestResult<AddParams> {
            started = true;

            auto nested_result =
                co_await context
                    ->send_request<AddResult>("client/add/context",
                                              CustomAddParams{.a = params.a, .b = params.b},
                                              {.token = context.cancellation})
                    .or_fail();

            co_return AddResult{.sum = nested_result.sum};
        });

    auto watchdog = [&]() -> task<> {
        co_await sleep(20, loop);
        transport_ptr->close();
    };

    auto watchdog_task = watchdog();
    loop.schedule(peer.run());
    loop.schedule(watchdog_task);
    EXPECT_EQ(loop.run(), 0);

    EXPECT_TRUE(started);

    const auto& outgoing = transport_ptr->outgoing();
    ASSERT_EQ(outgoing.size(), 3U);

    auto nested_request = serde::json::from_json<Request>(outgoing[0]);
    ASSERT_TRUE(nested_request.has_value());
    EXPECT_EQ(nested_request->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(nested_request->id), 1);
    EXPECT_EQ(nested_request->method, "client/add/context");
    EXPECT_EQ(nested_request->params.a, 4);
    EXPECT_EQ(nested_request->params.b, 5);

    auto nested_cancel = serde::json::from_json<CancelNotification>(outgoing[1]);
    ASSERT_TRUE(nested_cancel.has_value());
    EXPECT_EQ(nested_cancel->jsonrpc, "2.0");
    EXPECT_EQ(nested_cancel->method, "$/cancelRequest");
    EXPECT_EQ(std::get<std::int64_t>(nested_cancel->params.id), 1);

    auto final_error = serde::json::from_json<ErrorResponse>(outgoing[2]);
    ASSERT_TRUE(final_error.has_value());
    EXPECT_EQ(final_error->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(final_error->id), 31);
    EXPECT_EQ(final_error->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT_EQ(final_error->error.message, "request cancelled");
}

TEST_CASE(outbound_cancel_request) {
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{},
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("method":"$/cancelRequest")") != std::string_view::npos) {
                channel.close();
            }
        });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    cancellation_source source;
    Result<AddResult> request_result = outcome_error(Error("request did not complete"));

    auto requester = [&]() -> task<> {
        request_result = co_await peer.send_request<AddResult>("worker/build",
                                                               CustomAddParams{.a = 5, .b = 6},
                                                               {.token = source.token()});
        co_return;
    };

    auto canceler = [&]() -> task<> {
        co_await sleep(1, loop);
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

    auto request = serde::json::from_json<Request>(outgoing[0]);
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->jsonrpc, "2.0");
    EXPECT_EQ(std::get<std::int64_t>(request->id), 1);
    EXPECT_EQ(request->method, "worker/build");

    auto cancel = serde::json::from_json<CancelNotification>(outgoing[1]);
    ASSERT_TRUE(cancel.has_value());
    EXPECT_EQ(cancel->jsonrpc, "2.0");
    EXPECT_EQ(cancel->method, "$/cancelRequest");
    EXPECT_EQ(std::get<std::int64_t>(cancel->params.id), 1);
}

TEST_CASE(outbound_precancel) {
    auto transport = std::make_unique<ScriptedTransport>(std::vector<std::string>{}, nullptr);
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    cancellation_source source;
    source.cancel();
    Result<AddResult> request_result = outcome_error(Error("request did not complete"));

    auto requester = [&]() -> task<> {
        request_result = co_await peer.send_request<AddResult>("worker/build",
                                                               CustomAddParams{.a = 1, .b = 2},
                                                               {.token = source.token()});
        co_return;
    };

    auto closer = [&]() -> task<> {
        co_await sleep(1, loop);
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
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{},
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("method":"$/cancelRequest")") != std::string_view::npos) {
                channel.close();
            }
        });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    Result<AddResult> request_result = outcome_error(Error("request did not complete"));

    auto requester = [&]() -> task<> {
        request_result =
            co_await peer.send_request<AddResult>("worker/build",
                                                  CustomAddParams{.a = 8, .b = 9},
                                                  {.timeout = std::chrono::milliseconds{1}});
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

    auto request = serde::json::from_json<Request>(outgoing[0]);
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->method, "worker/build");

    auto cancel = serde::json::from_json<CancelNotification>(outgoing[1]);
    ASSERT_TRUE(cancel.has_value());
    EXPECT_EQ(cancel->method, "$/cancelRequest");
}

TEST_CASE(zero_timeout_cancel) {
    auto transport = std::make_unique<ScriptedTransport>(std::vector<std::string>{}, nullptr);
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    Result<AddResult> request_result = outcome_error(Error("request did not complete"));

    auto requester = [&]() -> task<> {
        request_result =
            co_await peer.send_request<AddResult>("worker/build",
                                                  CustomAddParams{.a = 1, .b = 1},
                                                  {.timeout = std::chrono::milliseconds{0}});
        co_return;
    };

    auto closer = [&]() -> task<> {
        co_await sleep(1, loop);
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

};  // TEST_SUITE(ipc_peer)

// ============================================================================
// Group: Peer — camelCase rename for request params and results
// ============================================================================

TEST_SUITE(ipc_peer_camel_case) {

// Incoming request with camelCase params → handler receives correct values
// Outgoing response contains camelCase result
TEST_CASE(request_params_result) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/rangeAdd","params":{"firstValue":10,"secondValue":20}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    peer.on_request(
        [&](RequestContext&, const RangeAddParams& params) -> RequestResult<RangeAddParams> {
            co_return RangeAddResult{.computed_sum = params.first_value + params.second_value};
        });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    const auto& raw = transport_ptr->outgoing().front();

    // Wire format must use camelCase
    EXPECT_TRUE(raw.find(R"("computedSum":30)") != std::string::npos);
    EXPECT_TRUE(raw.find("computed_sum") == std::string::npos);
}

// Incoming notification with camelCase params
TEST_CASE(notification_params) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","method":"test/statusNote","params":{"displayName":"alice","retryCount":3}})",
    });

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    std::string seen_name;
    std::int64_t seen_count = 0;

    peer.on_notification([&](const StatusNoteParams& params) {
        seen_name = params.display_name;
        seen_count = params.retry_count;
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    EXPECT_EQ(seen_name, "alice");
    EXPECT_EQ(seen_count, 3);
}

// Outgoing request serializes params in camelCase
TEST_CASE(outbound_request) {
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{},
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("method":"test/rangeAdd")") != std::string_view::npos) {
                channel.push_incoming(R"({"jsonrpc":"2.0","id":1,"result":{"computedSum":99}})");
                channel.close();
            }
        });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    Result<RangeAddResult> request_result = outcome_error(Error("not completed"));

    auto requester = [&]() -> task<> {
        request_result = co_await peer.send_request<RangeAddResult>(
            "test/rangeAdd",
            RangeAddParams{.first_value = 40, .second_value = 50});
        co_return;
    };

    auto request_task = requester();
    loop.schedule(peer.run());
    loop.schedule(request_task);
    EXPECT_EQ(loop.run(), 0);

    // Verify outgoing request used camelCase
    ASSERT_GE(transport_ptr->outgoing().size(), 1U);
    const auto& raw = transport_ptr->outgoing().front();
    EXPECT_TRUE(raw.find(R"("firstValue":40)") != std::string::npos);
    EXPECT_TRUE(raw.find(R"("secondValue":50)") != std::string::npos);
    EXPECT_TRUE(raw.find("first_value") == std::string::npos);

    // Verify response deserialized correctly
    ASSERT_TRUE(request_result.has_value());
    EXPECT_EQ(request_result->computed_sum, 99);
}

};  // TEST_SUITE(ipc_peer_camel_case)

}  // namespace eventide::ipc
