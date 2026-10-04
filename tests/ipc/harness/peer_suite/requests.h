#pragma once

// Requests and notifications the peer sends: what it writes, and what
// send_request returns for each way the remote answers.

#include <string>
#include <utility>
#include <vector>

#include "ipc/harness/peer_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"
#include "kota/codec/visit/common.h"

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
        ZEXPECT(ran.has_value());
        ZEXPECT(scripted.has_value());
        ZASSERT(asked.has_value());
        ZEXPECT(asked->sum == 9);
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(written[0].kind == Message::Kind::Request);
        ZEXPECT(written[0].id == RequestID(1));
        ZEXPECT(written[0].method == "worker/build");
        auto params = decoded<AddParams, A>(written[0].body);
        ZASSERT(params.has_value());
        ZEXPECT(*params == AddParams{.a = 2, .b = 3});
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
        ZEXPECT(ran.has_value());
        ZASSERT(asked.has_value());
        ZEXPECT(asked->sum == 2);
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(written[0].method == "test/add");
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
        ZEXPECT(ran.has_value());
        ZASSERT(asked.has_error());
        ZEXPECT(asked.error().code == -32001);
        ZEXPECT(asked.error().message == "remote failed");
    });

    // A remote's error made without a code is RequestFailed, which the
    // local ConnectionClosed stays apart from.
    kit.add("send_request_tells_a_remote_error_from_a_closed_connection", [](Fixture& f) {
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer.send_request(AddParams{}).or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(A::error_response(1, ipc::Error("remote failed")));
            f.remote.end_input();
        };
        auto [ran, answered, scripted] = f.run(f.peer.run(), ask(), remote());
        auto [after] = f.run(ask());
        ZEXPECT(ran.has_value());
        ZASSERT(answered.has_error());
        ZEXPECT(code_of(answered.error()) == ErrorCode::RequestFailed);
        ZASSERT(after.has_error());
        ZEXPECT(code_of(after.error()) == ErrorCode::ConnectionClosed);
    });

    // An answer resumes its requester once what was read with it is
    // dispatched, never inside the read loop.
    kit.add("answer_resumes_its_requester_after_the_messages_read_with_it", [](Fixture& f) {
        std::vector<std::string> order;
        f.peer.on_notification([&](const NoteParams& params) { order.push_back(params.text); });
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            auto result = co_await f.peer.send_request(AddParams{}).or_fail();
            order.emplace_back("answered");
            co_return result;
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(response<A>(1, AddResult{.sum = 1}));
            f.remote.send(notification<A>("test/note", NoteParams{.text = "note"}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(asked.has_value());
        ZEXPECT(order == std::vector<std::string>{"note", "answered"});
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
        ZEXPECT(ran.has_value());
        ZASSERT(asked.has_value());
        ZEXPECT(asked->first.sum == 1);
        ZEXPECT(asked->second.sum == 2);
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(written[0].id == RequestID(1));
        ZEXPECT(written[1].id == RequestID(2));
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
        ZEXPECT(ran.has_value());
        ZASSERT(first.has_value());
        ZEXPECT(first->sum == 10);
        ZASSERT(second.has_value());
        ZEXPECT(second->sum == 20);
    });

    // A RawValue result is the result as the codec wrote it.
    kit.add("raw_value_result_is_read_as_it_is", [](Fixture& f) {
        auto ask = [&]() -> task<codec::RawValue, ipc::Error> {
            co_return co_await f.peer
                .template send_request<codec::RawValue>("worker/build", AddParams{})
                .or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(response<A>(1, AddResult{.sum = 9}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        ZEXPECT(ran.has_value());
        ZASSERT(asked.has_value());
        ZEXPECT(asked->data == A::encode(AddResult{.sum = 9}));
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
        ZEXPECT(ran.has_value());
        ZASSERT(asked.has_error());
        ZEXPECT(code_of(asked.error()) == ErrorCode::RequestFailed);
        ZEXPECT(f.written().size() == 1U);
    });

    kit.add("send_request_with_params_that_do_not_encode_fails", [](Fixture& f) {
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            f.remote.end_input();
            co_return co_await f.peer
                .template send_request<AddResult>("test/unwritable", Unwritable{})
                .or_fail();
        };

        auto [ran, asked] = f.run(f.peer.run(), ask());
        ZEXPECT(ran.has_value());
        ZASSERT(asked.has_error());
        ZEXPECT(code_of(asked.error()) == ErrorCode::InternalError);
        ZEXPECT(f.written().empty());
    });

    // A method whose traits say it takes no params is sent none.
    kit.add("method_that_takes_no_params_is_sent_none", [](Fixture& f) {
        bool notified = false;
        auto ask = [&]() -> task<std::nullptr_t, ipc::Error> {
            notified = f.peer.send_notification(NoParams{}).has_value();
            co_return co_await f.peer.send_request(NoParams{}).or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            co_await f.next();
            f.remote.send(response<A>(1, nullptr));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(notified);
        ZEXPECT(asked.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(written[0].kind == Message::Kind::Notification);
        ZEXPECT(written[0].method == "test/none");
        ZEXPECT(written[0].body.empty());
        ZEXPECT(written[1].kind == Message::Kind::Request);
        ZEXPECT(written[1].method == "test/none");
        ZEXPECT(written[1].body.empty());
    });

    kit.add("send_notification_writes_a_notification", [](Fixture& f) {
        auto by_traits = f.peer.send_notification(NoteParams{.text = "by traits"});
        auto by_name = f.peer.send_notification("custom/note", NoteParams{.text = "by name"});
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(by_traits.has_value());
        ZEXPECT(by_name.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(written[0].kind == Message::Kind::Notification);
        ZEXPECT(written[0].method == "test/note");
        auto first = decoded<NoteParams, A>(written[0].body);
        ZASSERT(first.has_value());
        ZEXPECT(first->text == "by traits");
        ZEXPECT(written[1].method == "custom/note");
        auto second = decoded<NoteParams, A>(written[1].body);
        ZASSERT(second.has_value());
        ZEXPECT(second->text == "by name");
    });

    kit.add("send_notification_with_params_that_do_not_encode_fails", [](Fixture& f) {
        auto sent = f.peer.send_notification("test/unwritable", Unwritable{});
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZASSERT(sent.has_error());
        ZEXPECT(code_of(sent.error()) == ErrorCode::InternalError);
        ZEXPECT(f.written().empty());
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
        ZEXPECT(ran.has_value());
        ZEXPECT(scripted.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 5U);
        ZEXPECT(written[0].kind == Message::Kind::Notification);
        auto context_note = decoded<NoteParams, A>(written[0].body);
        ZASSERT(context_note.has_value());
        ZEXPECT(context_note->text == "context");
        ZEXPECT(written[1].kind == Message::Kind::Notification);
        auto peer_note = decoded<NoteParams, A>(written[1].body);
        ZASSERT(peer_note.has_value());
        ZEXPECT(peer_note->text == "peer");
        ZEXPECT(written[2].kind == Message::Kind::Request);
        ZEXPECT(written[2].id == RequestID(1));
        auto context_params = decoded<AddParams, A>(written[2].body);
        ZASSERT(context_params.has_value());
        ZEXPECT(*context_params == AddParams{.a = 2, .b = 3});
        ZEXPECT(written[3].kind == Message::Kind::Request);
        ZEXPECT(written[3].id == RequestID(2));
        auto peer_params = decoded<AddParams, A>(written[3].body);
        ZASSERT(peer_params.has_value());
        ZEXPECT(*peer_params == AddParams{.a = 3, .b = 1});
        ZEXPECT(written[4].id == RequestID(7));
        ZEXPECT(sum_of<A>(written[4]) == 13);
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
        ZEXPECT(ran.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(written[1].kind == Message::Kind::Error);
        ZEXPECT(written[1].id == RequestID(7));
        ZEXPECT(code_of(written[1].error) == ErrorCode::RequestFailed);
    });
}

}  // namespace kota::test
