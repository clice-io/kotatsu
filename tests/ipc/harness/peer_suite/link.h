#pragma once

// Two peers of one codec talking to each other: the library writes and
// parses both sides, so these check the codec end to end.

#include <string>
#include <vector>

#include "ipc/harness/peer_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"
#include "kota/codec/dyn/dyn.h"

namespace kota::test {

template <Wire W>
void peer_link(const PeerKit<W>& kit) {
    using Peers = LinkedPeers<W>;
    using Context = typename Peers::Context;
    using ipc::protocol::ErrorCode;

    kit.add_case("request_and_notification_cross_between_peers", [] {
        Peers f;
        std::vector<std::string> notes;
        f.b.on_request([](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            co_return AddResult{.sum = params.a + params.b};
        });
        f.b.on_notification([&](const NoteParams& params) { notes.push_back(params.text); });
        auto script = [&]() -> task<AddResult, ipc::Error> {
            co_await or_fail(f.a.send_notification(NoteParams{.text = "hello"}));
            auto result = co_await f.a.send_request(AddParams{.a = 2, .b = 3}).or_fail();
            f.a.close();
            f.b.close();
            co_return result;
        };

        auto [asked] = f.run_with(script());
        ASSERT(asked.has_value());
        EXPECT(asked->sum == 5);
        EXPECT(notes == std::vector<std::string>{"hello"});
    });

    kit.add_case("error_crosses_between_peers", [] {
        Peers f;
        f.b.on_request([](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            co_await fail(-32001, "remote failed");
        });
        auto script = [&]() -> task<ipc::Error> {
            auto result = co_await f.a.send_request(AddParams{});
            f.a.close();
            f.b.close();
            co_return result.has_error() ? result.error() : ipc::Error("no error");
        };

        auto [failure] = f.run_with(script());
        ASSERT(failure.has_value());
        EXPECT(failure->code == -32001);
        EXPECT(failure->message == "remote failed");
    });

    // b's handler sees its cancellation arrive, which shows the
    // $/cancelRequest crossed, before the script closes both.
    kit.add_case("cancellation_crosses_between_peers", [] {
        Peers f;
        event started;
        event cancelled;
        event never;
        f.b.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            started.set();
            auto waited = co_await never.wait().catch_cancel();
            if(waited.is_cancelled()) {
                cancelled.set();
            }
            co_return AddResult{};
        });
        cancellation_source source;
        auto ask = [&]() -> task<AddResult, ipc::Error> {
            co_return co_await f.a.send_request(AddParams{}, {.token = source.token()}).or_fail();
        };
        auto script = [&]() -> task<> {
            co_await started.wait();
            source.cancel();
            co_await cancelled.wait();
            f.a.close();
            f.b.close();
        };

        auto [asked, scripted] = f.run_with(ask(), script());
        EXPECT(scripted.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::RequestCancelled);
    });
}

/// An error's data reaches the peer that asked.
template <Wire W>
void error_data_crosses_between_peers() {
    using Context = typename LinkedPeers<W>::Context;
    LinkedPeers<W> f;
    codec::dyn::Value data{
        {"detail",  "bad state"},
        {"attempt", -1         },
    };
    f.b.on_request([&](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
        co_await fail(ipc::protocol::ErrorCode::InvalidParams, "rejected", data);
    });
    auto script = [&]() -> task<ipc::Error> {
        auto result = co_await f.a.send_request(AddParams{});
        f.a.close();
        f.b.close();
        co_return result.has_error() ? result.error() : ipc::Error("no error");
    };

    auto [failure] = f.run_with(script());
    ASSERT(failure.has_value());
    EXPECT(code_of(*failure) == ipc::protocol::ErrorCode::InvalidParams);
    ASSERT(failure->data.has_value());
    EXPECT(*failure->data == data);
}

}  // namespace kota::test
