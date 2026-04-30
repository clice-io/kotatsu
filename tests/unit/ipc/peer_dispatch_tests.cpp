#include "peer_test_types.h"
#include "kota/zest/zest.h"

namespace kota::ipc {
namespace {

// ============================================================================
// Group 3: Peer — dispatch routing
// ============================================================================

TEST_SUITE(ipc_peer_dispatch) {

// 3.2 Unregistered method → MethodNotFound
TEST_CASE(unregistered_method) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"unknown/method","params":{}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response = codec::json::from_json<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(std::get<std::int64_t>(response->id), 1);
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::MethodNotFound));
}

// 3.3 Duplicate request id while first is still pending → InvalidRequest
TEST_CASE(duplicate_request_id) {
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
    EXPECT_EQ(loop.run(), 0);

    EXPECT_EQ(invocations, 1);
    ASSERT_EQ(transport_ptr->outgoing().size(), 2U);

    auto error = codec::json::from_json<ErrorResponse>(transport_ptr->outgoing()[0]);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(std::get<std::int64_t>(error->id), 1);
    EXPECT_EQ(error->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::InvalidRequest));

    auto success = codec::json::from_json<Response>(transport_ptr->outgoing()[1]);
    ASSERT_TRUE(success.has_value());
    EXPECT_EQ(std::get<std::int64_t>(success->id), 1);
    ASSERT_TRUE(success->result.has_value());
    EXPECT_EQ(success->result->sum, 3);
}

// 3.5 Unregistered notification → silent ignore
TEST_CASE(unregistered_notification) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","method":"unknown/note","params":{"text":"hello"}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    EXPECT_TRUE(transport_ptr->outgoing().empty());
}

// 3.7 Orphan response → silent ignore
TEST_CASE(orphan_response) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":999,"result":{"sum":42}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    EXPECT_TRUE(transport_ptr->outgoing().empty());
}

// 3.9 Mixed message sequence
TEST_CASE(mixed_sequence) {
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
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(order.size(), 2U);
    EXPECT_EQ(order[0], "request");
    EXPECT_EQ(order[1], "note:mid");

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response = codec::json::from_json<Response>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(std::get<std::int64_t>(response->id), 1);
    ASSERT_TRUE(response->result.has_value());
    EXPECT_EQ(response->result->sum, 30);
}

};  // TEST_SUITE(ipc_peer_dispatch)

}  // namespace
}  // namespace kota::ipc
