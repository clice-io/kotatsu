#include "peer_test_types.h"
#include "kota/zest/zest.h"

namespace kota::ipc {
namespace {

// ============================================================================
// Group 5: Peer — lifecycle & error handling
// ============================================================================

ZEST_SUITE(ipc_peer_lifecycle) {

// 5.1 Transport read failure → pending requests receive error
ZEST_CASE(read_fail_pending) {
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
    EXPECT(loop.run() == 0);

    ASSERT(!request_result);
    EXPECT(!request_result.error().message.empty());
}

// 5.4 close_output() on base Transport → returns unsupported error
ZEST_CASE(close_unsupported) {
    auto transport = std::make_unique<FakeTransport>(std::vector<std::string>{});

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    auto close_result = peer.close_output();
    ASSERT(!close_result);

    auto notify_result = peer.send_notification("test/note", NoteParams{.text = "hello"});
    ASSERT(notify_result);

    // Drain the write_loop coroutine scheduled by send_notification
    loop.run();
}

// 5.5 run() with null transport → immediate return
ZEST_CASE(null_transport) {
    event_loop loop;
    JsonPeer peer(loop, nullptr);

    loop.schedule(peer.run());
    EXPECT(loop.run() == 0);

    auto close_result = peer.close_output();
    ASSERT(!close_result);
    EXPECT(close_result.error().message == "transport is null");
}

// 5.6 Double run() → second returns immediately
ZEST_CASE(double_run) {
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
    EXPECT(loop.run() == 0);
}

// 5.7 close() stops the read loop
ZEST_CASE(close_stops_run) {
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
    EXPECT(loop.run() == 0);
}

// 5.8 close() fails pending outgoing requests
ZEST_CASE(close_fails_pending) {
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
    EXPECT(loop.run() == 0);

    ASSERT(!request_result);
    EXPECT(request_result.error().message == "peer closed");
}

// 5.9 close() cancels in-flight incoming requests (loop exits promptly, not after 10s)
ZEST_CASE(close_cancels_incoming) {
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
    EXPECT(loop.run() == 0);
}

// 5.10 close() on null transport is a no-op
ZEST_CASE(close_null_transport) {
    event_loop loop;
    JsonPeer peer(loop, nullptr);

    auto result = peer.close();
    ASSERT(result);
}

// 5.11 close() is idempotent
ZEST_CASE(close_idempotent) {
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
    EXPECT(loop.run() == 0);
}

// 5.12 close() awaits all in-flight handlers via request_group.join()
ZEST_CASE(close_awaits_multiple_inflight_handlers) {
    int handlers_started = 0;
    int handlers_completed = 0;

    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{
            R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":1,"b":2}})",
            R"({"jsonrpc":"2.0","id":2,"method":"test/add","params":{"a":3,"b":4}})",
            R"({"jsonrpc":"2.0","id":3,"method":"test/add","params":{"a":5,"b":6}})",
        },
        nullptr);

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    peer.on_request([&](RequestContext, AddParams params) -> RequestResult<AddParams> {
        handlers_started++;
        co_await sleep(std::chrono::seconds(10), loop);
        handlers_completed++;
        co_return AddResult{.sum = params.a + params.b};
    });

    auto closer = [&]() -> task<> {
        co_await sleep(1, loop);
        EXPECT(handlers_started == 3);
        peer.close();
    };

    loop.schedule(peer.run());
    loop.schedule(closer());
    EXPECT(loop.run() == 0);

    EXPECT(handlers_started == 3);
    EXPECT(handlers_completed == 0);
}

// 5.13 close() with mixed completed and in-flight handlers
ZEST_CASE(close_after_partial_completion) {
    int quick_done = 0;
    int slow_done = 0;

    auto transport = std::make_unique<ScriptedTransport>(
        std::vector<std::string>{
            R"({"jsonrpc":"2.0","id":1,"method":"test/add","params":{"a":1,"b":2}})",
            R"({"jsonrpc":"2.0","id":2,"method":"test/add","params":{"a":3,"b":4}})",
        },
        nullptr);

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));

    int request_count = 0;
    peer.on_request([&](RequestContext, AddParams params) -> RequestResult<AddParams> {
        int n = ++request_count;
        if(n == 1) {
            co_await sleep(1, loop);
            quick_done = 1;
        } else {
            co_await sleep(std::chrono::seconds(10), loop);
            slow_done = 1;
        }
        co_return AddResult{.sum = params.a + params.b};
    });

    auto closer = [&]() -> task<> {
        co_await sleep(5, loop);
        EXPECT(quick_done == 1);
        EXPECT(slow_done == 0);
        peer.close();
    };

    loop.schedule(peer.run());
    loop.schedule(closer());
    EXPECT(loop.run() == 0);

    EXPECT(quick_done == 1);
    EXPECT(slow_done == 0);
}

// 5.14 write failure closes transport, ending the read loop
ZEST_CASE(write_fail_closes_transport) {
    auto transport = std::make_unique<ScriptedTransport>(std::vector<std::string>{}, nullptr);
    auto* transport_ptr = transport.get();

    event_loop loop;
    JsonPeer peer(loop, std::move(transport));
    Result<AddResult> request_result = outcome_error(Error("not completed"));

    auto requester = [&]() -> task<> {
        co_await sleep(1, loop);
        transport_ptr->set_fail_writes(true);
        request_result =
            co_await peer.send_request<AddResult>("worker/build", CustomAddParams{.a = 1, .b = 2});
    };

    loop.schedule(peer.run());
    loop.schedule(requester());
    EXPECT(loop.run() == 0);

    ASSERT(!request_result);
    EXPECT(!request_result.error().message.empty());
}

};  // ZEST_SUITE(ipc_peer_lifecycle)

}  // namespace
}  // namespace kota::ipc
