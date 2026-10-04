#pragma once

// Timeouts on send_request, alone and with a cancellation token. The remote
// never answers the requests that time out; a timeout is the subject here, so
// these cases wait for real time, as little as they can.

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

    // Deadlines expire in order, not as requests were sent: the later one
    // with the shorter timeout expires while the first still waits.
    kit.add("shorter_timeout_sent_later_expires_first", [](Fixture& f) {
        auto first = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.send_request(AddParams{.a = 2, .b = 3}, {.timeout = 1min})
                .or_fail();
        };
        auto second = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.send_request(AddParams{}, {.timeout = 10ms}).or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            co_await f.next();
            co_await f.next();
            f.remote.send(response<A>(1, AddResult{.sum = 5}));
            f.remote.end_input();
        };

        auto [ran, answered, timed_out, scripted] =
            f.run(f.peer.run(), first(), second(), remote());
        EXPECT(ran.has_value());
        ASSERT(answered.has_value());
        EXPECT(answered->sum == 5);
        ASSERT(timed_out.has_error());
        EXPECT(timed_out.error().message == "request timed out");
        const auto& written = f.written();
        ASSERT(written.size() == 3U);
        auto cancelled = decoded<CancelRequestParams, A>(written[2].body);
        ASSERT(cancelled.has_value());
        EXPECT(cancelled->id == RequestID(2));
    });

    // A deadline past what the clock counts is never reached.
    kit.add("timeout_past_the_clocks_range_never_passes", [](Fixture& f) {
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer
                .send_request(AddParams{.a = 2, .b = 3},
                              {.timeout = std::chrono::milliseconds::max()})
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

    // A cancelled request goes on waiting for its answer, but no longer than
    // its timeout: the remote is told once.
    kit.add("timeout_ends_a_cancelled_request_the_remote_never_answers", [](Fixture& f) {
        cancellation_source source;
        event done;
        auto ask = [&]() -> task<ipc::Error> {
            auto asked = co_await f.peer.send_request(AddParams{},
                                                      {.token = source.token(), .timeout = 20ms});
            done.set();
            co_return asked.has_error() ? asked.error() : ipc::Error("answered");
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            source.cancel();
            co_await f.next();
            co_await done.wait();
            f.remote.end_input();
        };

        auto [ran, failure, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        ASSERT(failure.has_value());
        EXPECT(code_of(*failure) == ErrorCode::RequestCancelled);
        EXPECT(failure->message == "request timed out");
        const auto& written = f.written();
        ASSERT(written.size() == 2U);
        EXPECT(written[1].method == "$/cancelRequest");
    });

    // The timeout caps the wait of a cancelled caller too.
    kit.add("timeout_ends_the_wait_of_a_cancelled_caller", [](Fixture& f) {
        cancellation_source source;
        event done;
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.send_request(AddParams{}, {.timeout = 20ms}).or_fail();
        };
        auto caller = [&]() -> task<bool> {
            auto asked = co_await with_token(ask(), source.token());
            done.set();
            co_return asked.is_cancelled();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            source.cancel();
            co_await f.next();
            co_await done.wait();
            f.remote.end_input();
        };

        auto [ran, cancelled, scripted] = f.run(f.peer.run(), caller(), remote());
        EXPECT(ran.has_value());
        ASSERT(cancelled.has_value());
        EXPECT(*cancelled);
        EXPECT(f.written().size() == 2U);
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
