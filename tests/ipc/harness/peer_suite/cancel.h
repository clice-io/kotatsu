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
        EXPECT(ran.has_value());
        EXPECT(scripted.has_value());
        EXPECT(!completed);
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].kind == Message::Kind::Error);
        EXPECT(written[0].id == RequestID(22));
        EXPECT(code_of(written[0].error) == ErrorCode::RequestCancelled);
        EXPECT(written[0].error.message == "request cancelled");
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
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(sum_of<A>(written[0]) == 3);
    });

    kit.add("cancel_request_for_an_unknown_id_is_ignored", [](Fixture& f) {
        f.remote.send(notification<A>("$/cancelRequest", CancelRequestParams{.id = 9999}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(f.written().empty());
    });

    kit.add("cancel_request_twice_is_answered_once", [](Fixture& f) {
        event never;
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            co_await never.wait();
            co_return AddResult{};
        });
        f.remote.send(request<A>(1, "test/add", AddParams{}));
        f.remote.send(notification<A>("$/cancelRequest", CancelRequestParams{.id = 1}));
        f.remote.send(notification<A>("$/cancelRequest", CancelRequestParams{.id = 1}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(code_of(written[0].error) == ErrorCode::RequestCancelled);
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
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 2U);
        EXPECT(written[0].id == RequestID(2));
        EXPECT(written[1].id == RequestID(1));
        EXPECT(sum_of<A>(written[1]) == 3);
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
        EXPECT(ran.has_value());
        EXPECT(!called);
    });

    // The handler passes its own cancellation to the request it sends; the
    // remote cancels the handler once it sees that request.
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
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        EXPECT(ran.has_value());
        EXPECT(scripted.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 3U);
        EXPECT(written[0].kind == Message::Kind::Request);
        EXPECT(written[0].id == RequestID(1));
        EXPECT(written[1].method == "$/cancelRequest");
        auto cancelled = decoded<CancelRequestParams, A>(written[1].body);
        ASSERT(cancelled.has_value());
        EXPECT(cancelled->id == RequestID(1));
        EXPECT(written[2].kind == Message::Kind::Error);
        EXPECT(written[2].id == RequestID(31));
        EXPECT(code_of(written[2].error) == ErrorCode::RequestCancelled);
    });

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
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::RequestCancelled);
        EXPECT(asked.error().message == "request cancelled");
        const auto& written = f.written();
        ASSERT(written.size() == 2U);
        EXPECT(written[0].id == RequestID(1));
        EXPECT(written[1].kind == Message::Kind::Notification);
        EXPECT(written[1].method == "$/cancelRequest");
        auto cancelled = decoded<CancelRequestParams, A>(written[1].body);
        ASSERT(cancelled.has_value());
        EXPECT(cancelled->id == RequestID(1));
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
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::RequestCancelled);
        EXPECT(asked.error().message == "request cancelled");
        EXPECT(f.written().empty());
    });

    // A remote may answer a request after it was told of the cancellation;
    // that answer goes nowhere.
    kit.add("answer_after_cancel_request_is_ignored", [](Fixture& f) {
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
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::RequestCancelled);
        EXPECT(f.written().size() == 2U);
    });

    // The task awaiting the request is cancelled, not the request: the
    // remote is told all the same, and the answer it may still send goes
    // nowhere.
    kit.add("send_request_whose_caller_is_cancelled_sends_cancel_request", [](Fixture& f) {
        cancellation_source source;
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.template send_request<AddResult>("worker/build", AddParams{})
                .or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            source.cancel();
            co_await f.next();
            f.remote.send(response<A>(1, AddResult{.sum = 1}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] =
            f.run(f.peer.run(), with_token(ask(), source.token()), remote());
        EXPECT(ran.has_value());
        EXPECT(asked.is_cancelled());
        const auto& written = f.written();
        ASSERT(written.size() == 2U);
        EXPECT(written[1].method == "$/cancelRequest");
        auto cancelled = decoded<CancelRequestParams, A>(written[1].body);
        ASSERT(cancelled.has_value());
        EXPECT(cancelled->id == RequestID(1));
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
        EXPECT(ran.is_cancelled());
        EXPECT(stopped.has_value());
        EXPECT(f.peer.send_notification(NoteParams{.text = "late"}).has_error());
        auto [asked] = f.run(f.peer.send_request(AddParams{}));
        EXPECT(asked.has_error());
        // At most the cancelled handler's answer.
        for(const auto& message: f.written()) {
            EXPECT(message.id == RequestID(1));
        }
    });
}

}  // namespace kota::test
