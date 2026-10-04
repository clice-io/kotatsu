#pragma once

// Cancellation both ways: $/cancelRequest from the remote cancels a running
// handler, and a cancelled send_request tells the remote with one.

#include <string>

#include "ipc/harness/peer_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::test {

template <CodecAdapter A>
void peer_cancel(const PeerKit<A>& kit) {
    using Fixture = PeerFixture<A>;
    using Context = typename Fixture::Context;
    using ipc::protocol::CancelRequestParams;
    using ipc::protocol::ErrorCode;

    kit.add("cancel_request_ends_the_running_handler", [](Fixture& f) {
        bool completed = false;
        event started;
        event never;
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            started.set();
            co_await never.wait();
            completed = true;
            co_return AddResult{};
        });
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(22, "test/add", AddParams{}));
            co_await started.wait();
            f.remote.send(notification<A>("$/cancelRequest", CancelRequestParams{.id = 22}));
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(scripted.has_value());
        ZEXPECT(!completed);
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(written[0].kind == Message::Kind::Error);
        ZEXPECT(written[0].id == RequestID(22));
        ZEXPECT(code_of(written[0].error) == ErrorCode::RequestCancelled);
        ZEXPECT(written[0].error.message == "request cancelled");
    });

    // A $/cancelRequest read with its request is dispatched after the handler
    // is called and before the task it returned starts, which then never
    // does.
    kit.add("cancel_before_a_task_starts_never_starts_it", [](Fixture& f) {
        bool called = false;
        bool started = false;
        auto answer = [&]() -> ipc::RequestResult<AddParams> {
            started = true;
            co_return AddResult{};
        };
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            called = true;
            return answer();
        });
        f.remote.send(request<A>(22, "test/add", AddParams{}));
        f.remote.send(notification<A>("$/cancelRequest", CancelRequestParams{.id = 22}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(called);
        ZEXPECT(!started);
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(written[0].kind == Message::Kind::Error);
        ZEXPECT(written[0].id == RequestID(22));
        ZEXPECT(code_of(written[0].error) == ErrorCode::RequestCancelled);
    });

    kit.add("cancel_request_after_the_answer_is_ignored", [](Fixture& f) {
        f.serve_add();
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(1, "test/add", AddParams{.a = 1, .b = 2}));
            co_await f.next();
            f.remote.send(notification<A>("$/cancelRequest", CancelRequestParams{.id = 1}));
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        ZEXPECT(ran.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(sum_of<A>(written[0]) == 3);
    });

    kit.add("cancel_request_for_an_unknown_id_is_ignored", [](Fixture& f) {
        f.remote.send(notification<A>("$/cancelRequest", CancelRequestParams{.id = 9999}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(f.written().empty());
    });

    // Both cancellations arrive while the handler runs.
    kit.add("cancel_request_twice_is_answered_once", [](Fixture& f) {
        event started;
        event never;
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            started.set();
            co_await never.wait();
            co_return AddResult{};
        });
        f.remote.send(request<A>(1, "test/add", AddParams{}));
        auto remote = [&]() -> task<> {
            co_await started.wait();
            f.remote.send(notification<A>("$/cancelRequest", CancelRequestParams{.id = 1}));
            f.remote.send(notification<A>("$/cancelRequest", CancelRequestParams{.id = 1}));
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(scripted.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(code_of(written[0].error) == ErrorCode::RequestCancelled);
    });

    // test/ping answers at once, so its answer shows the peer has read the
    // cancellations sent before it; the running request then still answers.
    kit.add("cancel_request_with_params_that_do_not_decode_is_ignored", [](Fixture& f) {
        event release;
        f.peer.on_request([&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            co_await release.wait();
            co_return AddResult{.sum = params.a + params.b};
        });
        f.peer.on_request("test/ping",
                          [](Context&, const EmptyParams&) -> ipc::RequestResult<AddParams> {
                              co_return AddResult{};
                          });
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(1, "test/add", AddParams{.a = 1, .b = 2}));
            f.remote.send(notification<A>("$/cancelRequest", NoteParams{.text = "1"}));
            f.remote.send(A::notification_raw("$/cancelRequest", A::not_a_value));
            f.remote.send(request<A>(2, "test/ping", EmptyParams{}));
            co_await f.next();
            release.set();
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        ZEXPECT(ran.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(written[0].id == RequestID(2));
        ZEXPECT(written[1].id == RequestID(1));
        ZEXPECT(sum_of<A>(written[1]) == 3);
    });

    // The peer handles $/cancelRequest itself; a handler for it is never
    // called.
    kit.add("cancel_request_handler_is_not_called", [](Fixture& f) {
        bool called = false;
        f.peer.on_notification("$/cancelRequest",
                               [&](const CancelRequestParams&) { called = true; });
        f.remote.send(notification<A>("$/cancelRequest", CancelRequestParams{.id = 5}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(!called);
    });

    // The handler passes its own cancellation to the request it sends; the
    // remote cancels the handler once it sees that request, and the handler
    // ends once the remote has answered the request it cancelled in turn.
    kit.add("handler_cancellation_cancels_its_own_request", [](Fixture& f) {
        f.peer.on_request(
            [&](Context& context, const AddParams& params) -> ipc::RequestResult<AddParams> {
                co_return co_await context
                    ->template send_request<AddResult>("client/add",
                                                       params,
                                                       {.token = context.cancellation})
                    .or_fail();
            });
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(31, "test/add", AddParams{.a = 4, .b = 5}));
            co_await f.next();
            f.remote.send(notification<A>("$/cancelRequest", CancelRequestParams{.id = 31}));
            co_await f.next();
            f.remote.send(A::error_response(
                1,
                ipc::Error(ErrorCode::RequestCancelled, "cancelled by the remote")));
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(scripted.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 3U);
        ZEXPECT(written[0].kind == Message::Kind::Request);
        ZEXPECT(written[0].id == RequestID(1));
        ZEXPECT(written[1].method == "$/cancelRequest");
        auto cancelled = decoded<CancelRequestParams, A>(written[1].body);
        ZASSERT(cancelled.has_value());
        ZEXPECT(cancelled->id == RequestID(1));
        ZEXPECT(written[2].kind == Message::Kind::Error);
        ZEXPECT(written[2].id == RequestID(31));
        ZEXPECT(code_of(written[2].error) == ErrorCode::RequestCancelled);
    });

    // The token's cancel tells the remote and waits for its answer, here
    // RequestCancelled, which is what the request ends with.
    kit.add("send_request_cancelled_by_its_token_sends_cancel_request", [](Fixture& f) {
        cancellation_source source;
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer
                .template send_request<AddResult>("worker/build",
                                                  AddParams{},
                                                  {.token = source.token()})
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
        ZEXPECT(ran.has_value());
        ZASSERT(asked.has_error());
        ZEXPECT(code_of(asked.error()) == ErrorCode::RequestCancelled);
        ZEXPECT(asked.error().message == "cancelled by the remote");
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(written[0].id == RequestID(1));
        ZEXPECT(written[1].kind == Message::Kind::Notification);
        ZEXPECT(written[1].method == "$/cancelRequest");
        auto cancelled = decoded<CancelRequestParams, A>(written[1].body);
        ZASSERT(cancelled.has_value());
        ZEXPECT(cancelled->id == RequestID(1));
    });

    kit.add("send_request_with_a_cancelled_token_fails_without_writing", [](Fixture& f) {
        cancellation_source source;
        source.cancel();
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            f.remote.end_input();
            co_return co_await f.peer
                .template send_request<AddResult>("worker/build",
                                                  AddParams{},
                                                  {.token = source.token()})
                .or_fail();
        };

        auto [ran, asked] = f.run(f.peer.run(), ask());
        ZEXPECT(ran.has_value());
        ZASSERT(asked.has_error());
        ZEXPECT(code_of(asked.error()) == ErrorCode::RequestCancelled);
        ZEXPECT(asked.error().message == "request cancelled");
        ZEXPECT(f.written().empty());
    });

    // A remote may finish a request after it was told of the cancellation:
    // the request ends with that answer.
    kit.add("answer_after_cancel_request_is_returned", [](Fixture& f) {
        cancellation_source source;
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.send_request(AddParams{}, {.token = source.token()})
                .or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            source.cancel();
            co_await f.next();
            f.remote.send(response<A>(1, AddResult{.sum = 1}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        ZEXPECT(ran.has_value());
        ZASSERT(asked.has_value());
        ZEXPECT(asked->sum == 1);
        ZEXPECT(f.written().size() == 2U);
    });

    // The token fires once the answer is read, before the caller resumes:
    // the request is settled, so the remote is not told.
    kit.add("token_fired_after_the_answer_is_read_sends_no_cancel", [](Fixture& f) {
        cancellation_source source;
        f.peer.on_notification([&](const NoteParams&) { source.cancel(); });
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.send_request(AddParams{}, {.token = source.token()})
                .or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(response<A>(1, AddResult{.sum = 1}));
            f.remote.send(notification<A>("test/note", NoteParams{.text = "cancel"}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(source.cancelled());
        ZASSERT(asked.has_value());
        ZEXPECT(asked->sum == 1);
        ZEXPECT(f.written().size() == 1U);
    });

    // The task awaiting the request is cancelled, not the request: the
    // remote is told, and the task ends cancelled once the answer is in, not
    // before.
    kit.add("send_request_whose_caller_is_cancelled_waits_for_the_answer", [](Fixture& f) {
        cancellation_source source;
        bool caller_ended = false;
        bool ended_before_the_answer = true;
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.template send_request<AddResult>("worker/build", AddParams{})
                .or_fail();
        };
        auto caller = [&]() -> task<bool> {
            auto asked = co_await with_token(ask(), source.token());
            caller_ended = true;
            co_return asked.is_cancelled();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            source.cancel();
            co_await f.next();
            ended_before_the_answer = caller_ended;
            f.remote.send(response<A>(1, AddResult{.sum = 1}));
            f.remote.end_input();
        };

        auto [ran, cancelled, scripted] = f.run(f.peer.run(), caller(), remote());
        ZEXPECT(ran.has_value());
        ZASSERT(cancelled.has_value());
        ZEXPECT(*cancelled);
        ZEXPECT(!ended_before_the_answer);
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(written[1].method == "$/cancelRequest");
        auto cancelled_id = decoded<CancelRequestParams, A>(written[1].body);
        ZASSERT(cancelled_id.has_value());
        ZEXPECT(cancelled_id->id == RequestID(1));
    });

    // A request whose remote cannot be told of the cancellation, here as
    // its output is closed, ends at once instead of waiting for an answer
    // the remote has no reason to cut short.
    kit.add("cancel_that_cannot_be_sent_ends_the_request_at_once", [](Fixture& f) {
        cancellation_source source;
        event done;
        auto ask = [&]() -> task<ipc::Error> {
            auto asked = co_await f.peer.send_request(AddParams{}, {.token = source.token()});
            done.set();
            co_return asked.has_error() ? asked.error() : ipc::Error("answered");
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.peer.close_output();
            source.cancel();
            co_await done.wait();
            f.remote.end_input();
        };

        auto [ran, failure, scripted] = f.run(f.peer.run(), ask(), remote());
        ZEXPECT(ran.has_value());
        ZASSERT(failure.has_value());
        ZEXPECT(code_of(*failure) == ErrorCode::RequestCancelled);
        ZEXPECT(f.written().size() == 1U);
    });

    // A request the remote never answers still lets a cancelled caller go
    // once the connection closes.
    kit.add("cancelled_caller_ends_when_the_connection_closes", [](Fixture& f) {
        cancellation_source source;
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.send_request(AddParams{}).or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            source.cancel();
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, asked, scripted] =
            f.run(f.peer.run(), with_token(ask(), source.token()), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(asked.is_cancelled());
    });

    // Once run() is cancelled, here while a handler runs, nothing more can
    // be sent.
    kit.add("sending_after_run_is_cancelled_fails", [](Fixture& f) {
        cancellation_source source;
        event started;
        event never;
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            started.set();
            co_await never.wait();
            co_return AddResult{};
        });
        f.remote.send(request<A>(1, "test/add", AddParams{}));
        auto stop = [&]() -> task<> {
            co_await started.wait();
            source.cancel();
        };

        auto [ran, stopped] = f.run(with_token(f.peer.run(), source.token()), stop());
        ZEXPECT(ran.is_cancelled());
        ZEXPECT(stopped.has_value());
        auto sent = f.peer.send_notification(NoteParams{.text = "late"});
        ZASSERT(sent.has_error());
        ZEXPECT(code_of(sent.error()) == ErrorCode::ConnectionClosed);
        auto [asked] = f.run(f.peer.send_request(AddParams{}));
        ZASSERT(asked.has_error());
        ZEXPECT(code_of(asked.error()) == ErrorCode::ConnectionClosed);
        // At most the cancelled handler's answer.
        for(const auto& message: f.written()) {
            ZEXPECT(message.id == RequestID(1));
        }
    });
}

}  // namespace kota::test
