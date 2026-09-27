#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "async/harness/os.h"
#include "ipc/harness/fixtures.h"
#include "kota/ipc/codec/bincode.h"
#include "kota/ipc/codec/json.h"
#include "kota/ipc/transport.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::ipc {

namespace {

using test::AddParams;
using test::AddResult;
using test::NoteParams;

/// Both ends of an anonymous pipe, opened as streams.
struct Ends {
    stream reader;
    stream writer;
};

std::optional<Ends> pipe_ends(event_loop& loop) {
    int fds[2] = {-1, -1};
    if(test::create_pipe(fds) != 0) {
        return std::nullopt;
    }
    auto reader = pipe::open(fds[0], {}, loop);
    auto writer = pipe::open(fds[1], {}, loop);
    if(!reader || !writer) {
        return std::nullopt;
    }
    return Ends{.reader = stream(std::move(*reader)), .writer = stream(std::move(*writer))};
}

/// Two peers of one codec, each reading the pipe the other writes. b sums
/// test/add and keeps the notes it gets; a asks, then closes both.
template <typename Codec>
void talk_over_pipes(test::LoopFixture& fixture) {
    using CodecPeer = Peer<Codec>;
    auto a_to_b = pipe_ends(fixture.loop);
    auto b_to_a = pipe_ends(fixture.loop);
    ASSERT(a_to_b.has_value());
    ASSERT(b_to_a.has_value());
    CodecPeer a(
        fixture.loop,
        std::make_unique<StreamTransport>(std::move(b_to_a->reader), std::move(a_to_b->writer)));
    CodecPeer b(
        fixture.loop,
        std::make_unique<StreamTransport>(std::move(a_to_b->reader), std::move(b_to_a->writer)));
    std::vector<std::string> notes;
    b.on_request([](typename CodecPeer::RequestContext&,
                    const AddParams& params) -> RequestResult<AddParams> {
        co_return AddResult{.sum = params.a + params.b};
    });
    b.on_notification([&](const NoteParams& params) { notes.push_back(params.text); });
    auto ask = [&]() -> task<AddResult, Error> {
        co_await or_fail(a.send_notification(NoteParams{.text = "hello"}));
        auto result = co_await a.send_request(AddParams{.a = 2, .b = 3}).or_fail();
        a.close();
        b.close();
        co_return result;
    };

    auto [ran_a, ran_b, asked] = fixture.run(a.run(), b.run(), ask());
    EXPECT(ran_a.has_value());
    EXPECT(ran_b.has_value());
    ASSERT(asked.has_value());
    EXPECT(asked->sum == 5);
    EXPECT(notes == std::vector<std::string>{"hello"});
}

ZEST_SUITE(ipc_peer_stream, test::LoopFixture) {

ZEST_CASE(json_peers_talk_over_pipes) {
    talk_over_pipes<JsonCodec>(*this);
}

ZEST_CASE(bincode_peers_talk_over_pipes) {
    talk_over_pipes<BincodeCodec>(*this);
}

// A notification larger than the pipe holds, which the test reads only once,
// is still being written when the peer closes. The write ends when the
// stream closes, and run() ends as it does after any close().
ZEST_CASE(close_during_a_write_ends_run) {
    auto output = pipe_ends(loop);
    auto input = pipe_ends(loop);
    ASSERT(output.has_value());
    ASSERT(input.has_value());
    JsonPeer peer(
        loop,
        std::make_unique<StreamTransport>(std::move(input->reader), std::move(output->writer)));
    auto closer = [&]() -> task<bool> {
        auto sent = peer.send_notification(NoteParams{.text = std::string(1024 * 1024, 'x')});
        // The first bytes arriving show the write has started; read_some
        // reads no more than asked, so the pipe stays full.
        std::array<char, 16> first{};
        co_await output->reader.read_some(first);
        peer.close();
        co_return sent.has_value();
    };

    auto [ran, closed] = run(peer.run(), closer());
    EXPECT(ran.has_value());
    ASSERT(closed.has_value());
    EXPECT(*closed);
}

// Closing stops the pending read, which resumes the read loop and ends run()
// before close() returns; its owner then destroys the peer, and close() must
// touch nothing of it after (under ASan, it would be a use after free).
ZEST_CASE(peer_destroyed_as_close_ends_run) {
    auto output = pipe_ends(loop);
    auto input = pipe_ends(loop);
    ASSERT(output.has_value());
    ASSERT(input.has_value());
    auto peer = std::make_unique<JsonPeer>(
        loop,
        std::make_unique<StreamTransport>(std::move(input->reader), std::move(output->writer)));
    bool destroyed_in_close = false;
    bool closing = false;
    auto owner = [&]() -> task<> {
        co_await peer->run();
        destroyed_in_close = closing;
        peer.reset();
    };
    // The owner's run() is reading by the time the closer starts: tasks run
    // in order until they first suspend.
    auto closer = [&]() -> task<> {
        closing = true;
        auto closed = peer->close();
        closing = false;
        EXPECT(closed.has_value());
        co_return;
    };

    auto [owned, done] = run(owner(), closer());
    EXPECT(owned.has_value());
    EXPECT(done.has_value());
    EXPECT(peer == nullptr);
    EXPECT(destroyed_in_close);
}

// One TCP stream cannot half-close yet, so closing its output closes it and
// ends run().
ZEST_CASE(close_output_on_a_shared_stream_ends_run) {
    auto listener = tcp::listen("127.0.0.1", 0, {}, loop);
    ASSERT(listener.has_value());
    auto port = tcp::local_port(*listener);
    ASSERT(port.has_value());
    auto [accepted, connected] =
        run(listener->accept(), StreamTransport::connect_tcp("127.0.0.1", *port, loop));
    ASSERT(accepted.has_value());
    ASSERT(connected.has_value());
    JsonPeer peer(loop, std::move(*connected));
    auto drain = [&]() -> task<std::string> {
        std::string received;
        while(auto chunk = co_await accepted->read()) {
            received += *chunk;
        }
        co_return received;
    };
    auto closer = [&]() -> task<> {
        peer.close_output();
        co_return;
    };

    auto [ran, drained, closed] = run(peer.run(), drain(), closer());
    EXPECT(ran.has_value());
    EXPECT(drained.has_value());
}

};  // ZEST_SUITE(ipc_peer_stream)

}  // namespace

}  // namespace kota::ipc
