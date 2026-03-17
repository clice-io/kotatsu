#include "peer_test_types.h"
#include "eventide/zest/zest.h"

namespace eventide::ipc {

// ============================================================================
// Group 4: Peer — cancellation edge cases
// ============================================================================

TEST_SUITE(ipc_peer_cancel) {

// 4.1 Cancel an already-completed request → no crash
TEST_CASE(cancel_completed) {
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{
            R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":1,"b":2}})",
        },
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("id":1)") != std::string_view::npos &&
               payload.find(R"("result")") != std::string_view::npos) {
                channel.push_incoming(
                    R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":1}})");
                channel.close();
            }
        });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    peer.on_request([&](RequestContext&, const AddParams& params) -> RequestResult<AddParams> {
        co_return AddResult{.sum = params.a + params.b};
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response = serde::json::from_json<Response>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    ASSERT_TRUE(response->result.has_value());
    EXPECT_EQ(response->result->sum, 3);
}

// 4.2 Cancel a nonexistent request id → silent
TEST_CASE(cancel_nonexistent) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":9999}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    EXPECT_TRUE(transport_ptr->outgoing().empty());
}

// 4.3 Double cancel → idempotent, no crash
TEST_CASE(double_cancel) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":1,"b":2}})",
        R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":1}})",
        R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":1}})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    peer.on_request([&](RequestContext&, const AddParams& params) -> RequestResult<AddParams> {
        co_await sleep(10, loop);
        co_return AddResult{.sum = params.a + params.b};
    });

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_EQ(transport_ptr->outgoing().size(), 1U);
    auto response = serde::json::from_json<ErrorResponse>(transport_ptr->outgoing().front());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->error.code,
              static_cast<protocol::integer>(protocol::ErrorCode::RequestCancelled));
}

// 4.5 $/cancelRequest with malformed params → silent
TEST_CASE(bad_cancel_params) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{
        R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"wrong":"field"}})",
        R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":"not an object"})",
    });
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    EXPECT_TRUE(transport_ptr->outgoing().empty());
}

};  // TEST_SUITE(ipc_peer_cancel)

}  // namespace eventide::ipc
