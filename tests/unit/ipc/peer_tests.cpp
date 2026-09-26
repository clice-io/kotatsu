#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "peer_test_types.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"
#include "kota/codec/json/json.h"

namespace kota::ipc {

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

ZEST_SUITE(ipc_peer){

    ZEST_CASE(traits_dispatch_order){
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
EXPECT(loop.run() == 0);

ASSERT(order.size() == 3U);
EXPECT(order[0] == "request");
EXPECT(order[1] == "note:first");
EXPECT(order[2] == "note:second");
EXPECT(second_saw_first);

ASSERT(transport_ptr->outgoing().size() == 1U);
auto response = codec::json::from_string<Response>(transport_ptr->outgoing().front());
ASSERT(response.has_value());
EXPECT(response->jsonrpc == "2.0");
EXPECT(std::get<std::int64_t>(response->id) == 1);
ASSERT(response->result.has_value());
EXPECT(response->result->sum == 5);

}  // namespace

ZEST_CASE(stream_note_response) {
    event_loop loop;

    int incoming_fds[2] = {-1, -1};
    int outgoing_fds[2] = {-1, -1};
    ASSERT(create_pipe(incoming_fds) == 0);
    ASSERT(create_pipe(outgoing_fds) == 0);

    auto input = pipe::open(incoming_fds[0], pipe::options{}, loop);
    ASSERT(input.has_value());
    auto output = pipe::open(outgoing_fds[1], pipe::options{}, loop);
    ASSERT(output.has_value());

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

    EXPECT(loop.run() == 0);

    ASSERT(request_result.value.has_value());
    EXPECT(request_result.value->sum == 9);
    ASSERT(seen_notes.size() == 1U);
    EXPECT(seen_notes.front() == "first");

    ASSERT(close_fd(outgoing_fds[0]) == 0);
}

ZEST_CASE(peers_share_loop) {
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

    EXPECT(loop.run() == 0);

    ASSERT(transport1_ptr->outgoing().size() == 1U);
    auto response1 = codec::json::from_string<Response>(transport1_ptr->outgoing().front());
    ASSERT(response1.has_value());
    EXPECT(std::get<std::int64_t>(response1->id) == 11);
    ASSERT(response1->result.has_value());
    EXPECT(response1->result->sum == 7);

    ASSERT(transport2_ptr->outgoing().size() == 1U);
    auto response2 = codec::json::from_string<Response>(transport2_ptr->outgoing().front());
    ASSERT(response2.has_value());
    EXPECT(std::get<std::int64_t>(response2->id) == 22);
    ASSERT(response2->result.has_value());
    EXPECT(response2->result->sum == 21);
}

ZEST_CASE(explicit_method) {
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
    EXPECT(loop.run() == 0);

    EXPECT(request_method == "custom/add");
    ASSERT(notifications.size() == 1U);
    EXPECT(notifications.front() == "hello");

    ASSERT(transport_ptr->outgoing().size() == 1U);
    auto response = codec::json::from_string<Response>(transport_ptr->outgoing().front());
    ASSERT(response.has_value());
    EXPECT(std::get<std::int64_t>(response->id) == 2);
    ASSERT(response->result.has_value());
    EXPECT(response->result->sum == 15);
}

ZEST_CASE(request_notify_apis) {
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
    EXPECT(loop.run() == 0);

    EXPECT(request_method == "test/add");
    EXPECT(request_id == 7);

    const auto& outgoing = transport_ptr->outgoing();
    ASSERT(outgoing.size() == 5U);

    auto note_from_context = codec::json::from_string<Notification>(outgoing[0]);
    ASSERT(note_from_context.has_value());
    EXPECT(note_from_context->jsonrpc == "2.0");
    EXPECT(note_from_context->method == "client/note/context");
    EXPECT(note_from_context->params.text == "context");

    auto note_from_peer = codec::json::from_string<Notification>(outgoing[1]);
    ASSERT(note_from_peer.has_value());
    EXPECT(note_from_peer->jsonrpc == "2.0");
    EXPECT(note_from_peer->method == "client/note/peer");
    EXPECT(note_from_peer->params.text == "peer");

    auto request_from_context = codec::json::from_string<Request>(outgoing[2]);
    ASSERT(request_from_context.has_value());
    EXPECT(request_from_context->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(request_from_context->id) == 1);
    EXPECT(request_from_context->method == "client/add/context");
    EXPECT(request_from_context->params.a == 2);
    EXPECT(request_from_context->params.b == 3);

    auto request_from_peer = codec::json::from_string<Request>(outgoing[3]);
    ASSERT(request_from_peer.has_value());
    EXPECT(request_from_peer->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(request_from_peer->id) == 2);
    EXPECT(request_from_peer->method == "client/add/peer");
    EXPECT(request_from_peer->params.a == 3);
    EXPECT(request_from_peer->params.b == 1);

    auto final_response = codec::json::from_string<Response>(outgoing[4]);
    ASSERT(final_response.has_value());
    EXPECT(final_response->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(final_response->id) == 7);
    ASSERT(final_response->result.has_value());
    EXPECT(final_response->result->sum == 13);
}

ZEST_CASE(request_notify_apis_failure) {
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
    EXPECT(loop.run() == 0);

    const auto& outgoing = transport_ptr->outgoing();
    ASSERT(outgoing.size() == 2U);

    auto nested_request = codec::json::from_string<Request>(outgoing[0]);
    ASSERT(nested_request.has_value());
    EXPECT(nested_request->method == "client/add/context");

    auto final_response = codec::json::from_string<ErrorResponse>(outgoing[1]);
    ASSERT(final_response.has_value());
    EXPECT(outgoing[1].find(R"("error")") != std::string::npos);
    EXPECT(final_response->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(final_response->id) == 7);
    EXPECT(final_response->error.code ==
           static_cast<protocol::integer>(protocol::ErrorCode::RequestFailed));
}

ZEST_CASE(request_error_code) {
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
    EXPECT(loop.run() == 0);

    ASSERT(transport_ptr->outgoing().size() == 1U);
    auto response = codec::json::from_string<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT(response.has_value());
    EXPECT(response->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(response->id) == 10);
    EXPECT(response->error.code ==
           static_cast<protocol::integer>(protocol::ErrorCode::InvalidParams));
    EXPECT(response->error.message == "forced invalid params");
}

ZEST_CASE(request_error_data) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":12,"method":"test/add","params":{"a":2,"b":3}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    peer.on_request([&](RequestContext&, const AddParams&) -> RequestResult<AddParams> {
        co_await fail(protocol::ErrorCode::InvalidParams,
                      "forced invalid params",
                      codec::dyn::Value{
                          {"detail", "invalid payload"},
                          {"index",  -3               }
        });
    });

    loop.schedule(peer.run());
    EXPECT(loop.run() == 0);

    ASSERT(transport_ptr->outgoing().size() == 1U);
    auto response = codec::json::from_string<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT(response.has_value());
    EXPECT(response->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(response->id) == 12);
    EXPECT(response->error.code ==
           static_cast<protocol::integer>(protocol::ErrorCode::InvalidParams));
    EXPECT(response->error.message == "forced invalid params");
    ASSERT(response->error.data.has_value());
    EXPECT(*response->error.data == codec::dyn::Value{
                                        {"detail", "invalid payload"},
                                        {"index",  -3               }
    });
}

