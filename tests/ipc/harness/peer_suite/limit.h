#pragma once

// The remote's limit on what it reads: Peer sends nothing larger. A request
// or notification over it fails unsent; an answer over it is replaced by a
// MessageTooLarge error, or dropped when even that is over it.

#include <cstddef>
#include <string>

#include "ipc/harness/peer_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::test {

template <CodecAdapter A>
void peer_limit(const PeerKit<A>& kit) {
    using Fixture = PeerFixture<A>;
    using Context = typename Fixture::Context;
    using ipc::protocol::ErrorCode;

    // Room for a short note, and for the error of one too large.
    constexpr std::size_t limit = 200;
    const NoteParams long_note{.text = std::string(limit, 'x')};

    kit.add("request_over_the_limit_fails_unsent", [=](Fixture& f) {
        f.remote.limit_payload(limit);
        auto ask = [&]() -> task<NoteParams, ipc::Error> {
            auto asked = co_await f.peer.template send_request<NoteParams>("test/echo", long_note);
            f.remote.end_input();
            co_return co_await or_fail(std::move(asked));
        };

        auto [ran, asked] = f.run(f.peer.run(), ask());
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::MessageTooLarge);
        EXPECT(f.written().empty());
    });

    kit.add("notification_over_the_limit_fails_unsent", [=](Fixture& f) {
        f.remote.limit_payload(limit);
        auto sent = f.peer.send_notification(long_note);
        auto short_sent = f.peer.send_notification(NoteParams{.text = "short"});
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        ASSERT(sent.has_error());
        EXPECT(code_of(sent.error()) == ErrorCode::MessageTooLarge);
        EXPECT(short_sent.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].method == "test/note");
    });

    kit.add("answer_over_the_limit_is_message_too_large", [=](Fixture& f) {
        f.remote.limit_payload(limit);
        f.peer.on_request("test/echo",
                          [](Context&, const NoteParams& note) -> task<NoteParams, ipc::Error> {
                              co_return note;
                          });
        f.remote.send(request<A>(3, "test/echo", long_note));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].kind == Message::Kind::Error);
        EXPECT(written[0].id == RequestID(3));
        EXPECT(code_of(written[0].error) == ErrorCode::MessageTooLarge);
    });

    kit.add("error_answer_over_the_limit_is_message_too_large", [=](Fixture& f) {
        f.remote.limit_payload(limit);
        f.peer.on_request([](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            co_await fail(-32001, std::string(limit, 'x'));
        });
        f.remote.send(request<A>(3, "test/add", AddParams{}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].id == RequestID(3));
        EXPECT(code_of(written[0].error) == ErrorCode::MessageTooLarge);
    });

    // A limit too small even for the error leaves the request unanswered.
    kit.add("answer_without_room_for_its_error_is_dropped", [=](Fixture& f) {
        f.remote.limit_payload(16);
        bool handled = false;
        f.peer.on_request("test/echo",
                          [&](Context&, const NoteParams& note) -> task<NoteParams, ipc::Error> {
                              handled = true;
                              co_return note;
                          });
        f.remote.send(request<A>(3, "test/echo", long_note));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        EXPECT(handled);
        EXPECT(f.written().empty());
    });

    // The limit drops below the $/cancelRequest once the request is out:
    // the remote cannot be told, so the request ends at once.
    kit.add("cancel_request_over_the_limit_ends_the_request_at_once", [](Fixture& f) {
        cancellation_source source;
        event done;
        auto ask = [&]() -> task<ipc::Error> {
            auto asked = co_await f.peer.send_request(AddParams{}, {.token = source.token()});
            done.set();
            co_return asked.has_error() ? asked.error() : ipc::Error("answered");
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.limit_payload(1);
            source.cancel();
            co_await done.wait();
            f.remote.end_input();
        };

        auto [ran, failure, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        ASSERT(failure.has_value());
        EXPECT(code_of(*failure) == ErrorCode::RequestCancelled);
        EXPECT(f.written().size() == 1U);
    });
}

}  // namespace kota::test
