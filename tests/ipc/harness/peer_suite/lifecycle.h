#pragma once

// Lifecycle: how run() ends (the input ends, close(), a failed write, an
// outside cancellation), what becomes of pending requests and running
// handlers, and what close_output() leaves open.

#include <optional>
#include <string>
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
        EXPECT(code_of(asked.error()) == ErrorCode::RequestFailed);
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
        EXPECT(code_of(asked.error()) == ErrorCode::RequestFailed);
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
        EXPECT(code_of(sent.error()) == ErrorCode::RequestFailed);
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::RequestFailed);
        EXPECT(f.written().empty());
    });

    kit.add("write_failure_fails_pending_requests_and_closes_the_transport", [](Fixture& f) {
        f.remote.fail_writes();

        auto [ran, asked] = f.run(f.peer.run(), f.peer.send_request(AddParams{}));
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::RequestFailed);
        EXPECT(asked.error().message == "write failed");
        EXPECT(f.remote.closed());
    });

    kit.add("close_output_ends_the_remote_input_and_keeps_reading", [](Fixture& f) {
        std::vector<std::string> seen;
        f.peer.on_notification([&](const NoteParams& params) { seen.push_back(params.text); });
        auto closed = f.peer.close_output();
        f.remote.send(notification<A>("test/note", NoteParams{.text = "after"}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        ASSERT(closed.has_value());
        EXPECT(f.remote.output_ended());
        EXPECT(!f.remote.closed());
        EXPECT(seen == std::vector<std::string>{"after"});
    });

    kit.add("cancelling_run_cancels_running_handlers", [](Fixture& f) {
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
            source.cancel();
        };

        auto [ran, scripted] = f.run(with_token(f.peer.run(), source.token()), remote());
        EXPECT(ran.is_cancelled());
        EXPECT(!completed);
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

/// Once the input has ended no answer can arrive, so a request a handler
/// sends then fails at once instead of waiting for good. The probe request
/// fails only when the peer reads the end of its input, so the handler is
/// released after that.
template <CodecAdapter A>
void request_from_a_handler_after_end_of_input_fails() {
    PeerFixture<A> f;
    event started;
    event input_ended;
    event release;
    std::optional<ipc::Error> failure;
    f.peer.on_request([&](typename PeerFixture<A>::Context& context,
                          const AddParams&) -> ipc::RequestResult<AddParams> {
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
    EXPECT(code_of(*failure) == ipc::protocol::ErrorCode::RequestFailed);
}

/// close_output() half-closes after what is queued has been written.
template <CodecAdapter A>
void close_output_writes_queued_messages_first() {
    PeerFixture<A> f;
    auto sent = f.peer.send_notification(NoteParams{.text = "queued"});
    auto closed = f.peer.close_output();
    f.remote.end_input();

    auto [ran] = f.run(f.peer.run());
    EXPECT(ran.has_value());
    ASSERT(sent.has_value());
    ASSERT(closed.has_value());
    const auto& written = f.written();
    ASSERT(written.size() == 1U);
    EXPECT(written[0].method == "test/note");
    EXPECT(f.remote.output_ended());
}

/// After close_output() a send fails at once, and the input stays open.
template <CodecAdapter A>
void send_after_close_output_fails() {
    PeerFixture<A> f;
    std::vector<std::string> seen;
    f.peer.on_notification([&](const NoteParams& params) { seen.push_back(params.text); });
    ASSERT(f.peer.close_output().has_value());
    auto sent = f.peer.send_notification(NoteParams{.text = "late"});
    f.remote.send(notification<A>("test/note", NoteParams{.text = "after"}));
    f.remote.end_input();

    auto [ran] = f.run(f.peer.run());
    EXPECT(ran.has_value());
    ASSERT(sent.has_error());
    EXPECT(code_of(sent.error()) == ipc::protocol::ErrorCode::RequestFailed);
    EXPECT(seen == std::vector<std::string>{"after"});
}

}  // namespace kota::test