ZEST_CASE(outbound_error_data) {
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
    EXPECT(loop.run() == 0);

    ASSERT(!request_result.has_value());
    EXPECT(request_result.error().code == -32001);
    EXPECT(request_result.error().message == "remote failed");
    ASSERT(request_result.error().data.has_value());
    EXPECT(*request_result.error().data == codec::dyn::Value{
                                               {"detail",  "bad state"},
                                               {"attempt", -1         }
    });
}

ZEST_CASE(bad_response_silent) {
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{},
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("method":"worker/build")") == std::string_view::npos) {
                return;
            }

            channel.push_incoming(R"({"jsonrpc":"2.0","id":1,"\uD800":0})");
            channel.close();
        });
    [[maybe_unused]] auto* transport_ptr = transport.get();

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
    EXPECT(loop.run() == 0);

    ASSERT(!request_result.has_value());
    EXPECT(!request_result.error().message.empty());
}

ZEST_CASE(bad_params_invalid) {
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
    EXPECT(loop.run() == 0);

    EXPECT(!invoked);
    ASSERT(transport_ptr->outgoing().size() == 1U);
    auto response = codec::json::from_string<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT(response.has_value());
    EXPECT(response->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(response->id) == 11);
    EXPECT(response->error.code ==
           static_cast<protocol::integer>(protocol::ErrorCode::InvalidParams));
    EXPECT(!response->error.message.empty());
}

ZEST_CASE(malformed_parse_null) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/add")",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    loop.schedule(peer.run());
    EXPECT(loop.run() == 0);

    ASSERT(transport_ptr->outgoing().size() == 1U);
    auto response = codec::json::from_string<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT(response.has_value());
    EXPECT(response->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(response->id) == 0);
    EXPECT(response->error.code == static_cast<protocol::integer>(protocol::ErrorCode::ParseError));
    EXPECT(!response->error.message.empty());
}

