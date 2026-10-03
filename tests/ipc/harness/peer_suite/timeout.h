#pragma once

// Timeouts on send_request, alone and with a cancellation: a hard deadline
// from the send, which also bounds the wait for the answer after a cancel.
// The remote never answers the requests that time out; a timeout is the
// subject here, so these cases wait for real time, as little as they can.

#include <chrono>

#include "ipc/harness/peer_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::test {

template <CodecAdapter A>
void peer_timeout(const PeerKit<A>& kit) {
    using Fixture = PeerFixture<A>;
    using ipc::protocol::CancelRequestParams;
    using ipc::protocol::ErrorCode;
    using namespace std::chrono_literals;

    kit.add("send_request_times_out_and_sends_cancel_request", [](Fixture& f) {
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.send_request(AddParams{}, {.timeout = 10ms}).or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::RequestCancelled);
        EXPECT(asked.error().message == "request timed out");
        const auto& written = f.written();
        ASSERT(written.size() == 2U);
        EXPECT(written[0].id == RequestID(1));
        EXPECT(written[1].method == "$/cancelRequest");
        auto cancelled = decoded<CancelRequestParams, A>(written[1].body);
        ASSERT(cancelled.has_value());
        EXPECT(cancelled->id == RequestID(1));
    });

    kit.add("zero_timeout_fails_without_writing", [](Fixture& f) {
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            f.remote.end_input();
            co_return co_await f.peer.send_request(AddParams{}, {.timeout = 0ms}).or_fail();
        };

        auto [ran, asked] = f.run(f.peer.run(), ask());
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::RequestCancelled);
        EXPECT(asked.error().message == "request timed out");
        EXPECT(f.written().empty());
    });

    // The timer stops with the request: a leaked one shows under ASan.
    kit.add("answer_before_the_timeout_wins", [](Fixture& f) {
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.send_request(AddParams{.a = 2, .b = 3}, {.timeout = 1min})
                .or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(response<A>(1, AddResult{.sum = 5}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_value());
        EXPECT(asked->sum == 5);
    });

    kit.add("token_before_the_timeout_returns_the_remote_answer", [](Fixture& f) {
        cancellation_source source;
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer
                .send_request(AddParams{}, {.token = source.token(), .timeout = 1min})
                .or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            source.cancel();
            co_await f.next();
            f.remote.send(A::error_response(
                1,
                ipc::Error(ErrorCode::RequestCancelled, "cancelled by the remote")));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::RequestCancelled);
        EXPECT(asked.error().message == "cancelled by the remote");
    });

    // The token sent $/cancelRequest, which the remote ignores: the request
    // fails at the deadline all the same, and the remote is not told again.
    // The token fires in the turn the request is sent, so that however slow
    // the run, the cancel comes before the deadline.
    kit.add("token_then_a_silent_remote_times_out", [](Fixture& f) {
        cancellation_source source;
        event ended;
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            auto asked = co_await f.peer.send_request(AddParams{},
                                                      {.token = source.token(), .timeout = 10ms});
            ended.set();
            co_return co_await or_fail(std::move(asked));
        };
        auto remote = [&]() -> task<> {
            source.cancel();
            co_await f.next();
            co_await f.next();
            co_await ended.wait();
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::RequestCancelled);
        EXPECT(asked.error().message == "request timed out");
        const auto& written = f.written();
        ASSERT(written.size() == 2U);
        EXPECT(written[1].method == "$/cancelRequest");
    });

    // The caller is cancelled and the remote ignores $/cancelRequest: the
    // caller ends cancelled at the deadline. It is cancelled in the turn the
    // request is sent, as above.
    kit.add("cancelled_caller_and_a_silent_remote_end_at_the_timeout", [](Fixture& f) {
        cancellation_source source;
        event ended;
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.send_request(AddParams{}, {.timeout = 10ms}).or_fail();
        };
        auto caller = [&]() -> task<bool> {
            auto asked = co_await with_token(ask(), source.token());
            ended.set();
            co_return asked.is_cancelled();
        };
        auto remote = [&]() -> task<> {
            source.cancel();
            co_await f.next();
            co_await f.next();
            co_await ended.wait();
            f.remote.end_input();
        };

        auto [ran, cancelled, scripted] = f.run(f.peer.run(), caller(), remote());
        EXPECT(ran.has_value());
        ASSERT(cancelled.has_value());
        EXPECT(*cancelled);
        const auto& written = f.written();
        ASSERT(written.size() == 2U);
        EXPECT(written[1].method == "$/cancelRequest");
    });

    kit.add("timeout_before_the_token_reports_timed_out", [](Fixture& f) {
        cancellation_source source;
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer
                .send_request(AddParams{}, {.token = source.token(), .timeout = 10ms})
                .or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::RequestCancelled);
        EXPECT(asked.error().message == "request timed out");
    });
}

}  // namespace kota::test
