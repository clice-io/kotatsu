#pragma once

// The transport's payload limit on what the peer writes: a message larger
// than the transport carries is never written, since the remote would skip
// it unread. A request or notification that large fails at once; an answer
// that large is replaced by a MessageTooLarge error the remote can read.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "ipc/harness/peer_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"
#include "kota/codec/dyn/dyn.h"

namespace kota::test {

/// The limit these cases give the transport: every codec writes the small
/// messages within it, and the MessageTooLarge errors that replace answers.
constexpr std::size_t payload_limit = 256;

/// Text no message carrying it fits within payload_limit.
inline std::string beyond_the_limit() {
    return std::string(4 * payload_limit, 'x');
}

template <CodecAdapter A>
void peer_limit(const PeerKit<A>& kit) {
    using Fixture = PeerFixture<A>;
    using Context = typename Fixture::Context;
    using ipc::protocol::ErrorCode;

    // The request never goes out, so nothing is left waiting for an answer
    // to it: a later request is answered as usual.
    kit.add("request_over_the_limit_fails_without_writing", [](Fixture& f) {
        f.remote.limit_payload(payload_limit);
        auto ask = [&]() -> task<std::pair<ipc::Error, ipc::Result<AddResult>>> {
            auto large = co_await f.peer.template send_request<AddResult>(
                "test/note",
                NoteParams{.text = beyond_the_limit()});
            auto small = co_await f.peer.send_request(AddParams{.a = 1, .b = 2});
            co_return std::pair{large.has_error() ? large.error() : ipc::Error("no error"),
                                std::move(small)};
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.send(response<A>(2, AddResult{.sum = 3}));
            f.remote.end_input();
        };

        auto [ran, asked, scripted] = f.run(f.peer.run(), ask(), remote());
        EXPECT(ran.has_value());
        ASSERT(asked.has_value());
        auto& [large, small] = *asked;
        EXPECT(code_of(large) == ErrorCode::MessageTooLarge);
        ASSERT(small.has_value());
        EXPECT(small->sum == 3);
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].id == RequestID(2));
        EXPECT(written[0].method == "test/add");
    });

    // The limit is the largest payload carried, as the reading side reads
    // it: a request exactly at it is sent, one a byte larger is not. The
    // codec gives the sizes, ids 1 and 2 taking as many bytes.
    kit.add("request_at_the_limit_is_sent_and_one_a_byte_larger_is_not", [](Fixture& f) {
        typename A::Codec codec;
        auto request_size = [&](std::int64_t id, std::size_t length) -> std::size_t {
            auto params = codec.serialize_value(NoteParams{.text = std::string(length, 'x')});
            auto encoded = codec.encode_request(RequestID(id), "test/note", *params);
            return encoded.has_value() ? encoded->size() : 0;
        };
        const auto limit = request_size(1, payload_limit);
        ASSERT(request_size(2, payload_limit + 1) == limit + 1);
        f.remote.limit_payload(limit);
        auto ask = [&](std::size_t length) -> task<ipc::Error> {
            auto asked = co_await f.peer.template send_request<AddResult>(
                "test/note",
                NoteParams{.text = std::string(length, 'x')});
            co_return asked.has_error() ? asked.error() : ipc::Error("no error");
        };
        auto remote = [&]() -> task<> {
            co_await f.next();
            f.remote.end_input();
        };

        auto [ran, at, over, scripted] =
            f.run(f.peer.run(), ask(payload_limit), ask(payload_limit + 1), remote());
        EXPECT(ran.has_value());
        ASSERT(at.has_value());
        EXPECT(code_of(*at) == ErrorCode::ConnectionClosed);
        ASSERT(over.has_value());
        EXPECT(code_of(*over) == ErrorCode::MessageTooLarge);
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].id == RequestID(1));
    });

    kit.add("notification_over_the_limit_fails_without_writing", [](Fixture& f) {
        f.remote.limit_payload(payload_limit);
        auto large = f.peer.send_notification(NoteParams{.text = beyond_the_limit()});
        auto small = f.peer.send_notification(NoteParams{.text = "small"});
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        ASSERT(large.has_error());
        EXPECT(code_of(large.error()) == ErrorCode::MessageTooLarge);
        EXPECT(small.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        auto note = decoded<NoteParams, A>(written[0].body);
        ASSERT(note.has_value());
        EXPECT(note->text == "small");
    });

    // test/repeat answers with `a` x's: few fit, many do not.
    kit.add("result_over_the_limit_is_answered_with_message_too_large", [](Fixture& f) {
        f.remote.limit_payload(payload_limit);
        f.peer.on_request("test/repeat",
                          [](Context&, const AddParams& params) -> task<NoteParams, ipc::Error> {
                              co_return NoteParams{
                                  .text = std::string(static_cast<std::size_t>(params.a), 'x')};
                          });
        f.remote.send(request<A>(1, "test/repeat", AddParams{.a = 4}));
        f.remote.send(request<A>(2,
                                 "test/repeat",
                                 AddParams{.a = static_cast<std::int64_t>(4 * payload_limit)}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 2U);
        EXPECT(written[0].kind == Message::Kind::Result);
        EXPECT(written[0].id == RequestID(1));
        EXPECT(written[1].kind == Message::Kind::Error);
        EXPECT(written[1].id == RequestID(2));
        EXPECT(code_of(written[1].error) == ErrorCode::MessageTooLarge);
        EXPECT(!written[1].error.data.has_value());
    });

    // The same for a result: one exactly at the limit is sent, one a byte
    // larger replaced.
    kit.add("result_at_the_limit_is_sent_and_one_a_byte_larger_is_not", [](Fixture& f) {
        typename A::Codec codec;
        auto response_size = [&](std::int64_t id, std::size_t length) -> std::size_t {
            auto result = codec.serialize_value(NoteParams{.text = std::string(length, 'x')});
            auto encoded = codec.encode_success_response(RequestID(id), *result);
            return encoded.has_value() ? encoded->size() : 0;
        };
        const auto limit = response_size(1, payload_limit);
        ASSERT(response_size(2, payload_limit + 1) == limit + 1);
        f.remote.limit_payload(limit);
        f.peer.on_request("test/repeat",
                          [](Context&, const AddParams& params) -> task<NoteParams, ipc::Error> {
                              co_return NoteParams{
                                  .text = std::string(static_cast<std::size_t>(params.a), 'x')};
                          });
        f.remote.send(
            request<A>(1, "test/repeat", AddParams{.a = static_cast<std::int64_t>(payload_limit)}));
        f.remote.send(request<A>(2,
                                 "test/repeat",
                                 AddParams{.a = static_cast<std::int64_t>(payload_limit + 1)}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 2U);
        EXPECT(written[0].kind == Message::Kind::Result);
        EXPECT(written[0].id == RequestID(1));
        EXPECT(written[1].kind == Message::Kind::Error);
        EXPECT(written[1].id == RequestID(2));
        EXPECT(code_of(written[1].error) == ErrorCode::MessageTooLarge);
    });

    // The error's data is what makes it too large; the error that replaces
    // it has none.
    kit.add("error_over_the_limit_is_answered_with_message_too_large", [](Fixture& f) {
        f.remote.limit_payload(payload_limit);
        f.peer.on_request([](Context&, const AddParams&) -> ipc::RequestResult<AddParams> {
            co_await fail(-32001, "failed", codec::dyn::Value(beyond_the_limit()));
        });
        f.remote.send(request<A>(1, "test/add", AddParams{}));
        f.remote.end_input();

        auto [ran] = f.run(f.peer.run());
        EXPECT(ran.has_value());
        const auto& written = f.written();
        ASSERT(written.size() == 1U);
        EXPECT(written[0].kind == Message::Kind::Error);
        EXPECT(written[0].id == RequestID(1));
        EXPECT(code_of(written[0].error) == ErrorCode::MessageTooLarge);
        EXPECT(!written[0].error.data.has_value());
    });
}

}  // namespace kota::test
