#pragma once

// Lifecycle: how run() ends (the input ends, close(), a failed write, an
// outside cancellation), what becomes of pending requests and running
// handlers, and what close_output() leaves open.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ipc/harness/peer_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::test {

template <CodecAdapter A>
void peer_lifecycle(const PeerKit<A>& kit) {
    using Fixture = PeerFixture<A>;
    using Context = typename Fixture::Context;
    using ipc::protocol::ErrorCode;

    kit.add("end_of_input_fails_pending_requests", [](Fixture& f) {
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, asked, scripted] =
            f.run(f.peer.run(), f.peer.send_request(AddParams{}), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::ConnectionClosed);
        EXPECT(asked.error().message == "transport closed");
    });

    kit.add("end_of_input_lets_running_handlers_answer", [](Fixture& f) {
        event started;
        event release;
        f.peer.on_request([&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            started.set();
            co_await release.wait();
            co_return AddResult{.sum = params.a + params.b};
        });
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(1, "test/add", AddParams{.a = 1, .b = 2}));
            co_await started.wait();
            f.remote.end_input();
            release.set();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(sum_of<A>(written[0]) == 3);
    });

    // Nothing more is written once the input has ended and every answer is
    // out, so the remote reads the end of its input.
    kit.add("end_of_input_ends_the_output_once_answered", [](Fixture& f) {
        f.peer.on_request([](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            co_return AddResult{.sum = params.a + params.b};
        });
        f.remote.send(request<A>(1, "test/add", AddParams{.a = 1, .b = 2}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(sum_of<A>(written[0]) == 3);
        EXPECT(f.remote.output_ended());
        EXPECT(!f.remote.closed());
    });

    kit.add("close_ends_run", [](Fixture& f) {
        auto closer = [&]() -> task<ipc::Result<void>> {
            co_return f.peer.close();
        };

        auto [ran, closed] = f.run(f.peer.run(), closer());
        EXPECT(ran.has_value());
        ASSERT(closed.has_value());
        EXPECT(closed->has_value());
        EXPECT(f.remote.closed());
    });

    kit.add("close_twice_succeeds", [](Fixture& f) {
        auto closer = [&]() -> task<std::vector<bool>> {
            auto first = f.peer.close();
            auto second = f.peer.close();
            co_return std::vector{first.has_value(), second.has_value()};
        };

        auto [ran, closed] = f.run(f.peer.run(), closer());
        EXPECT(ran.has_value());
        ASSERT(closed.has_value());
        EXPECT(*closed == std::vector{true, true});
    });

    kit.add("run_after_close_returns_at_once", [](Fixture& f) {
        ASSERT(f.peer.close().has_value());

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(f.remote.closed());
    });

    kit.add("close_fails_pending_requests", [](Fixture& f) {
        auto closer = [&]() -> task<> {
            co_await f.next();
            f.peer.close();
        };

        auto [ran, asked, closed] = f.run(f.peer.run(), f.peer.send_request(AddParams{}), closer());
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::ConnectionClosed);
        EXPECT(asked.error().message == "peer closed");
    });

    // test/ping answers before the close; the two test/add handlers are
    // still running at it, and are cancelled without answering.
    kit.add("close_cancels_running_handlers", [](Fixture& f) {
        int started = 0;
        int completed = 0;
        event both_started;
        event never;
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            started += 1;
            if(started == 2) {
                both_started.set();
            }
            co_await never.wait();
            completed += 1;
            co_return AddResult{};
        });
        f.peer.on_request("test/ping",
                          [](Context&, const EmptyParams&) -> ipc::RequestResult<AddParams> {
                              co_return AddResult{};
                          });
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(1, "test/ping", EmptyParams{}));
            f.remote.send(request<A>(2, "test/add", AddParams{}));
            f.remote.send(request<A>(3, "test/add", AddParams{}));
            co_await f.next();
            co_await both_started.wait();
            f.peer.close();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        EXPECT(ran.has_value());
        EXPECT(started == 2);
        EXPECT(completed == 0);
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].id == RequestID(1));
    });

    kit.add("close_discards_queued_messages", [](Fixture& f) {
        auto closer = [&]() -> task<ipc::Result<void>> {
            auto sent = f.peer.send_notification(NoteParams{.text = "queued"});
            f.peer.close();
            co_return sent;
        };

        auto [ran, sent] = f.run(f.peer.run(), closer());
        EXPECT(ran.has_value());
        ASSERT(sent.has_value());
        EXPECT(sent->has_value());
        EXPECT(f.written().empty());
    });

    kit.add("close_in_a_handler_ends_run", [](Fixture& f) {
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            f.peer.close();
            co_return AddResult{};
        });
        f.remote.send(request<A>(1, "test/add", AddParams{}));

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(f.remote.closed());
        EXPECT(f.written().empty());
    });

    kit.add("send_after_close_fails", [](Fixture& f) {
        ASSERT(f.peer.close().has_value());
        auto sent = f.peer.send_notification(NoteParams{.text = "late"});

        auto [ran, asked] = f.run(f.peer.run(), f.peer.send_request(AddParams{}));
        EXPECT(ran.has_value());
        ASSERT(sent.has_error());
        EXPECT(code_of(sent.error()) == ErrorCode::ConnectionClosed);
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::ConnectionClosed);
        EXPECT(f.written().empty());
    });

    kit.add("write_failure_fails_pending_requests_and_closes_the_transport", [](Fixture& f) {
        f.remote.fail_writes();
        auto ask = [&]() -> task<std::pair<ipc::Error, ipc::Result<void>>> {
            auto asked = co_await f.peer.send_request(AddParams{});
            co_return std::pair{asked.has_error() ? asked.error() : ipc::Error("no error"),
                                f.peer.send_notification(NoteParams{.text = "after"})};
        };

        auto [ran, asked] = f.run(f.peer.run(), ask());
        EXPECT(ran.has_value());
        ASSERT(asked.has_value());
        auto& [failure, after] = *asked;
        EXPECT(code_of(failure) == ErrorCode::ConnectionClosed);
        EXPECT(failure.message == "write failed");
        ASSERT(after.has_error());
        EXPECT(code_of(after.error()) == ErrorCode::ConnectionClosed);
        EXPECT(f.remote.closed());
    });

    // The handler's answer could not be written: it is cancelled, and run()
    // ends without waiting for it.
    kit.add("write_failure_cancels_running_handlers", [](Fixture& f) {
        event started;
        event never;
        bool cancelled = false;
        f.peer.on_request([&](Context& context, const AddParams&) -> ipc::RequestResult<AddParams> {
            started.set();
            co_await wait_for(never).catch_cancel();
            cancelled = context.cancelled();
            co_return AddResult{};
        });
        f.remote.fail_writes();
        f.remote.send(request<A>(1, "test/add", AddParams{}));
        auto ask = [&]() -> task<> {
            co_await started.wait();
            co_await f.peer.send_request(AddParams{});
        };

        auto [ran, asked] = f.run(f.peer.run(), ask());
        EXPECT(ran.has_value());
        EXPECT(asked.has_value());
        EXPECT(cancelled);
        EXPECT(f.remote.closed());
    });

    kit.add("close_output_ends_the_remote_input_and_keeps_reading", [](Fixture& f) {
        std::vector<std::string> seen;
        f.peer.on_notification([&](const NoteParams& params) { seen.push_back(params.text); });
        f.peer.close_output();
        f.remote.send(notification<A>("test/note", NoteParams{.text = "after"}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(f.remote.output_ended());
        EXPECT(!f.remote.closed());
        EXPECT(seen == std::vector<std::string>{"after"});
    });

    kit.add("close_output_writes_queued_messages_first", [](Fixture& f) {
        auto sent = f.peer.send_notification(NoteParams{.text = "queued"});
        f.peer.close_output();
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(sent.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].method == "test/note");
        EXPECT(f.remote.output_ended());
    });

    // A half-close that fails leaves the remote without the end of its
    // input, so the peer closes the connection.
    kit.add("failed_close_output_fails_pending_requests_and_closes_the_transport", [](Fixture& f) {
        f.remote.fail_close_output();
        auto closer = [&]() -> task<> {
            co_await f.next();
            f.peer.close_output();
        };

        auto [ran, asked, scripted] =
            f.run(f.peer.run(), f.peer.send_request(AddParams{}), closer());
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::ConnectionClosed);
        EXPECT(f.remote.closed());
    });

    kit.add("send_after_close_output_fails", [](Fixture& f) {
        std::vector<std::string> seen;
        f.peer.on_notification([&](const NoteParams& params) { seen.push_back(params.text); });
        f.peer.close_output();
        auto sent = f.peer.send_notification(NoteParams{.text = "late"});
        f.remote.send(notification<A>("test/note", NoteParams{.text = "after"}));
        f.remote.end_input();

        auto [ran, asked] = f.run(f.peer.run(), f.peer.send_request(AddParams{}));
        EXPECT(ran.has_value());
        ASSERT(sent.has_error());
        EXPECT(code_of(sent.error()) == ErrorCode::ConnectionClosed);
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::ConnectionClosed);
        EXPECT(seen == std::vector<std::string>{"after"});
        EXPECT(f.written().empty());
    });

    // A handler still running when the peer's output closes finishes, but
    // its answer is dropped.
    kit.add("answer_after_close_output_is_dropped", [](Fixture& f) {
        event started;
        event release;
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            started.set();
            co_await release.wait();
            co_return AddResult{};
        });
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(1, "test/add", AddParams{}));
            co_await started.wait();
            f.peer.close_output();
            co_await f.next();
            release.set();
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        EXPECT(ran.has_value());
        EXPECT(f.remote.output_ended());
        EXPECT(f.written().empty());
    });

    // The request is pending when the input ends and fails with it; the
    // handler runs on and its own request fails at once, since nothing
    // could answer it.
    kit.add("request_after_end_of_input_fails", [](Fixture& f) {
        event started;
        event input_ended;
        event release;
        std::optional<ipc::Error> failure;
        f.peer.on_request([&](Context& context, const AddParams&) -> ipc::RequestResult<AddParams> {
            started.set();
            co_await release.wait();
            auto nested = co_await context->send_request(AddParams{});
            if(nested.has_error()) {
                failure = nested.error();
            }
            co_return AddResult{};
        });
        auto probe = [&]() -> task<> {
            co_await f.peer.send_request(AddParams{});
            input_ended.set();
        };
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(1, "test/add", AddParams{}));
            co_await started.wait();
            f.remote.end_input();
            co_await input_ended.wait();
            release.set();
        };

        auto [ran, probed, scripted] = f.run(f.peer.run(), probe(), remote());
        EXPECT(ran.has_value());
        ASSERT(failure.has_value());
        EXPECT(code_of(*failure) == ErrorCode::ConnectionClosed);
        EXPECT(failure->message == "peer input closed");
    });

    kit.add("cancelling_run_cancels_running_handlers_and_fails_pending_requests", [](Fixture& f) {
        bool completed = false;
        event started;
        event never;
        cancellation_source source;
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            started.set();
            co_await never.wait();
            completed = true;
            co_return AddResult{};
        });
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(1, "test/add", AddParams{}));
            co_await started.wait();
            co_await f.next();
            source.cancel();
        };

        auto [ran, asked, scripted] = f.run(with_token(f.peer.run(), source.token()),
                                            f.peer.send_request(AddParams{}),
                                            remote());
        EXPECT(ran.is_cancelled());
        EXPECT(!completed);
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::ConnectionClosed);
    });

    // Nothing can use the peer again once run() is cancelled, so its
    // transport closes, and the remote reads the end.
    kit.add("cancelling_run_closes_the_transport", [](Fixture& f) {
        cancellation_source source;
        auto canceller = [&]() -> task<> {
            source.cancel();
            co_return;
        };

        auto [ran, cancelled] = f.run(with_token(f.peer.run(), source.token()), canceller());
        EXPECT(ran.is_cancelled());
        EXPECT(f.remote.closed());
    });

    kit.add("two_peers_answer_on_one_loop", [](Fixture& f) {
        Remote other_remote;
        typename Fixture::Peer other(f.loop, other_remote.transport());
        f.serve_add();
        other.on_request([](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            co_return AddResult{.sum = params.a * params.b};
        });
        f.remote.send(request<A>(11, "test/add", AddParams{.a = 2, .b = 5}));
        f.remote.end_input();
        other_remote.send(request<A>(22, "test/add", AddParams{.a = 7, .b = 3}));
        other_remote.end_input();

        auto [ran, other_ran] = f.run(f.peer.run(), other.run());
        EXPECT(ran.has_value());
        EXPECT(other_ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].id == RequestID(11));
        EXPECT(sum_of<A>(written[0]) == 7);
        auto other_written = other_remote.drain();
        ASSERT(other_written.size() == 1U);
        auto answer = A::read(other_written[0]);
        ASSERT(answer.has_value());
        EXPECT(answer->id == RequestID(22));
        EXPECT(sum_of<A>(*answer) == 21);
    });
}

}  // namespace kota::test
