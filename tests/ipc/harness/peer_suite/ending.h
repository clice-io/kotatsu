#pragma once

// How a conversation ends: what ended() says of it, and the messages that
// end it under ViolationPolicy::End, or that it reads past.

#include <optional>
#include <string>
#include <utility>

#include "ipc/harness/peer_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::test {

/// What a request failed with, and what Peer::ended() said once it had.
struct EndedRequest {
    ipc::Error error;
    std::optional<ipc::ConversationEnd> ended;
};

/// Sends test/add, which is to fail, and reports how.
template <CodecAdapter A>
task<EndedRequest> failing_request(PeerFixture<A>& f) {
    auto asked = co_await f.peer.send_request(AddParams{});
    co_return EndedRequest{
        .error = asked.has_error() ? asked.error() : ipc::Error("answered"),
        .ended = f.peer.ended(),
    };
}

template <CodecAdapter A>
void peer_ending(const PeerKit<A>& kit) {
    using Fixture = PeerFixture<A>;
    using Context = typename Fixture::Context;
    using Kind = ipc::ConversationEnd::Kind;
    using ipc::protocol::ErrorCode;

    kit.add("ended_is_none_until_the_conversation_ends", [](Fixture& f) {
        ZEXPECT(!f.peer.ended().has_value());
        std::optional<ipc::ConversationEnd> during;
        auto remote = [&]() -> task<> {
            co_await f.next();
            during = f.peer.ended();
            f.remote.end_input();
        };

        auto [ran, asked, scripted] =
            f.run(f.peer.run(), f.peer.send_request(AddParams{}), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(!during.has_value());
        ZEXPECT(f.peer.ended().has_value());
    });

    // What the transport said of the end is what the request fails with.
    kit.add("end_of_input_ends_it_with_what_the_transport_said", [](Fixture& f) {
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, failed, scripted] = f.run(f.peer.run(), failing_request(f), remote());
        ZEXPECT(ran.has_value());
        ZASSERT(failed.has_value());
        ZEXPECT(code_of(failed->error) == ErrorCode::ConnectionClosed);
        ZEXPECT(failed->error.message == "input ended");
        ZASSERT(failed->ended.has_value());
        ZEXPECT(failed->ended->kind == Kind::InputEnded);
        ZEXPECT(failed->ended->message == "input ended");
    });

    kit.add("malformed_frame_ends_it_malformed", [](Fixture& f) {
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send_unreadable(ipc::ReadError{
                .kind = ipc::ReadError::Kind::Malformed,
                .message = "missing Content-Length",
            });
        };

        auto [ran, failed, scripted] = f.run(f.peer.run(), failing_request(f), remote());
        ZEXPECT(ran.has_value());
        ZASSERT(failed.has_value());
        ZASSERT(failed->ended.has_value());
        ZEXPECT(failed->ended->kind == Kind::Malformed);
        ZEXPECT(failed->ended->message == "missing Content-Length");
    });

    // The transport closes then, which ends the input too: the first reason
    // is the one kept.
    kit.add("write_failure_ends_it_write_failed", [](Fixture& f) {
        f.remote.fail_writes();

        auto [ran, failed] = f.run(f.peer.run(), failing_request(f));
        ZEXPECT(ran.has_value());
        ZASSERT(failed.has_value());
        ZEXPECT(code_of(failed->error) == ErrorCode::ConnectionClosed);
        ZEXPECT(failed->error.message == "write failed");
        ZASSERT(failed->ended.has_value());
        ZEXPECT(failed->ended->kind == Kind::WriteFailed);
        ZEXPECT(failed->ended->message == "write failed");
        auto last = f.peer.ended();
        ZASSERT(last.has_value());
        ZEXPECT(last->kind == Kind::WriteFailed);
    });

    kit.add("failed_close_output_ends_it_write_failed", [](Fixture& f) {
        f.remote.fail_close_output();
        auto closer = [&]() -> task<> {
            co_await f.next();
            f.peer.close_output();
        };

        auto [ran, failed, scripted] = f.run(f.peer.run(), failing_request(f), closer());
        ZEXPECT(ran.has_value());
        ZASSERT(failed.has_value());
        ZASSERT(failed->ended.has_value());
        ZEXPECT(failed->ended->kind == Kind::WriteFailed);
        ZEXPECT(failed->ended->message == "close_output failed");
    });

    // Its own half-close leaves the answers to come.
    kit.add("close_output_leaves_it_going", [](Fixture& f) {
        std::optional<ipc::ConversationEnd> after;
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.peer.close_output();
            // Nothing more: the output has ended.
            co_await f.next();
            after = f.peer.ended();
            f.remote.send(response<A>(1, AddResult{.sum = 1}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] =
            f.run(f.peer.run(), f.peer.send_request(AddParams{}), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(f.remote.output_ended());
        ZEXPECT(!after.has_value());
        ZASSERT(asked.has_value());
        ZEXPECT(asked->sum == 1);
    });

    kit.add("close_ends_it_closed", [](Fixture& f) {
        auto closer = [&]() -> task<> {
            co_await f.next();
            f.peer.close();
        };

        auto [ran, failed, scripted] = f.run(f.peer.run(), failing_request(f), closer());
        ZEXPECT(ran.has_value());
        ZASSERT(failed.has_value());
        ZASSERT(failed->ended.has_value());
        ZEXPECT(failed->ended->kind == Kind::Closed);
        ZEXPECT(failed->ended->message == "peer closed");
    });

    kit.add("cancelled_run_ends_it_closed", [](Fixture& f) {
        cancellation_source source;
        auto remote = [&]() -> task<> {
            co_await f.next();
            source.cancel();
        };

        auto [ran, failed, scripted] =
            f.run(with_token(f.peer.run(), source.token()), failing_request(f), remote());
        ZEXPECT(ran.is_cancelled());
        ZASSERT(failed.has_value());
        ZEXPECT(code_of(failed->error) == ErrorCode::ConnectionClosed);
        ZEXPECT(failed->error.message == "run() cancelled");
        ZASSERT(failed->ended.has_value());
        ZEXPECT(failed->ended->kind == Kind::Closed);
    });

    kit.add("close_after_the_end_keeps_the_first_reason", [](Fixture& f) {
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(f.peer.close().has_value());
        auto ended = f.peer.ended();
        ZASSERT(ended.has_value());
        ZEXPECT(ended->kind == Kind::InputEnded);
    });

    // The remote's answer, sent after it, is never read; what the peer
    // answers the message with still goes out.
    kit.add("garbage_ends_it_under_end", [](Fixture& f) {
        f.peer.set_violation_policy(ipc::ViolationPolicy::End);
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(std::string(A::garbage));
            f.remote.send(response<A>(1, AddResult{.sum = 1}));
        };

        auto [ran, failed, scripted] = f.run(f.peer.run(), failing_request(f), remote());
        ZEXPECT(ran.has_value());
        ZASSERT(failed.has_value());
        ZEXPECT(code_of(failed->error) == ErrorCode::ConnectionClosed);
        ZASSERT(failed->ended.has_value());
        ZEXPECT(failed->ended->kind == Kind::Malformed);
        ZEXPECT(failed->error.message == failed->ended->message);
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(written[1].kind == Message::Kind::Error);
        ZEXPECT(code_of(written[1].error) == ErrorCode::ParseError);
        ZEXPECT(written[1].error.message == failed->ended->message);
        ZEXPECT(f.remote.output_ended());
    });

    kit.add("error_response_without_an_id_ends_it_under_end", [](Fixture& f) {
        f.peer.set_violation_policy(ipc::ViolationPolicy::End);
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(
                A::error_response(std::nullopt, ipc::Error(ErrorCode::ParseError, "unreadable")));
        };

        auto [ran, failed, scripted] = f.run(f.peer.run(), failing_request(f), remote());
        ZEXPECT(ran.has_value());
        ZASSERT(failed.has_value());
        ZASSERT(failed->ended.has_value());
        ZEXPECT(failed->ended->kind == Kind::Malformed);
        ZEXPECT(failed->ended->message == "error response without an id: unreadable");
        ZEXPECT(f.written().size() == 1U);
    });

    kit.add("unhandled_notification_ends_it_under_end", [](Fixture& f) {
        f.peer.set_violation_policy(ipc::ViolationPolicy::End);
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(notification<A>("test/note", NoteParams{.text = "hello"}));
        };

        auto [ran, failed, scripted] = f.run(f.peer.run(), failing_request(f), remote());
        ZEXPECT(ran.has_value());
        ZASSERT(failed.has_value());
        ZASSERT(failed->ended.has_value());
        ZEXPECT(failed->ended->kind == Kind::Malformed);
        ZEXPECT(failed->ended->message == "unhandled notification: test/note");
    });

    kit.add("notification_params_that_do_not_decode_end_it_under_end", [](Fixture& f) {
        f.peer.set_violation_policy(ipc::ViolationPolicy::End);
        bool called = false;
        f.peer.on_notification([&](const NoteParams&) { called = true; });
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(A::notification_raw("test/note", A::not_a_value));
        };

        auto [ran, failed, scripted] = f.run(f.peer.run(), failing_request(f), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(!called);
        ZASSERT(failed.has_value());
        ZASSERT(failed->ended.has_value());
        ZEXPECT(failed->ended->kind == Kind::Malformed);
        ZEXPECT(zest::starts_with(failed->ended->message,
                                  "notification 'test/note' params deserialization failed: "));
    });

    // The peer stops reading, not answering: a handler already running
    // answers, and run() returns once it has.
    kit.add("handler_running_at_a_violation_still_answers_under_end", [](Fixture& f) {
        f.peer.set_violation_policy(ipc::ViolationPolicy::End);
        event release;
        f.peer.on_request([&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            co_await release.wait();
            co_return AddResult{.sum = params.a + params.b};
        });
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(3, "test/add", AddParams{.a = 1, .b = 2}));
            f.remote.send(std::string(A::garbage));
            co_await f.next();
            release.set();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        ZEXPECT(ran.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(code_of(written[0].error) == ErrorCode::ParseError);
        ZEXPECT(written[1].id == RequestID(3));
        ZEXPECT(sum_of<A>(written[1]) == 3);
        auto ended = f.peer.ended();
        ZASSERT(ended.has_value());
        ZEXPECT(ended->kind == Kind::Malformed);
    });

    // A request the peer answers with an error, a $/cancelRequest it cannot
    // read and answers to no pending request leave the protocol whole.
    kit.add("answered_and_late_messages_do_not_end_it_under_end", [](Fixture& f) {
        f.peer.set_violation_policy(ipc::ViolationPolicy::End);
        f.serve_add();
        event answered;
        std::optional<ipc::ConversationEnd> ended;
        auto ask = [&]() -> task<ipc::Result<AddResult>> {
            auto asked = co_await f.peer.send_request(AddParams{});
            ended = f.peer.ended();
            answered.set();
            co_return asked;
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(request<A>(5, "test/unknown", AddParams{}));
            f.remote.send(A::request_raw(6, "test/add", A::not_a_value));
            f.remote.send(A::notification_raw("$/cancelRequest", A::not_a_value));
            f.remote.send(response<A>(99, AddResult{}));
            f.remote.send(A::error_response(RequestID(98), ipc::Error("late")));
            f.remote.send(response<A>(1, AddResult{.sum = 3}));
            co_await answered.wait();
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        ZEXPECT(ran.has_value());
        ZASSERT(asked.has_value());
        ZASSERT(asked->has_value());
        ZEXPECT((*asked)->sum == 3);
        ZEXPECT(!ended.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 3U);
        ZEXPECT(code_of(written[1].error) == ErrorCode::MethodNotFound);
        ZEXPECT(code_of(written[2].error) == ErrorCode::InvalidParams);
    });

    // What JSON-RPC asks of a server: answer, and read on.
    kit.add("violations_leave_it_going_by_default", [](Fixture& f) {
        event answered;
        std::optional<ipc::ConversationEnd> ended;
        auto ask = [&]() -> task<ipc::Result<AddResult>> {
            auto asked = co_await f.peer.send_request(AddParams{});
            ended = f.peer.ended();
            answered.set();
            co_return asked;
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(std::string(A::garbage));
            f.remote.send(A::error_response(std::nullopt, ipc::Error("unreadable")));
            f.remote.send(notification<A>("test/note", NoteParams{}));
            f.remote.send(response<A>(1, AddResult{.sum = 3}));
            co_await answered.wait();
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        ZEXPECT(ran.has_value());
        ZASSERT(asked.has_value());
        ZASSERT(asked->has_value());
        ZEXPECT((*asked)->sum == 3);
        ZEXPECT(!ended.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(code_of(written[1].error) == ErrorCode::ParseError);
    });
}

}  // namespace kota::test
