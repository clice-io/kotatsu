#include "ipc/harness/peer_test_types.h"
#include "kota/zest/zest.h"

namespace kota::ipc {
namespace {

// ============================================================================
// Group 3: Peer — dispatch routing
// ============================================================================

ZEST_SUITE(ipc_peer_dispatch) {

// 3.2 Unregistered method → MethodNotFound
ZEST_CASE(unregistered_method) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"unknown/method","params":{}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    loop.schedule(peer.run());
    EXPECT(loop.run() == 0);

    ASSERT(transport_ptr->outgoing().size() == 1U);
    auto response = codec::json::from_string<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT(response);
    EXPECT(std::get<std::int64_t>(response->id) == 1);
    EXPECT(response->error.code ==
           static_cast<protocol::integer>(protocol::ErrorCode::MethodNotFound));
}

// 3.3 Duplicate request id while first is still pending → InvalidRequest
ZEST_CASE(duplicate_request_id) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":1,"b":2}})",
        R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":3,"b":4}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    int invocations = 0;

    peer.on_request([&](RequestContext&, const AddParams& params) -> RequestResult<AddParams> {
        ++invocations;
        co_await sleep(1, loop);
        co_return AddResult{.sum = params.a + params.b};
    });

    loop.schedule(peer.run());
    EXPECT(loop.run() == 0);

    EXPECT(invocations == 1);
    ASSERT(transport_ptr->outgoing().size() == 2U);

    auto error = codec::json::from_string<ErrorResponse>(transport_ptr->outgoing()[0]);
    ASSERT(error);
    EXPECT(std::get<std::int64_t>(error->id) == 1);
    EXPECT(error->error.code ==
           static_cast<protocol::integer>(protocol::ErrorCode::InvalidRequest));

    auto success = codec::json::from_string<Response>(transport_ptr->outgoing()[1]);
    ASSERT(success);
    EXPECT(std::get<std::int64_t>(success->id) == 1);
    ASSERT(success->result);
    EXPECT(success->result->sum == 3);
}

// 3.5 Unregistered notification → silent ignore
ZEST_CASE(unregistered_notification) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","method":"unknown/note","params":{"text":"hello"}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    loop.schedule(peer.run());
    EXPECT(loop.run() == 0);

    EXPECT(transport_ptr->outgoing().empty());
}

// 3.7 Orphan response → silent ignore
ZEST_CASE(orphan_response) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":999,"result":{"sum":42}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    loop.schedule(peer.run());
    EXPECT(loop.run() == 0);

    EXPECT(transport_ptr->outgoing().empty());
}

// 3.9 Mixed message sequence
ZEST_CASE(mixed_sequence) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":10,"b":20}})",
        R"({"jsonrpc":"2.0","method":"test/note","params":{"text":"mid"}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    std::vector<std::string> order;

    peer.on_request([&](RequestContext&, const AddParams& params) -> RequestResult<AddParams> {
        order.emplace_back("request");
        co_return AddResult{.sum = params.a + params.b};
    });

    peer.on_notification(
        [&](const NoteParams& params) { order.emplace_back("note:" + params.text); });

    loop.schedule(peer.run());
    EXPECT(loop.run() == 0);

    ASSERT(order.size() == 2U);
    EXPECT(order[0] == "request");
    EXPECT(order[1] == "note:mid");

    ASSERT(transport_ptr->outgoing().size() == 1U);
    auto response = codec::json::from_string<Response>(transport_ptr->outgoing().front());
    ASSERT(response);
    EXPECT(std::get<std::int64_t>(response->id) == 1);
    ASSERT(response->result);
    EXPECT(response->result->sum == 30);
}

};  // ZEST_SUITE(ipc_peer_dispatch)

}  // namespace
}  // namespace kota::ipc