ZEST_CASE(invalid_request_null) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    loop.schedule(peer.run());
    EXPECT(loop.run() == 0);

    ASSERT(transport_ptr->outgoing().size() == 1U);
    auto response = codec::json::from_string<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT(response.has_value());
    EXPECT(response->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(response->id) == 0);
    EXPECT(response->error.code ==
           static_cast<protocol::integer>(protocol::ErrorCode::InvalidRequest));
    EXPECT(response->error.message == "message must contain method or id");
}

ZEST_CASE(string_id_request) {
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
    EXPECT(loop.run() == 0);

    ASSERT(transport_ptr->outgoing().size() == 1U);
    auto& out = transport_ptr->outgoing().front();
    EXPECT(zest::contains(out, R"("id":"abc")"));
    EXPECT(zest::contains(out, R"("sum":5)"));
}

ZEST_CASE(cancel_inflight_request) {
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
    EXPECT(loop.run() == 0);

    EXPECT(!finished);
    ASSERT(transport_ptr->outgoing().size() == 1U);
    auto response = codec::json::from_string<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT(response.has_value());
    EXPECT(response->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(response->id) == 21);
    EXPECT(response->error.code ==
           static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT(response->error.message == "request cancelled");
}

ZEST_CASE(cancel_running_handler) {
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
    EXPECT(loop.run() == 0);

    EXPECT(started);
    EXPECT(!completed);

    ASSERT(transport_ptr->outgoing().size() == 1U);
    auto response = codec::json::from_string<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT(response.has_value());
    EXPECT(response->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(response->id) == 22);
    EXPECT(response->error.code ==
           static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT(response->error.message == "request cancelled");
}

ZEST_CASE(context_token_propagates) {
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
    EXPECT(loop.run() == 0);

    EXPECT(started);

    const auto& outgoing = transport_ptr->outgoing();
    ASSERT(outgoing.size() == 3U);

    auto nested_request = codec::json::from_string<Request>(outgoing[0]);
    ASSERT(nested_request.has_value());
    EXPECT(nested_request->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(nested_request->id) == 1);
    EXPECT(nested_request->method == "client/add/context");
    EXPECT(nested_request->params.a == 4);
    EXPECT(nested_request->params.b == 5);

    auto nested_cancel = codec::json::from_string<CancelNotification>(outgoing[1]);
    ASSERT(nested_cancel.has_value());
    EXPECT(nested_cancel->jsonrpc == "2.0");
    EXPECT(nested_cancel->method == "$/cancelRequest");
    EXPECT(std::get<std::int64_t>(nested_cancel->params.id) == 1);

    auto final_error = codec::json::from_string<ErrorResponse>(outgoing[2]);
    ASSERT(final_error.has_value());
    EXPECT(final_error->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(final_error->id) == 31);
    EXPECT(final_error->error.code ==
           static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT(final_error->error.message == "request cancelled");
}

ZEST_CASE(outbound_cancel_request) {
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
    EXPECT(loop.run() == 0);

    ASSERT(!request_result.has_value());
    EXPECT(request_result.error().code ==
           static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT(request_result.error().message == "request cancelled");

    const auto& outgoing = transport_ptr->outgoing();
    ASSERT(outgoing.size() == 2U);

    auto request = codec::json::from_string<Request>(outgoing[0]);
    ASSERT(request.has_value());
    EXPECT(request->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(request->id) == 1);
    EXPECT(request->method == "worker/build");

    auto cancel = codec::json::from_string<CancelNotification>(outgoing[1]);
    ASSERT(cancel.has_value());
    EXPECT(cancel->jsonrpc == "2.0");
    EXPECT(cancel->method == "$/cancelRequest");
    EXPECT(std::get<std::int64_t>(cancel->params.id) == 1);
}

ZEST_CASE(outbound_precancel) {
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
    EXPECT(loop.run() == 0);

    ASSERT(!request_result.has_value());
    EXPECT(request_result.error().code ==
           static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT(request_result.error().message == "request cancelled");
    EXPECT(transport_ptr->outgoing().empty());
}

ZEST_CASE(outbound_timeout_cancel) {
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
    EXPECT(loop.run() == 0);

    ASSERT(!request_result.has_value());
    EXPECT(request_result.error().code ==
           static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT(request_result.error().message == "request timed out");

    const auto& outgoing = transport_ptr->outgoing();
    ASSERT(outgoing.size() == 2U);

    auto request = codec::json::from_string<Request>(outgoing[0]);
    ASSERT(request.has_value());
    EXPECT(request->method == "worker/build");

    auto cancel = codec::json::from_string<CancelNotification>(outgoing[1]);
    ASSERT(cancel.has_value());
    EXPECT(cancel->method == "$/cancelRequest");
}

ZEST_CASE(zero_timeout_cancel) {
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
    EXPECT(loop.run() == 0);

    ASSERT(!request_result.has_value());
    EXPECT(request_result.error().code ==
           static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
    EXPECT(request_result.error().message == "request timed out");
    EXPECT(transport_ptr->outgoing().empty());
}

// Verify that completing a request before its timeout doesn't leak the timer coroutine frame.
// Before the fix, cancel_after_timeout held a shared_ptr<cancellation_source> in its coroutine
// frame that was only released when the timer fired. If the request completed early and the peer
// was closed, the timer task's coroutine frame (and its captured shared_ptrs) leaked.
// ASan will catch this as a leak if the fix regresses.
ZEST_CASE(timeout_timer_cleanup_on_early_completion) {
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{},
        [](std::string_view payload, ScriptedTransport& channel) {
            // When we see the outgoing request, immediately push a success response and close.
            if(payload.find(R"("method":"worker/build")") != std::string_view::npos) {
                channel.push_incoming(R"({"jsonrpc":"2.0","id":1,"result":{"sum":5}})");
                channel.close();
            }
        });

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    Result<AddResult> request_result = outcome_error(Error("request did not complete"));

    auto requester = [&]() -> task<> {
        request_result =
            co_await peer.send_request<AddResult>("worker/build",
                                                  CustomAddParams{.a = 2, .b = 3},
                                                  {.timeout = std::chrono::milliseconds{5000}});
    };

    auto request_task = requester();
    loop.schedule(peer.run());
    loop.schedule(request_task);
    EXPECT(loop.run() == 0);

    ASSERT(request_result.has_value());
    EXPECT(request_result->sum == 5);
}

// Handler returning task<codec::RawValue, Error> instead of RequestResult<Params>
ZEST_CASE(raw_value_return) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":10,"b":20}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    peer.on_request([&](RequestContext&, const AddParams& params) -> task<codec::RawValue, Error> {
        // Return pre-serialized JSON directly
        auto raw = std::format(R"({{"sum":{}}})", params.a + params.b);
        co_return codec::RawValue{.data = std::move(raw)};
    });

    loop.schedule(peer.run());
    EXPECT(loop.run() == 0);

    ASSERT(transport_ptr->outgoing().size() == 1U);
    auto response = codec::json::from_string<Response>(transport_ptr->outgoing().front());
    ASSERT(response.has_value());
    EXPECT(response->jsonrpc == "2.0");
    EXPECT(std::get<std::int64_t>(response->id) == 1);
    ASSERT(response->result.has_value());
    EXPECT(response->result->sum == 30);
}

};  // namespace kota::ipc

// ============================================================================
// Group: Peer — camelCase rename for request params and results
// ============================================================================

ZEST_SUITE(ipc_peer_camel_case){

    // Incoming request with camelCase params → handler receives correct values
    // Outgoing response contains camelCase result
    ZEST_CASE(request_params_result){
        auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
            R"({"jsonrpc":"2.0","id":1,"method":"test/rangeAdd","params":{"firstValue":10,"secondValue":20}})",
        });
auto* transport_ptr = transport.get();

event_loop loop;
JsonPeer peer(loop, std::move(transport));

peer.on_request([&](RequestContext&,
                    const RangeAddParams& params) -> RequestResult<RangeAddParams> {
    co_return RangeAddResult{.computed_sum = params.first_value + params.second_value};
});

loop.schedule(peer.run());
EXPECT(loop.run() == 0);

ASSERT(transport_ptr->outgoing().size() == 1U);
const auto& raw = transport_ptr->outgoing().front();

// Serialized form must use camelCase
EXPECT(zest::contains(raw, R"("computedSum":30)"));
EXPECT(!zest::contains(raw, "computed_sum"));
}

// Incoming notification with camelCase params
ZEST_CASE(notification_params) {
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
    EXPECT(loop.run() == 0);

    EXPECT(seen_name == "alice");
    EXPECT(seen_count == 3);
}

// Outgoing request serializes params in camelCase
ZEST_CASE(outbound_request) {
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
    EXPECT(loop.run() == 0);

    // Verify outgoing request used camelCase
    ASSERT(transport_ptr->outgoing().size() >= 1U);
    const auto& raw = transport_ptr->outgoing().front();
    EXPECT(zest::contains(raw, R"("firstValue":40)"));
    EXPECT(zest::contains(raw, R"("secondValue":50)"));
    EXPECT(!zest::contains(raw, "first_value"));

    // Verify response deserialized correctly
    ASSERT(request_result.has_value());
    EXPECT(request_result->computed_sum == 99);
}
}
;  // ZEST_SUITE(ipc_peer_camel_case)

}  // namespace
}  // namespace kota::ipc
