#pragma once

// The requests read and not answered yet, as incoming_requests() lists them:
// from their dispatch until their answer is queued.

#include <algorithm>
#include <vector>

#include "ipc/harness/peer_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::test {

using Listed = std::vector<ipc::UnansweredRequest>;

/// What `peer` lists as read and not answered yet, by id.
template <typename PeerT>
Listed listed_by(const PeerT& peer) {
    auto requests = peer.incoming_requests();
    std::ranges::sort(requests, {}, &ipc::UnansweredRequest::id);
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
        const Listed expected = {
            {.method = "test/add", .id = 7}
        };
        ZEXPECT(at_call == expected);
        ZEXPECT(in_task == expected);
        ZEXPECT(f.peer.incoming_requests().empty());
        const auto& written = f.written();
        ZASSERT(written.size() == 1U);
        ZEXPECT(sum_of<A>(written[0]) == 5);
    });

    // Each handler waits for its release; the remote reads the list before
    // and between the answers.
    kit.add("each_request_is_listed_until_it_is_answered", [](Fixture& f) {
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
        Listed both;
        Listed after_one;
        auto remote = [&]() -> task<> {
            f.remote.send(request<A>(1, "test/add", AddParams{.a = 1, .b = 2}));
            f.remote.send(request<A>(2, "custom/add", AddParams{.a = 3, .b = 4}));
            co_await both_started.wait();
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
        const Listed expected_both = {
            {.method = "test/add",   .id = 1},
            {.method = "custom/add", .id = 2},
        };
        const Listed expected_after_one = {
            {.method = "custom/add", .id = 2}
        };
        ZEXPECT(both == expected_both);
        ZEXPECT(after_one == expected_after_one);
        ZEXPECT(f.peer.incoming_requests().empty());
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(written[0].id == RequestID(1));
        ZEXPECT(written[1].id == RequestID(2));
    });

    // The remote waits for the list to empty, as a request that waits for
    // the peer to go quiet would, then has the peer send a notification: the
    // answer comes first.
    kit.add("request_off_the_list_is_answered_ahead_of_what_is_sent_after", [](Fixture& f) {
        event started;
        event release;
        f.peer.on_request([&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            started.set();
            co_await release.wait();
            co_return AddResult{.sum = params.a + params.b};
        });
        auto remote = [&]() -> task<ipc::Result<void>> {
            f.remote.send(request<A>(1, "test/add", AddParams{.a = 1, .b = 2}));
            co_await started.wait();
            release.set();
            while(!f.peer.incoming_requests().empty()) {
                co_await yield(f.loop);
            }
            auto sent = f.peer.send_notification(NoteParams{.text = "after"});
            co_await f.next();
            co_await f.next();
            f.remote.end_input();
            co_return sent;
        };

        auto [ran, sent] = f.run(f.peer.run(), remote());
        ZEXPECT(ran.has_value());
        ZASSERT(sent.has_value());
        ZEXPECT(sent->has_value());
        const auto& written = f.written();
        ZASSERT(written.size() == 2U);
        ZEXPECT(sum_of<A>(written[0]) == 3);
        ZEXPECT(written[1].method == "test/note");
    });

    if constexpr(A::caps.string_ids) {
        kit.add("request_with_a_string_id_is_listed_with_it", [](Fixture& f) {
            Listed listed;
            f.peer.on_request(
                [&](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
                    listed = listed_by(f.peer);
                    co_return AddResult{.sum = params.a + params.b};
                });
            f.remote.send(request<A>("abc", "test/add", AddParams{.a = 2, .b = 3}));
            f.remote.end_input();

            auto [ran] = f.run(f.peer.run());
            ZEXPECT(ran.has_value());
            const Listed expected = {
                {.method = "test/add", .id = "abc"}
            };
            ZEXPECT(listed == expected);
            ZEXPECT(f.peer.incoming_requests().empty());
        });
    }

    // The duplicate of request 1 names another method, so that it would show
    // if it took the first one's place in the list.
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
        const Listed expected = {
            {.method = "test/add", .id = 1}
        };
        ZEXPECT(after_errors == expected);
        const auto& written = f.written();
        ZASSERT(written.size() == 3U);
        ZEXPECT(code_of(written[0].error) == ErrorCode::InvalidRequest);
        ZEXPECT(code_of(written[1].error) == ErrorCode::MethodNotFound);
        ZEXPECT(code_of(written[2].error) == ErrorCode::RequestCancelled);
    });

    // The handler passes the cancel on to a request of its own, so it ends
    // only once the remote answers that one.
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
        const Listed expected = {
            {.method = "test/add", .id = 31}
        };
        ZEXPECT(after_cancel == expected);
        ZEXPECT(f.peer.incoming_requests().empty());
        const auto& written = f.written();
        ZASSERT(written.size() == 3U);
        ZEXPECT(written[1].method == "$/cancelRequest");
        ZEXPECT(written[2].id == RequestID(31));
        ZEXPECT(code_of(written[2].error) == ErrorCode::RequestCancelled);
    });

    kit.add("close_empties_incoming_requests", [](Fixture& f) {
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
        const Listed expected = {
            {.method = "test/add", .id = 1}
        };
        ZEXPECT(before_close == expected);
        ZEXPECT(empty_after_close);
        ZEXPECT(f.written().empty());
    });

    // The peer's own request fails to be written, which fails the output; the
    // asker resumes once the peer is done with the failure.
    kit.add("write_failure_empties_incoming_requests", [](Fixture& f) {
        event started;
        event never;
        f.peer.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            started.set();
            co_await never.wait();
            co_return AddResult{};
        });
        f.remote.fail_writes();
        f.remote.send(request<A>(1, "test/add", AddParams{}));
        Listed before_failure;
        bool empty_after_failure = false;
        auto ask = [&]() -> task<> {
            co_await started.wait();
            before_failure = listed_by(f.peer);
            co_await f.peer.send_request(AddParams{});
            empty_after_failure = f.peer.incoming_requests().empty();
        };

        auto [ran, asked] = f.run(f.peer.run(), ask());
        ZEXPECT(ran.has_value());
        ZEXPECT(asked.has_value());
        const Listed expected = {
            {.method = "test/add", .id = 1}
        };
        ZEXPECT(before_failure == expected);
        ZEXPECT(empty_after_failure);
    });
}

}  // namespace kota::test
