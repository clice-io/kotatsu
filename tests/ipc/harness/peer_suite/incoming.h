#pragma once

// The requests read and not answered yet, as incoming_requests() lists them:
// from their dispatch until their answer is queued.

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "ipc/harness/peer_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::test {

/// Requests as (method, id), in that order.
using Listed = std::vector<std::pair<std::string, RequestID>>;

/// What `peer` lists as read and not answered yet.
template <typename PeerT>
Listed listed_by(const PeerT& peer) {
    Listed requests;
    for(auto request: peer.incoming_requests()) {
        requests.emplace_back(std::string(request.method), request.id);
    }
    std::ranges::sort(requests);
    return requests;
}

template <CodecAdapter A>
void peer_incoming(const PeerKit<A>& kit) {
    using Fixture = PeerFixture<A>;
    using Context = typename Fixture::Context;
    using ipc::protocol::CancelRequestParams;
    using ipc::protocol::ErrorCode;

    kit.add("handler_finds_its_own_request_listed_in_both_steps", [](Fixture& f) {
        Listed at_call;
        Listed in_task;
        auto answer = [&](const AddParams& params) -> ipc::RequestResult<AddParams> {
            in_task = listed_by(f.peer);
            co_return AddResult{.sum = params.a + params.b};
        };
        f.peer.on_request([&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            at_call = listed_by(f.peer);
            return answer(params);
        });
        f.remote.send(request<A>(7, "test/add", AddParams{.a = 2, .b = 3}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        ZEXPECT(ran.has_value());
        ZEXPECT(at_call == Listed{
                               {"test/add", RequestID(7)}
        });
        ZEXPECT(in_task == Listed{
                               {"test/add", RequestID(7)}
        });
        ZEXPECT(f.peer.incoming_requests().empty());
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(sum_of<A>(written[0]) == 5);
    });

    // Each handler waits for its release; the remote reads the list before
    // and between the answers.
    kit.add("requests_are_listed_until_their_answers_are_queued", [](Fixture& f) {
        event both_started;
        event release_add;
        event release_custom;
        int started = 0;
        auto start = [&] {
            started += 1;
            if(started == 2) {
                both_started.set();
            }
        };
        f.peer.on_request([&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            start();
            co_await release_add.wait();
            co_return AddResult{.sum = params.a + params.b};
        });
        f.peer.on_request("custom/add",
                          [&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
                              start();
                              co_await release_custom.wait();
                              co_return AddResult{.sum = params.a + params.b};
                          });
        std::size_t count = 0;
        Listed both;
        Listed after_one;
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(1, "test/add", AddParams{.a = 1, .b = 2}));
            f.remote.send(request<A>(2, "custom/add", AddParams{.a = 3, .b = 4}));
            co_await both_started.wait();
            count = f.peer.incoming_requests().size();
            both = listed_by(f.peer);
            release_add.set();
            co_await f.next();
            after_one = listed_by(f.peer);
            release_custom.set();
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(scripted.has_value());
        ZEXPECT(count == 2U);
        ZEXPECT(both == Listed{
                            {"custom/add", RequestID(2)},
                            {"test/add",   RequestID(1)}
        });
        ZEXPECT(after_one == Listed{
                                 {"custom/add", RequestID(2)}
        });
        ZEXPECT(f.peer.incoming_requests().empty());
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(written[0].id == RequestID(1));
        ZEXPECT(written[1].id == RequestID(2));
    });

    if constexpr(A::caps.string_ids) {
        kit.add("request_with_a_string_id_is_listed_with_it", [](Fixture& f) {
            Listed at_call;
            f.peer.on_request(
                [&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
                    at_call = listed_by(f.peer);
                    co_return AddResult{.sum = params.a + params.b};
                });
            f.remote.send(request<A>("abc", "test/add", AddParams{.a = 2, .b = 3}));
            f.remote.end_input();

            auto [ran] = f.run(f.peer.run());
            ZEXPECT(ran.has_value());
            ZEXPECT(at_call == Listed{
                                   {"test/add", RequestID("abc")}
            });
            ZEXPECT(f.peer.incoming_requests().empty());
        });
    }

    // The duplicate of request 1 names another handler's method: listed, it
    // would show.
    kit.add("request_answered_at_once_with_an_error_is_never_listed", [](Fixture& f) {
        event started;
        event never;
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            started.set();
            co_await never.wait();
            co_return AddResult{};
        });
        f.peer.on_request("test/ping",
                          [](Context&, const EmptyParams&) -> ipc::RequestResult<AddParams> {
                              co_return AddResult{};
                          });
        Listed after_errors;
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(1, "test/add", AddParams{}));
            co_await started.wait();
            f.remote.send(request<A>(1, "test/ping", EmptyParams{}));
            f.remote.send(request<A>(2, "test/unknown", EmptyParams{}));
            co_await f.next();
            co_await f.next();
            after_errors = listed_by(f.peer);
            f.remote.send(notification<A>("$/cancelRequest", CancelRequestParams{.id = 1}));
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(scripted.has_value());
        ZEXPECT(after_errors == Listed{
                                    {"test/add", RequestID(1)}
        });
        const auto& written = f.written();
        ZASSERT(written.size() == 3U);
        ZEXPECT(code_of(written[0].error) == ErrorCode::InvalidRequest);
        ZEXPECT(code_of(written[1].error) == ErrorCode::MethodNotFound);
        ZEXPECT(code_of(written[2].error) == ErrorCode::RequestCancelled);
    });

    // The handler waits for a request of its own, which the cancel passes on
    // to: it ends once the remote has answered that request too.
    kit.add("cancelled_request_stays_listed_until_its_handler_ends", [](Fixture& f) {
        f.peer.on_request(
            [&](Context& context, const AddParams& params) -> ipc::RequestResult<AddParams> {
                co_return co_await context
                    ->template send_request<AddResult>("client/add",
                                                       params,
                                                       {.token = context.cancellation})
                    .or_fail();
            });
        Listed after_cancel;
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(31, "test/add", AddParams{.a = 4, .b = 5}));
            co_await f.next();
            f.remote.send(notification<A>("$/cancelRequest", CancelRequestParams{.id = 31}));
            co_await f.next();
            after_cancel = listed_by(f.peer);
            f.remote.send(A::error_response(
                1,
                ipc::Error(ErrorCode::RequestCancelled, "cancelled by the remote")));
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(scripted.has_value());
        ZEXPECT(after_cancel == Listed{
                                    {"test/add", RequestID(31)}
        });
        ZEXPECT(f.peer.incoming_requests().empty());
        const auto& written = f.written();
        ZASSERT(written.size() == 3U);
        ZEXPECT(written[1].method == "$/cancelRequest");
        ZEXPECT(written[2].id == RequestID(31));
        ZEXPECT(code_of(written[2].error) == ErrorCode::RequestCancelled);
    });

    kit.add("close_empties_the_list", [](Fixture& f) {
        event started;
        event never;
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            started.set();
            co_await never.wait();
            co_return AddResult{};
        });
        Listed before_close;
        bool empty_after_close = false;
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(1, "test/add", AddParams{}));
            co_await started.wait();
            before_close = listed_by(f.peer);
            f.peer.close();
            empty_after_close = f.peer.incoming_requests().empty();
        };

        auto [ran, scripted] = f.run(f.peer.run(), remote());
        ZEXPECT(ran.has_value());
        ZEXPECT(scripted.has_value());
        ZEXPECT(before_close == Listed{
                                    {"test/add", RequestID(1)}
        });
        ZEXPECT(empty_after_close);
        ZEXPECT(f.written().empty());
    });
}

}  // namespace kota::test
