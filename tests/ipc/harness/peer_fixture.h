#pragma once

// A Peer on an in-memory transport, and the kit its shared suite registers
// through. Peer<Codec> is one implementation over any codec, so its
// behaviour is written once, in the areas under peer_suite/, and runs over
// every codec through the codec's adapter:
//
//     ZEST_CASE_GROUP(dispatch) {
//         test::peer_dispatch(test::PeerKit<test::JSONAdapter>{add_case});
//     }
//
// Each case gets a fresh PeerFixture. The remote's side of a case is a
// coroutine run next to peer.run(); it orders its steps by what the peer
// writes (co_await next()), and ends the case by ending the peer's input or
// by closing the peer. The case then checks written(), everything the peer
// wrote. Expected failures work as in codec_kit.h.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ipc/harness/codec_kit.h"
#include "ipc/harness/fixtures.h"
#include "ipc/harness/memory_transport.h"
#include "kota/ipc/peer.h"
#include "kota/zest/async.h"
#include "kota/zest/zest.h"

namespace kota::test {

/// Waits for `signal`: a task, whose cancellation its awaiter can catch.
inline task<> wait_for(event& signal) {
    co_await signal.wait();
}

template <CodecAdapter A>
struct PeerFixture : zest::LoopFixture {
    using Peer = ipc::Peer<typename A::Codec>;
    using Context = typename Peer::RequestContext;

    Remote remote;
    Peer peer{loop, remote.transport()};

    /// Answers test/add with the sum of its params.
    void serve_add() {
        peer.on_request([](Context&, const AddParams& params) -> ipc::RequestResult<AddParams> {
            co_return AddResult{.sum = params.a + params.b};
        });
    }

    /// Waits for the next message the peer writes, or for its output to end,
    /// and keeps the message for written().
    task<> next() {
        if(auto payload = co_await remote.receive()) {
            keep(*payload);
        }
    }

    /// Every message the peer wrote, in order, read back with the adapter:
    /// those next() waited for and those still queued. One the adapter cannot
    /// read fails the test and is left out.
    const std::vector<Message>& written() {
        for(auto& payload: remote.drain()) {
            keep(payload);
        }
        return kept;
    }

private:
    void keep(const std::string& payload) {
        auto message = A::read(payload);
        if(!message) {
            ZEST_CONTEXT("{} cannot read what the peer wrote: {}", A::name, message.error());
            // Reports the failure: message holds an error here.
            ZEXPECT(message.has_value());
            return;
        }
        kept.push_back(std::move(*message));
    }

    std::vector<Message> kept;
};

/// The sum a result message carries, if it is one of AddResult.
template <CodecAdapter A>
std::optional<std::int64_t> sum_of(const Message& message) {
    if(message.kind != Message::Kind::Result) {
        return std::nullopt;
    }
    auto result = decoded<AddResult, A>(message.body);
    if(!result) {
        return std::nullopt;
    }
    return result->sum;
}

/// Where the Peer cases of adapter A are registered.
template <CodecAdapter A>
struct PeerKit {
    const zest::CaseRegistrar& add_case;

    /// Registers `body` as case `name`; it runs on a fresh PeerFixture<A>.
    template <typename Body>
    void add(std::string name, Body body) const {
        add_case(std::move(name), [body] {
            PeerFixture<A> fixture;
            body(fixture);
        });
    }
};

/// Two peers of one codec on one loop, each writing to the other through
/// forward(). Closing both ends the case.
template <CodecAdapter A>
struct LinkedPeers : zest::LoopFixture {
    using Peer = ipc::Peer<typename A::Codec>;
    using Context = typename Peer::RequestContext;

    Remote a_end;
    Remote b_end;
    Peer a{loop, a_end.transport()};
    Peer b{loop, b_end.transport()};

    /// Runs both peers and the forwarding between them next to `scripts`,
    /// checks that the peers and the forwarding ended well, and returns what
    /// each script ended with.
    template <typename... Tasks>
    std::tuple<zest::run_result_t<Tasks>...> run_with(Tasks... scripts) {
        auto results = run(a.run(),
                           b.run(),
                           forward(a_end, b_end),
                           forward(b_end, a_end),
                           std::move(scripts)...);
        ZEXPECT(std::get<0>(results).has_value());
        ZEXPECT(std::get<1>(results).has_value());
        ZEXPECT(std::get<2>(results).has_value());
        ZEXPECT(std::get<3>(results).has_value());
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return std::tuple<zest::run_result_t<Tasks>...>(std::move(std::get<I + 4>(results))...);
        }(std::index_sequence_for<Tasks...>{});
    }
};

}  // namespace kota::test
