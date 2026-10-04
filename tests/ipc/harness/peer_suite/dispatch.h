#pragma once

// Dispatch: what the peer does with each message it reads, requests and
// notifications to their handlers, and what it answers.

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ipc/harness/peer_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"
#include "kota/codec/visit/common.h"

namespace kota::test {

template <CodecAdapter A>
void peer_dispatch(const PeerKit<A>& kit) {
    using Fixture = PeerFixture<A>;
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
        f.remote.send(request<A>(7, "test/add", AddParams{.a = 2, .b = 3}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(method == "test/add");
        ZEXPECT(id == RequestID(7));
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(written[0].id == RequestID(7));
        ZEXPECT(sum_of<A>(written[0]) == 5);
    });

    if constexpr(A::caps.string_ids) {
        kit.add("request_with_a_string_id_is_answered_with_it", [](Fixture& f) {
            f.serve_add();
            f.remote.send(request<A>("abc", "test/add", AddParams{.a = 2, .b = 3}));
            f.remote.end_input();

            auto [ran] = f.run(f.peer.run());
            ZEXPECT(ran.has_value());
            const auto& written = f.written();
            ZASSERT(written.size() == 1U);
            ZEXPECT(written[0].id == RequestID("abc"));
            ZEXPECT(sum_of<A>(written[0]) == 5);
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
        f.remote.send(request<A>(2, "custom/add", AddParams{.a = 7, .b = 8}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(method == "custom/add");
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(written[0].id == RequestID(2));
        ZEXPECT(sum_of<A>(written[0]) == 15);
    });

    kit.add("notification_reaches_its_handler", [](Fixture& f) {
        std::vector<std::string> seen;
        f.peer.on_notification([&](const NoteParams& params) { seen.push_back(params.text); });
        f.peer.on_notification("custom/note", [&](const NoteParams& params) {
            seen.push_back("custom:" + params.text);
        });
        f.remote.send(notification<A>("test/note", NoteParams{.text = "by traits"}));
        f.remote.send(notification<A>("custom/note", NoteParams{.text = "by name"}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(seen == std::vector<std::string>{"by traits", "custom:by name"});
        ZEXPECT(f.written().empty());
    });

    // The handler is called before the notifications read with its request
    // are dispatched, and the task it returns starts after them.
    kit.add("handler_is_called_at_dispatch_and_its_task_after_the_messages_read_with_it",
            [](Fixture& f) {
                std::vector<std::string> order;
                auto answer = [&](const AddParams& params) -> ipc::RequestResult<AddParams> {
                    order.emplace_back("task");
                    co_return AddResult{.sum = params.a + params.b};
                };
                f.peer.on_request(
                    [&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
                        order.emplace_back("handler");
                        return answer(params);
                    });
                f.peer.on_notification(
                    [&](const NoteParams& params) { order.push_back(params.text); });
                f.remote.send(request<A>(1, "test/add", AddParams{.a = 2, .b = 3}));
                f.remote.send(notification<A>("test/note", NoteParams{.text = "first"}));
                f.remote.send(notification<A>("test/note", NoteParams{.text = "second"}));
                f.remote.end_input();

                auto [ran] = f.run(f.peer.run());
                ZEXPECT(ran.has_value());
                ZEXPECT(order == std::vector<std::string>{"handler", "first", "second", "task"});
                const auto& written = f.written();
                ZASSERT(written.size() == 1U);
                ZEXPECT(sum_of<A>(written[0]) == 5);
            });

    // A coroutine handler does nothing as it is called: all of it runs after
    // the notifications read with its request.
    kit.add("coroutine_handler_runs_after_the_messages_read_with_its_request", [](Fixture& f) {
        std::vector<std::string> order;
        f.peer.on_request([&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            order.emplace_back("request");
            co_return AddResult{.sum = params.a + params.b};
        });
        f.peer.on_notification([&](const NoteParams& params) { order.push_back(params.text); });
        f.remote.send(request<A>(1, "test/add", AddParams{.a = 2, .b = 3}));
        f.remote.send(notification<A>("test/note", NoteParams{.text = "first"}));
        f.remote.send(notification<A>("test/note", NoteParams{.text = "second"}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(order == std::vector<std::string>{"first", "second", "request"});
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(sum_of<A>(written[0]) == 5);
    });

    // Each task records its request as it starts: they start in the order
    // the requests were read.
    kit.add("tasks_start_in_the_order_their_requests_were_read", [](Fixture& f) {
        std::vector<std::int64_t> order;
        f.peer.on_request([&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            order.push_back(params.a);
            co_return AddResult{.sum = params.a};
        });
        for(std::int64_t a = 1; a <= 3; ++a) {
            f.remote.send(request<A>(a, "test/add", AddParams{.a = a}));
        }
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(order == std::vector<std::int64_t>{1, 2, 3});
        ZEXPECT(f.written().size() == 3U);
    });

    // The handler wrote its result itself; the requester gets it as it is.
    kit.add("raw_value_result_is_sent_as_it_is", [](Fixture& f) {
        f.peer.on_request(
            [](Context&, const AddParams& params) -> task<codec::RawValue, ipc::Error> {
                co_return codec::RawValue{A::encode(AddResult{.sum = params.a + params.b})};
            });
        f.remote.send(request<A>(1, "test/add", AddParams{.a = 10, .b = 20}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(sum_of<A>(written[0]) == 30);
    });

    kit.add("second_request_handler_replaces_the_first", [](Fixture& f) {
        f.serve_add();
        f.peer.on_request([](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            co_return AddResult{.sum = params.a * params.b};
        });
        f.remote.send(request<A>(1, "test/add", AddParams{.a = 2, .b = 3}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(sum_of<A>(written[0]) == 6);
    });

    kit.add("second_notification_handler_replaces_the_first", [](Fixture& f) {
        std::vector<std::string> seen;
        f.peer.on_notification(
            [&](const NoteParams& params) { seen.push_back("first:" + params.text); });
        f.peer.on_notification(
            [&](const NoteParams& params) { seen.push_back("second:" + params.text); });
        f.remote.send(notification<A>("test/note", NoteParams{.text = "x"}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(seen == std::vector<std::string>{"second:x"});
    });

    kit.add("unknown_method_is_answered_with_method_not_found", [](Fixture& f) {
        f.remote.send(request<A>(1, "unknown/method", EmptyParams{}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(written[0].kind == Message::Kind::Error);
        ZEXPECT(written[0].id == RequestID(1));
        ZEXPECT(code_of(written[0].error) == ErrorCode::MethodNotFound);
        ZEXPECT(zest::contains(written[0].error.message, "unknown/method"));
    });

    kit.add("unknown_notification_is_ignored", [](Fixture& f) {
        f.remote.send(notification<A>("unknown/note", NoteParams{.text = "hello"}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(f.written().empty());
    });

    kit.add("response_to_no_request_is_ignored", [](Fixture& f) {
        f.remote.send(response<A>(999, AddResult{.sum = 42}));
        f.remote.send(A::error_response(998, ipc::Error("stray")));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(f.written().empty());
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
            f.remote.send(request<A>(1, "test/add", AddParams{.a = 1, .b = 2}));
            f.remote.send(request<A>(1, "test/add", AddParams{.a = 3, .b = 4}));
            co_await f.next();
            release.set();
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(scripted.has_value());
        ZEXPECT(calls == 1);
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(written[0].kind == Message::Kind::Error);
        ZEXPECT(written[0].id == RequestID(1));
        ZEXPECT(code_of(written[0].error) == ErrorCode::InvalidRequest);
        ZEXPECT(written[1].id == RequestID(1));
        ZEXPECT(sum_of<A>(written[1]) == 3);
    });

    kit.add("params_that_do_not_decode_are_answered_with_invalid_params", [](Fixture& f) {
        bool called = false;
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            called = true;
            co_return AddResult{};
        });
        f.remote.send(A::request_raw(11, "test/add", A::not_a_value));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(!called);
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(written[0].kind == Message::Kind::Error);
        ZEXPECT(written[0].id == RequestID(11));
        ZEXPECT(code_of(written[0].error) == ErrorCode::InvalidParams);
    });

    // A handler whose params have fields gets no made-up params.
    kit.add("request_without_params_is_answered_with_invalid_params", [](Fixture& f) {
        bool called = false;
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            called = true;
            co_return AddResult{};
        });
        f.remote.send(A::request_raw(1, "test/add", ""));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(!called);
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(written[0].kind == Message::Kind::Error);
        ZEXPECT(code_of(written[0].error) == ErrorCode::InvalidParams);
    });

    kit.add("notification_params_that_do_not_decode_are_dropped", [](Fixture& f) {
        bool called = false;
        f.peer.on_notification([&](const NoteParams&) { called = true; });
        f.remote.send(A::notification_raw("test/note", A::not_a_value));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(!called);
        ZEXPECT(f.written().empty());
    });

    kit.add("handler_error_is_answered_with_its_code_and_message", [](Fixture& f) {
        f.peer.on_request([](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            co_await fail(ErrorCode::InvalidParams, "forced invalid params");
        });
        f.remote.send(request<A>(10, "test/add", AddParams{}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(written[0].kind == Message::Kind::Error);
        ZEXPECT(written[0].id == RequestID(10));
        ZEXPECT(code_of(written[0].error) == ErrorCode::InvalidParams);
        ZEXPECT(written[0].error.message == "forced invalid params");
    });

    kit.add("result_that_does_not_encode_is_answered_with_internal_error", [](Fixture& f) {
        f.peer.on_request("test/unwritable",
                          [](Context&, const EmptyParams&) -> task<Unwritable, ipc::Error> {
                              co_return Unwritable{};
                          });
        f.remote.send(request<A>(4, "test/unwritable", EmptyParams{}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(written[0].kind == Message::Kind::Error);
        ZEXPECT(written[0].id == RequestID(4));
        ZEXPECT(code_of(written[0].error) == ErrorCode::InternalError);
    });

    // The reply answers no request, so it carries no id: a null one in
    // JSON-RPC.
    kit.add("unparsable_message_is_answered_with_a_parse_error_without_an_id", [](Fixture& f) {
        f.remote.send(std::string(A::garbage));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(written[0].kind == Message::Kind::Error);
        ZEXPECT(code_of(written[0].error) == ErrorCode::ParseError);
        ZEXPECT(!written[0].id.has_value());
    });

    // Two peers would otherwise trade such errors for good.
    kit.add("error_response_without_an_id_is_not_answered", [](Fixture& f) {
        f.remote.send(A::error_response(std::nullopt, ipc::Error(ErrorCode::ParseError, "bad")));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(f.written().empty());
    });

#if KOTA_ENABLE_EXCEPTIONS
    // Request 1's handler throws while request 2's is running; both are
    // answered, and so is request 3, read after the throw.
    kit.add("throwing_request_handler_is_answered_with_internal_error", [](Fixture& f) {
        event release;
        f.peer.on_request([&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            if(params.a == 1) {
                throw std::runtime_error("boom");
            }
            if(params.a == 2) {
                co_await release.wait();
            }
            co_return AddResult{.sum = params.a + params.b};
        });
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(2, "test/add", AddParams{.a = 2, .b = 0}));
            f.remote.send(request<A>(1, "test/add", AddParams{.a = 1, .b = 0}));
            co_await f.next();
            release.set();
            co_await f.next();
            f.remote.send(request<A>(3, "test/add", AddParams{.a = 3, .b = 0}));
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(scripted.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 3U);
        ZEXPECT(written[0].id == RequestID(1));
        ZEXPECT(written[0].kind == Message::Kind::Error);
        ZEXPECT(code_of(written[0].error) == ErrorCode::InternalError);
        ZEXPECT(written[1].id == RequestID(2));
        ZEXPECT(sum_of<A>(written[1]) == 2);
        ZEXPECT(written[2].id == RequestID(3));
        ZEXPECT(sum_of<A>(written[2]) == 3);
    });

    // The test/add handler throws as it is called, before it has a task to
    // return; the peer reads on and answers test/ping.
    kit.add("handler_throwing_as_it_is_called_is_answered_with_internal_error", [](Fixture& f) {
        f.peer.on_request([](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            throw std::runtime_error("boom");
        });
        f.peer.on_request("test/ping",
                          [](Context&, const EmptyParams&) -> ipc::RequestResult<AddParams> {
                              co_return AddResult{.sum = 1};
                          });
        f.remote.send(request<A>(1, "test/add", AddParams{}));
        f.remote.send(request<A>(2, "test/ping", EmptyParams{}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(written[0].id == RequestID(1));
        ZEXPECT(written[0].kind == Message::Kind::Error);
        ZEXPECT(code_of(written[0].error) == ErrorCode::InternalError);
        ZEXPECT(written[1].id == RequestID(2));
        ZEXPECT(sum_of<A>(written[1]) == 1);
    });

    // The notification's handler throws while a request's is running.
    kit.add("throwing_notification_handler_leaves_the_peer_serving", [](Fixture& f) {
        event started;
        event release;
        f.peer.on_request([&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            started.set();
            co_await release.wait();
            co_return AddResult{.sum = params.a + params.b};
        });
        f.peer.on_notification([](const NoteParams&) { throw std::runtime_error("boom"); });
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(1, "test/add", AddParams{.a = 1, .b = 2}));
            co_await started.wait();
            f.remote.send(notification<A>("test/note", NoteParams{.text = "throw"}));
            release.set();
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(scripted.has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(written[0].id == RequestID(1));
        ZEXPECT(sum_of<A>(written[0]) == 3);
    });

    // With nothing else running, a throwing notification handler does not end
    // run(), and the peer's own request is still answered.
    kit.add("throwing_notification_handler_does_not_end_run", [](Fixture& f) {
        f.peer.on_notification([](const NoteParams&) { throw std::runtime_error("boom"); });
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.peer
                .template send_request<AddResult>("worker/build", AddParams{.a = 2, .b = 3})
                .or_fail();
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(notification<A>("test/note", NoteParams{.text = "throw"}));
            f.remote.send(response<A>(1, AddResult{.sum = 5}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(scripted.has_value());
        ZASSERT(asked.has_value());
        ZEXPECT(asked->sum == 5);
    });
#endif
}

}  // namespace kota::test
