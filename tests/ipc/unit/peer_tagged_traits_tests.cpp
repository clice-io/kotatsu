#include "ipc/harness/peer_test_types.h"
#include "kota/zest/zest.h"

namespace kota::ipc {
namespace {

ZEST_SUITE(ipc_peer_tagged_traits) {

// on_request<Tag> dispatches by tag's method name
ZEST_CASE(tagged_request_handler) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/taggedAdd","params":{"a":10,"b":20}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    peer.on_request<TaggedAdd>(
        [](RequestContext&, const AddParams& params) -> RequestResult<AddParams> {
            co_return AddResult{.sum = params.a + params.b};
        });

    loop.schedule(peer.run());
    EXPECT(loop.run() == 0);

    ASSERT(transport_ptr->outgoing().size() == 1U);
    auto response = codec::json::from_string<Response>(transport_ptr->outgoing().front());
    ASSERT(response);
    EXPECT(std::get<std::int64_t>(response->id) == 1);
    ASSERT(response->result);
    EXPECT(response->result->sum == 30);
}

// on_notification<Tag> dispatches by tag's method name
ZEST_CASE(tagged_notification_handler) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","method":"test/taggedNote","params":{"text":"hello tag"}})",
    });

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    std::string received;

    peer.on_notification<TaggedNote>([&](const NoteParams& params) { received = params.text; });

    loop.schedule(peer.run());
    EXPECT(loop.run() == 0);

    EXPECT(received == "hello tag");
}

// send_request<Tag> and send_notification<Tag> use tag's method name
ZEST_CASE(tagged_send_apis) {
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{
            R"({"jsonrpc":"2.0","id":7,"method":"test/add","params":{"a":1,"b":2}})",
        },
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("method":"test/taggedAdd")") != std::string_view::npos) {
                channel.push_incoming(R"({"jsonrpc":"2.0","id":1,"result":{"sum":99}})");
                return;
            }

            if(payload.find(R"("id":7)") != std::string_view::npos &&
               payload.find(R"("result")") != std::string_view::npos) {
                channel.close();
            }
        });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    peer.on_request([&](RequestContext&, const AddParams&) -> RequestResult<AddParams> {
        co_await or_fail(peer.send_notification<TaggedNote>(NoteParams{.text = "tagged"}));

        auto result = co_await peer.send_request<TaggedAdd>(AddParams{.a = 42, .b = 58}).or_fail();
        co_return AddResult{.sum = result.sum};
    });

    loop.schedule(peer.run());
    EXPECT(loop.run() == 0);

    const auto& outgoing = transport_ptr->outgoing();
    ASSERT(outgoing.size() == 3U);

    auto note = codec::json::from_string<Notification>(outgoing[0]);
    ASSERT(note);
    EXPECT(note->method == "test/taggedNote");
    EXPECT(note->params.text == "tagged");

    auto req = codec::json::from_string<Request>(outgoing[1]);
    ASSERT(req);
    EXPECT(req->method == "test/taggedAdd");
    EXPECT(req->params.a == 42);
    EXPECT(req->params.b == 58);

    auto resp = codec::json::from_string<Response>(outgoing[2]);
    ASSERT(resp);
    EXPECT(std::get<std::int64_t>(resp->id) == 7);
    ASSERT(resp->result);
    EXPECT(resp->result->sum == 99);
}

};  // ZEST_SUITE(ipc_peer_tagged_traits)

}  // namespace
}  // namespace kota::ipc
