#include "peer_test_types.h"
#include "kota/zest/zest.h"

namespace kota::ipc {
namespace {

// ============================================================================
// Group 5: Peer — lifecycle & error handling
// ============================================================================

TEST_SUITE(ipc_peer_lifecycle) {

// 5.1 Transport read failure → pending requests receive error
TEST_CASE(read_fail_pending) {
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{},
        [](std::string_view payload, ScriptedTransport& channel) {
            if(payload.find(R"("method":"worker/build")") != std::string_view::npos) {
                channel.close();
            }
        });

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    Result<AddResult> request_result = outcome_error(Error("not completed"));

    auto requester = [&]() -> task<> {
        request_result =
            co_await peer.send_request<AddResult>("worker/build", CustomAddParams{.a = 1, .b = 2});
        co_return;
    };

    auto request_task = requester();
    loop.schedule(peer.run());
    loop.schedule(request_task);
    EXPECT_EQ(loop.run(), 0);

    ASSERT_FALSE(request_result.has_value());
    EXPECT_FALSE(request_result.error().message.empty());
}

// 5.4 close_output() on base Transport → returns unsupported error
TEST_CASE(close_unsupported) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{});

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    auto close_result = peer.close_output();
    ASSERT_FALSE(close_result.has_value());

    auto notify_result = peer.send_notification("test/note", NoteParams{.text = "hello"});
    ASSERT_TRUE(notify_result.has_value());

    // Drain the write_loop coroutine scheduled by send_notification
    loop.run();
}

// 5.5 run() with null transport → immediate return
TEST_CASE(null_transport) {
    event_loop loop;
    JsonPeer peer(loop, nullptr);

    loop.schedule(peer.run());
    EXPECT_EQ(loop.run(), 0);

    auto close_result = peer.close_output();
    ASSERT_FALSE(close_result.has_value());
    EXPECT_EQ(close_result.error().message, "transport is null");
}

// 5.6 Double run() → second returns immediately
TEST_CASE(double_run) {
    auto transport = std::make_unique<ScriptedTransport>(std::vector<std::string>{}, nullptr);
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    auto closer = [&]() -> task<> {
        co_await sleep(1, loop);
        transport_ptr->close();
    };

    auto close_task = closer();
    loop.schedule(peer.run());
    loop.schedule(peer.run());  // second run() returns immediately
    loop.schedule(close_task);
    EXPECT_EQ(loop.run(), 0);
}

// 5.7 close() stops the read loop
TEST_CASE(close_stops_run) {
    auto transport = std::make_unique<ScriptedTransport>(std::vector<std::string>{}, nullptr);

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    auto closer = [&]() -> task<> {
        co_await sleep(1, loop);
        peer.close();
    };

    auto close_task = closer();
    loop.schedule(peer.run());
    loop.schedule(close_task);
    EXPECT_EQ(loop.run(), 0);
}

// 5.8 close() fails pending outgoing requests
TEST_CASE(close_fails_pending) {
    auto transport = std::make_unique<ScriptedTransport>(std::vector<std::string>{}, nullptr);

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    Result<AddResult> request_result = outcome_error(Error("not completed"));

    auto requester = [&]() -> task<> {
        request_result =
            co_await peer.send_request<AddResult>("worker/build", CustomAddParams{.a = 1, .b = 2});
    };

    auto closer = [&]() -> task<> {
        co_await sleep(1, loop);
        peer.close();
    };

    loop.schedule(peer.run());
    loop.schedule(requester());
    loop.schedule(closer());
    EXPECT_EQ(loop.run(), 0);

    ASSERT_FALSE(request_result.has_value());
    EXPECT_EQ(request_result.error().message, "peer closed");
}

// 5.9 close() cancels in-flight incoming requests (loop exits promptly, not after 10s)
TEST_CASE(close_cancels_incoming) {
    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{
            R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":1,"b":2}})",
        },
        nullptr);

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    peer.on_request([&](RequestContext, AddParams params) -> RequestResult<AddParams> {
        // This long sleep should be interrupted by close()
        co_await sleep(std::chrono::seconds(10), loop);
        co_return AddResult{.sum = params.a + params.b};
    });

    auto closer = [&]() -> task<> {
        co_await sleep(1, loop);
        peer.close();
    };

    loop.schedule(peer.run());
    loop.schedule(closer());
    // If close() didn't cancel the handler, this would take ~10 seconds
    EXPECT_EQ(loop.run(), 0);
}

// 5.10 close() on null transport is a no-op
TEST_CASE(close_null_transport) {
    event_loop loop;
    JsonPeer peer(loop, nullptr);

    auto result = peer.close();
    ASSERT_TRUE(result.has_value());
}

// 5.11 close() is idempotent
TEST_CASE(close_idempotent) {
    auto transport = std::make_unique<ScriptedTransport>(std::vector<std::string>{}, nullptr);

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    auto closer = [&]() -> task<> {
        co_await sleep(1, loop);
        peer.close();
        peer.close();  // second close should be safe
    };

    loop.schedule(peer.run());
    loop.schedule(closer());
    EXPECT_EQ(loop.run(), 0);
}

};  // TEST_SUITE(ipc_peer_lifecycle)

}  // namespace
}  // namespace kota::ipc
