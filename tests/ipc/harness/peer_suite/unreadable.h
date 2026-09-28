#pragma once

// Frames the transport cannot read: one too large is skipped, and what its
// first bytes tell decides what fails; one whose header cannot be read ends
// the input.

#include <string>
#include <utility>
#include <vector>

#include "ipc/harness/peer_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::test {

/// `message` as the transport reports it when it is too large: its size and
/// its first bytes, cut inside its last member.
inline ipc::ReadError too_large(std::string message) {
    return ipc::ReadError{
        .kind = ipc::ReadError::Kind::Oversized,
        .message = "too large",
        .size = message.size(),
        .prefix = message.substr(0, message.size() - 16),
    };
}

/// Params long enough that a message carrying them is cut inside them.
inline NoteParams long_note() {
    return NoteParams{.text = std::string(64, 'x')};
}

template <CodecAdapter A>
void peer_unreadable(const PeerKit<A>& kit) {
    using Fixture = PeerFixture<A>;
    using ipc::protocol::ErrorCode;

    kit.add("oversized_request_is_answered_with_message_too_large", [](Fixture& f) {
        f.remote.send_unreadable(too_large(request<A>(5, "test/note", long_note())));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].kind == Message::Kind::Error);
        EXPECT(written[0].id == RequestID(5));
        EXPECT(code_of(written[0].error) == ErrorCode::MessageTooLarge);
    });

    kit.add("oversized_response_fails_only_its_request", [](Fixture& f) {
        auto remote = [&]() -> task<> {
            co_await f.next();
            co_await f.next();
            f.remote.send_unreadable(too_large(response<A>(1, long_note())));
            f.remote.send(response<A>(2, AddResult{.sum = 2}));
            f.remote.end_input();
        };

        auto [ran, first, second, scripted] = f.run(f.peer.run(),
                                                    f.peer.send_request(AddParams{}),
                                                    f.peer.send_request(AddParams{}),
                                                    remote());
        EXPECT(ran.has_value());
        ASSERT(first.has_error());
        EXPECT(code_of(first.error()) == ErrorCode::MessageTooLarge);
        ASSERT(second.has_value());
        EXPECT(second->sum == 2);
    });

    // An error response whose id is null answers none of the peer's
    // requests, as one read whole would not.
    kit.add("oversized_error_response_without_an_id_fails_no_request", [](Fixture& f) {
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send_unreadable(too_large(
                A::error_response(std::nullopt, ipc::Error(ErrorCode::ParseError, "unreadable"))));
            f.remote.send(response<A>(1, AddResult{.sum = 1}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] =
            f.run(f.peer.run(), f.peer.send_request(AddParams{}), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_value());
        EXPECT(asked->sum == 1);
    });

    kit.add("oversized_notification_is_dropped", [](Fixture& f) {
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send_unreadable(too_large(notification<A>("test/note", long_note())));
            f.remote.send(response<A>(1, AddResult{.sum = 1}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] =
            f.run(f.peer.run(), f.peer.send_request(AddParams{}), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_value());
        EXPECT(asked->sum == 1);
        EXPECT(f.written().size() == 1U);
    });

    // It could have answered any of them; reading goes on after it.
    kit.add("oversized_message_without_an_id_fails_every_pending_request", [](Fixture& f) {
        std::vector<std::string> notes;
        f.peer.on_notification([&](const NoteParams& params) { notes.push_back(params.text); });
        auto remote = [&]() -> task<> {
            co_await f.next();
            co_await f.next();
            f.remote.send_unreadable(ipc::ReadError{
                .kind = ipc::ReadError::Kind::Oversized,
                .message = "too large",
                .size = 1U << 30,
                .prefix = "??",
            });
            f.remote.send(notification<A>("test/note", NoteParams{.text = "after"}));
            f.remote.end_input();
        };

        auto [ran, first, second, scripted] = f.run(f.peer.run(),
                                                    f.peer.send_request(AddParams{}),
                                                    f.peer.send_request(AddParams{}),
                                                    remote());
        EXPECT(ran.has_value());
        ASSERT(first.has_error());
        EXPECT(code_of(first.error()) == ErrorCode::MessageTooLarge);
        ASSERT(second.has_error());
        EXPECT(code_of(second.error()) == ErrorCode::MessageTooLarge);
        EXPECT(notes == std::vector<std::string>{"after"});
    });

    // A connection whose frames cannot be followed is as good as gone:
    // pending requests fail with RequestFailed, as when it closes.
    kit.add("malformed_frame_ends_the_input", [](Fixture& f) {
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send_unreadable(ipc::ReadError{
                .kind = ipc::ReadError::Kind::Malformed,
                .message = "missing Content-Length",
            });
        };

        auto [ran, asked, scripted] =
            f.run(f.peer.run(), f.peer.send_request(AddParams{}), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_error());
        EXPECT(code_of(asked.error()) == ErrorCode::RequestFailed);
        EXPECT(asked.error().message == "missing Content-Length");
    });
}

}  // namespace kota::test
