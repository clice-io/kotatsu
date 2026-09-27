#pragma once

// Dispatch: what the peer does with each message it reads, requests and
// notifications to their handlers, and what it answers.

#include <optional>
#include <string>
#include <vector>

#include "ipc/harness/peer_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"
#include "kota/codec/visit/common.h"

namespace kota::test {

template <Wire W>
void peer_dispatch(const PeerKit<W>& kit) {
    using Fixture = PeerFixture<W>;
    using Context = typename Fixture::Context;
    using ipc::protocol::ErrorCode;

    kit.add("request_is_answered_with_its_handler_result", [](Fixture& f) {
        std::string method;
        std::optional<RequestID> id;
        f.peer.on_request(
            [&](Context& context, const AddParams& params) -> ipc::RequestResult<AddParams> {
                method = std::string(context.method);
                id = context.id;
                co_return AddResult{.sum = params.a + params.b};
            });
        f.remote.send(request<W>(7, "test/add", AddParams{.a = 2, .b = 3}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(method == "test/add");
        EXPECT(id == RequestID(7));
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].id == RequestID(7));
        EXPECT(sum_of<W>(written[0]) == 5);
    });

    if constexpr(W::caps.string_ids) {
        kit.add("request_with_a_string_id_is_answered_with_it", [](Fixture& f) {
            f.serve_add();
            f.remote.send(request<W>("abc", "test/add", AddParams{.a = 2, .b = 3}));
            f.remote.end_input();

            auto [ran] = f.run(f.peer.run());
            EXPECT(ran.has_value());
            const auto& written = f.written();
            ASSERT(written.size() == 1U);
            EXPECT(written[0].id == RequestID("abc"));
            EXPECT(sum_of<W>(written[0]) == 5);
        });
    }

    kit.add("request_by_method_name_reaches_its_handler", [](Fixture& f) {
        std::string method;
        f.peer.on_request(
            "custom/add",
            [&](Context& context, const AddParams& params) -> ipc::RequestResult<AddParams> {
                method = std::string(context.method);
                co_return AddResult{.sum = params.a + params.b};
            });
        f.remote.send(request<W>(2, "custom/add", AddParams{.a = 7, .b = 8}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(method == "custom/add");
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].id == RequestID(2));
        EXPECT(sum_of<W>(written[0]) == 15);
    });

    kit.add("notification_reaches_its_handler", [](Fixture& f) {
        std::vector<std::string> seen;
        f.peer.on_notification([&](const NoteParams& params) { seen.push_back(params.text); });
        f.peer.on_notification("custom/note", [&](const NoteParams& params) {
            seen.push_back("custom:" + params.text);
        });
        f.remote.send(notification<W>("test/note", NoteParams{.text = "by traits"}));
        f.remote.send(notification<W>("custom/note", NoteParams{.text = "by name"}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(seen == std::vector<std::string>{"by traits", "custom:by name"});
        EXPECT(f.written().empty());
    });

    kit.add("tagged_handlers_answer_the_tag_method", [](Fixture& f) {
        std::vector<std::string> notes;
        f.peer.template on_request<TaggedAdd>(
            [](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
                co_return AddResult{.sum = params.a + params.b};
            });
        f.peer.template on_notification<TaggedNote>(
            [&](const NoteParams& params) { notes.push_back(params.text); });
        f.remote.send(request<W>(1, "test/taggedAdd", AddParams{.a = 10, .b = 20}));
        f.remote.send(notification<W>("test/taggedNote", NoteParams{.text = "hello tag"}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(notes == std::vector<std::string>{"hello tag"});
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(sum_of<W>(written[0]) == 30);
    });

    // A handler runs until it first suspends before the peer reads on, so
    // handlers start in the order their messages arrived.
    kit.add("handlers_start_in_arrival_order", [](Fixture& f) {
        std::vector<std::string> order;
        f.peer.on_request([&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            order.emplace_back("request");
            co_return AddResult{.sum = params.a + params.b};
        });
        f.peer.on_notification([&](const NoteParams& params) { order.push_back(params.text); });
        f.remote.send(request<W>(1, "test/add", AddParams{.a = 2, .b = 3}));
        f.remote.send(notification<W>("test/note", NoteParams{.text = "first"}));
        f.remote.send(notification<W>("test/note", NoteParams{.text = "second"}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(order == std::vector<std::string>{"request", "first", "second"});
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(sum_of<W>(written[0]) == 5);
    });

    kit.add("second_request_handler_replaces_the_first", [](Fixture& f) {
        f.serve_add();
        f.peer.on_request([](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            co_return AddResult{.sum = params.a * params.b};
        });
        f.remote.send(request<W>(1, "test/add", AddParams{.a = 2, .b = 3}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(sum_of<W>(written[0]) == 6);
    });

    kit.add("second_notification_handler_replaces_the_first", [](Fixture& f) {
        std::vector<std::string> seen;
        f.peer.on_notification(
            [&](const NoteParams& params) { seen.push_back("first:" + params.text); });
        f.peer.on_notification(
            [&](const NoteParams& params) { seen.push_back("second:" + params.text); });
        f.remote.send(notification<W>("test/note", NoteParams{.text = "x"}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(seen == std::vector<std::string>{"second:x"});
    });

    kit.add("unknown_method_is_answered_with_method_not_found", [](Fixture& f) {
        f.remote.send(request<W>(1, "unknown/method", EmptyParams{}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].kind == Message::Kind::Error);
        EXPECT(written[0].id == RequestID(1));
        EXPECT(code_of(written[0].error) == ErrorCode::MethodNotFound);
        EXPECT(zest::contains(written[0].error.message, "unknown/method"));
    });

    kit.add("unknown_notification_is_ignored", [](Fixture& f) {
        f.remote.send(notification<W>("unknown/note", NoteParams{.text = "hello"}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(f.written().empty());
    });

    kit.add("response_to_no_request_is_ignored", [](Fixture& f) {
        f.remote.send(response<W>(999, AddResult{.sum = 42}));
        f.remote.send(W::error_response(998, ipc::Error("stray")));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(f.written().empty());
    });

    // The first request's handler holds its id until released; a second
    // request with that id meanwhile is refused, and the first still answers.
    kit.add("request_reusing_a_running_id_is_answered_with_invalid_request", [](Fixture& f) {
        int calls = 0;
        event release;
        f.peer.on_request([&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            calls += 1;
            co_await release.wait();
            co_return AddResult{.sum = params.a + params.b};
        });
        auto remote = [&]() -> task<> {
            f.remote.send(request<W>(1, "test/add", AddParams{.a = 1, .b = 2}));
            f.remote.send(request<W>(1, "test/add", AddParams{.a = 3, .b = 4}));
            co_await f.next();
            release.set();
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        EXPECT(ran.has_value());
        EXPECT(scripted.has_value());
        EXPECT(calls == 1);
        const auto& written = f.written();
        ASSERT(written.size() == 2U);
        EXPECT(written[0].kind == Message::Kind::Error);
        EXPECT(written[0].id == RequestID(1));
        EXPECT(code_of(written[0].error) == ErrorCode::InvalidRequest);
        EXPECT(written[1].id == RequestID(1));
        EXPECT(sum_of<W>(written[1]) == 3);
    });

    kit.add("params_that_do_not_decode_are_answered_with_invalid_params", [](Fixture& f) {
        bool called = false;
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            called = true;
            co_return AddResult{};
        });
        f.remote.send(W::request_raw(11, "test/add", W::not_a_value));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(!called);
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].kind == Message::Kind::Error);
        EXPECT(written[0].id == RequestID(11));
        EXPECT(code_of(written[0].error) == ErrorCode::InvalidParams);
    });

    kit.add("notification_params_that_do_not_decode_are_dropped", [](Fixture& f) {
        bool called = false;
        f.peer.on_notification([&](const NoteParams&) { called = true; });
        f.remote.send(W::notification_raw("test/note", W::not_a_value));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(!called);
        EXPECT(f.written().empty());
    });

    kit.add("handler_error_is_answered_with_its_code_and_message", [](Fixture& f) {
        f.peer.on_request([](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            co_await fail(ErrorCode::InvalidParams, "forced invalid params");
        });
        f.remote.send(request<W>(10, "test/add", AddParams{}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].kind == Message::Kind::Error);
        EXPECT(written[0].id == RequestID(10));
        EXPECT(code_of(written[0].error) == ErrorCode::InvalidParams);
        EXPECT(written[0].error.message == "forced invalid params");
    });

    kit.add("result_that_does_not_encode_is_answered_with_internal_error", [](Fixture& f) {
        f.peer.on_request("test/unwritable",
                          [](Context&, const EmptyParams&) -> task<Unwritable, ipc::Error> {
                              co_return Unwritable{};
                          });
        f.remote.send(request<W>(4, "test/unwritable", EmptyParams{}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].kind == Message::Kind::Error);
        EXPECT(written[0].id == RequestID(4));
        EXPECT(code_of(written[0].error) == ErrorCode::InternalError);
    });

    // Where the reply goes is the case below, which waits for P1.2.
    kit.add("unparsable_message_is_answered_with_a_parse_error", [](Fixture& f) {
        f.remote.send(std::string(W::garbage));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].kind == Message::Kind::Error);
        EXPECT(code_of(written[0].error) == ErrorCode::ParseError);
    });
}

/// The reply to a message that does not parse answers no request, so it
/// carries no id: a null one in JSON-RPC.
template <Wire W>
void unparsable_message_is_answered_without_an_id() {
    PeerFixture<W> f;
    f.remote.send(std::string(W::garbage));
    f.remote.end_input();

    auto [ran] = f.run(f.peer.run());
    EXPECT(ran.has_value());
    const auto& written = f.written();
    ASSERT(written.size() == 1U);
    EXPECT(written[0].kind == Message::Kind::Error);
    EXPECT(!written[0].id.has_value());
}

/// A request without params for a handler whose params have fields is
/// refused, rather than handled with made-up params.
template <Wire W>
void request_without_params_is_answered_with_invalid_params() {
    PeerFixture<W> f;
    bool called = false;
    f.peer.on_request(
        [&](typename PeerFixture<W>::Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            called = true;
            co_return AddResult{};
        });
    f.remote.send(W::request_raw(1, "test/add", ""));
    f.remote.end_input();

    auto [ran] = f.run(f.peer.run());
    EXPECT(ran.has_value());
    EXPECT(!called);
    const auto& written = f.written();
    ASSERT(written.size() == 1U);
    EXPECT(written[0].kind == Message::Kind::Error);
    EXPECT(code_of(written[0].error) == ipc::protocol::ErrorCode::InvalidParams);
}

/// An error response that answers no request is not answered in turn:
/// two peers would otherwise trade such errors for good.
template <Wire W>
void error_response_without_an_id_is_not_answered() {
    PeerFixture<W> f;
    f.remote.send(
        W::error_response(std::nullopt, ipc::Error(ipc::protocol::ErrorCode::ParseError, "bad")));
    f.remote.end_input();

    auto [ran] = f.run(f.peer.run());
    EXPECT(ran.has_value());
    EXPECT(f.written().empty());
}

/// A handler that returns a RawValue has written its result itself: the
/// requester gets it as it is.
template <Wire W>
void raw_value_result_is_sent_as_it_is() {
    PeerFixture<W> f;
    f.peer.on_request([](typename PeerFixture<W>::Context&,
                         const AddParams& params) -> task<codec::RawValue, ipc::Error> {
        co_return codec::RawValue{W::encode(AddResult{.sum = params.a + params.b})};
    });
    f.remote.send(request<W>(1, "test/add", AddParams{.a = 10, .b = 20}));
    f.remote.end_input();

    auto [ran] = f.run(f.peer.run());
    EXPECT(ran.has_value());
    const auto& written = f.written();
    ASSERT(written.size() == 1U);
    EXPECT(sum_of<W>(written[0]) == 30);
}

}  // namespace kota::test
