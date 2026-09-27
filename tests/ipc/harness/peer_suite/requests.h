#pragma once

// Requests and notifications the peer sends: what it writes, and what
// send_request returns for each way the remote answers.

#include <string>
#include <utility>

#include "ipc/harness/peer_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::test {

template <CodecAdapter A>
void peer_requests(const PeerKit<A>& kit) {
    using Fixture = PeerFixture<A>;
    using Context = typename Fixture::Context;
    using ipc::protocol::ErrorCode;

    kit.add("send_request_returns_the_result", [](Fixture& f) {
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer
                .template send_request<AddResult>("worker/build", AddParams{.a = 2, .b = 3})
                .or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(response<A>(1, AddResult{.sum = 9}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        EXPECT(scripted.has_value());
        ASSERT(asked.has_value());
        EXPECT(asked->sum == 9);
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].kind == Message::Kind::Request);
        EXPECT(written[0].id == RequestID(1));
        EXPECT(written[0].method == "worker/build");
        auto params = decoded<AddParams, A>(written[0].body);
        ASSERT(params.has_value());
        EXPECT(*params == AddParams{.a = 2, .b = 3});
    });

    kit.add("send_request_by_traits_names_the_traits_method", [](Fixture& f) {
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.send_request(AddParams{.a = 1, .b = 1}).or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(response<A>(1, AddResult{.sum = 2}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_value());
        EXPECT(asked->sum == 2);
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].method == "test/add");
    });

    kit.add("send_by_tag_names_the_tag_method", [](Fixture& f) {
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_await or_fail(
                f.peer.template send_notification<TaggedNote>(NoteParams{.text = "tagged"}));
            co_return co_await f.peer.template send_request<TaggedAdd>(AddParams{.a = 42, .b = 58})
                .or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            co_await f.next();
            f.remote.send(response<A>(1, AddResult{.sum = 100}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_value());
        EXPECT(asked->sum == 100);
        const auto& written = f.written();
        ASSERT(written.size() == 2U);
        EXPECT(written[0].kind == Message::Kind::Notification);
        EXPECT(written[0].method == "test/taggedNote");
        EXPECT(written[1].kind == Message::Kind::Request);
        EXPECT(written[1].method == "test/taggedAdd");
    });

    kit.add("send_request_returns_the_error_response", [](Fixture& f) {
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.template send_request<AddResult>("worker/build", AddParams{})
                .or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(A::error_response(1, ipc::Error(-32001, "remote failed")));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(asked.error().code == -32001);
        EXPECT(asked.error().message == "remote failed");
    });

    kit.add("send_request_ids_count_up_from_one", [](Fixture& f) {
        auto ask = [&]() -> task<std::pair<AddResult, AddResult>, ipc::Error> {
            auto first = co_await f.peer.send_request(AddParams{.a = 1, .b = 0}).or_fail();
            auto second = co_await f.peer.send_request(AddParams{.a = 2, .b = 0}).or_fail();
            co_return std::pair{first, second};
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(response<A>(1, AddResult{.sum = 1}));
            co_await f.next();
            f.remote.send(response<A>(2, AddResult{.sum = 2}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_value());
        EXPECT(asked->first.sum == 1);
        EXPECT(asked->second.sum == 2);
        const auto& written = f.written();
        ASSERT(written.size() == 2U);
        EXPECT(written[0].id == RequestID(1));
        EXPECT(written[1].id == RequestID(2));
    });

    kit.add("responses_in_any_order_complete_their_own_requests", [](Fixture& f) {
        auto ask = [&](std::int64_t a) -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.send_request(AddParams{.a = a, .b = 0}).or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            co_await f.next();
            f.remote.send(response<A>(2, AddResult{.sum = 20}));
            f.remote.send(response<A>(1, AddResult{.sum = 10}));
            f.remote.end_input();
        };

        auto [ran, first, second, scripted] = f.run(f.peer.run(), ask(1), ask(2), remote());
        EXPECT(ran.has_value());
        ASSERT(first.has_value());
        EXPECT(first->sum == 10);
        ASSERT(second.has_value());
        EXPECT(second->sum == 20);
    });

    kit.add("result_that_does_not_decode_fails_the_request", [](Fixture& f) {
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.send_request(AddParams{}).or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(A::response_raw(1, A::not_a_value));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::RequestFailed);
        EXPECT(f.written().size() == 1U);
    });

    kit.add("send_request_with_params_that_do_not_encode_fails", [](Fixture& f) {
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            f.remote.end_input();
            co_return co_await f.peer
                .template send_request<AddResult>("test/unwritable", Unwritable{})
                .or_fail();
        };

        auto [ran, asked] = f.run(f.peer.run(), ask());
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::InternalError);
        EXPECT(f.written().empty());
    });

    kit.add("send_notification_writes_a_notification", [](Fixture& f) {
        auto by_traits = f.peer.send_notification(NoteParams{.text = "by traits"});
        auto by_name = f.peer.send_notification("custom/note", NoteParams{.text = "by name"});
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(by_traits.has_value());
        EXPECT(by_name.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 2U);
        EXPECT(written[0].kind == Message::Kind::Notification);
        EXPECT(written[0].method == "test/note");
        auto first = decoded<NoteParams, A>(written[0].body);
        ASSERT(first.has_value());
        EXPECT(first->text == "by traits");
        EXPECT(written[1].method == "custom/note");
        auto second = decoded<NoteParams, A>(written[1].body);
        ASSERT(second.has_value());
        EXPECT(second->text == "by name");
    });

    kit.add("send_notification_with_params_that_do_not_encode_fails", [](Fixture& f) {
        auto sent = f.peer.send_notification("test/unwritable", Unwritable{});
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        ASSERT(sent.has_error());
        EXPECT(code_of(sent.error()) == ErrorCode::InternalError);
        EXPECT(f.written().empty());
    });

    // Through its context and through the peer alike, a handler sends
    // notifications and requests of its own and answers after their results.
    kit.add("handler_sends_and_awaits_while_handling", [](Fixture& f) {
        f.peer.on_request([&](Context& context,
                              const AddParams& params) -> ipc::RequestResult<AddParams> {
            co_await or_fail(
                context->send_notification("client/note", NoteParams{.text = "context"}));
            co_await or_fail(f.peer.send_notification("client/note", NoteParams{.text = "peer"}));
            auto from_context =
                co_await context->template send_request<AddResult>("client/add", params).or_fail();
            auto from_peer = co_await f.peer
                                 .template send_request<AddResult>("client/add",
                                                                   AddParams{.a = params.b, .b = 1})
                                 .or_fail();
            co_return AddResult{.sum = from_context.sum + from_peer.sum};
        });
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(7, "test/add", AddParams{.a = 2, .b = 3}));
            co_await f.next();
            co_await f.next();
            co_await f.next();
            f.remote.send(response<A>(1, AddResult{.sum = 9}));
            co_await f.next();
            f.remote.send(response<A>(2, AddResult{.sum = 4}));
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        EXPECT(ran.has_value());
        EXPECT(scripted.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 5U);
        EXPECT(written[0].kind == Message::Kind::Notification);
        auto context_note = decoded<NoteParams, A>(written[0].body);
        ASSERT(context_note.has_value());
        EXPECT(context_note->text == "context");
        EXPECT(written[1].kind == Message::Kind::Notification);
        auto peer_note = decoded<NoteParams, A>(written[1].body);
        ASSERT(peer_note.has_value());
        EXPECT(peer_note->text == "peer");
        EXPECT(written[2].kind == Message::Kind::Request);
        EXPECT(written[2].id == RequestID(1));
        auto context_params = decoded<AddParams, A>(written[2].body);
        ASSERT(context_params.has_value());
        EXPECT(*context_params == AddParams{.a = 2, .b = 3});
        EXPECT(written[3].kind == Message::Kind::Request);
        EXPECT(written[3].id == RequestID(2));
        auto peer_params = decoded<AddParams, A>(written[3].body);
        ASSERT(peer_params.has_value());
        EXPECT(*peer_params == AddParams{.a = 3, .b = 1});
        EXPECT(written[4].id == RequestID(7));
        EXPECT(sum_of<A>(written[4]) == 13);
    });

    kit.add("handler_whose_own_request_fails_is_answered_with_its_error", [](Fixture& f) {
        f.peer.on_request(
            [&](Context& context, const AddParams& params) -> ipc::RequestResult<AddParams> {
                co_return co_await context->template send_request<AddResult>("client/add", params)
                    .or_fail();
            });
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(7, "test/add", AddParams{.a = 2, .b = 3}));
            co_await f.next();
            f.remote.send(A::response_raw(1, A::not_a_value));
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 2U);
        EXPECT(written[1].kind == Message::Kind::Error);
        EXPECT(written[1].id == RequestID(7));
        EXPECT(code_of(written[1].error) == ErrorCode::RequestFailed);
    });
}

}  // namespace kota::test
