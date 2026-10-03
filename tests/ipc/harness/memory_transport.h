#pragma once

// An in-memory transport for a Peer, and the Remote a test plays the other
// side with. Messages keep their order and nothing touches the operating
// system, so tests on it are unit tests.
//
//     Remote remote;
//     ipc::JsonPeer peer(loop, remote.transport());
//     remote.send(R"({"jsonrpc":"2.0","method":"exit"})");
//     remote.end_input();
//
// A test orders its steps by what the remote receives: co_await
// remote.receive() returns once the peer has written a message.

#include <cassert>
#include <cstddef>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kota/ipc/transport.h"
#include "kota/async/async.h"

namespace kota::test {

/// One direction of an in-memory link: the messages written and not yet
/// read, in order, and whether the writing side has ended it.
template <typename Message>
struct Channel {
    std::deque<Message> messages;
    bool ended = false;
    event changed;

    /// Queues `message`, unless the channel has ended.
    void push(Message message) {
        if(ended) {
            return;
        }
        messages.push_back(std::move(message));
        changed.set();
    }

    /// Ends the channel: a reader gets what is queued, then the end.
    void end() {
        ended = true;
        changed.set();
    }

    /// The next message, or nothing once the channel has ended and every
    /// message was read.
    task<std::optional<Message>> pop() {
        while(messages.empty() && !ended) {
            changed.reset();
            co_await changed.wait();
        }
        if(messages.empty()) {
            co_return std::nullopt;
        }
        auto message = std::move(messages.front());
        messages.pop_front();
        co_return message;
    }
};

/// What the peer reads next: a message, or a frame it cannot read.
using Delivery = std::expected<std::string, ipc::ReadError>;

/// Both directions of the link between a MemoryTransport and its Remote.
struct Link {
    /// What the remote sends and the peer reads.
    Channel<Delivery> inbound;
    /// What the peer writes and the remote receives.
    Channel<std::string> outbound;
    /// Writes by the peer fail.
    bool writes_fail = false;
    /// The peer's close_output() fails.
    bool close_output_fails = false;
    /// The peer called close() on its transport.
    bool closed = false;
    /// The largest payload the transport carries, when the remote set one.
    std::optional<std::size_t> max_payload;
};

/// The peer's end of a Link.
class MemoryTransport final : public ipc::Transport {
public:
    explicit MemoryTransport(std::shared_ptr<Link> link) : link(std::move(link)) {}

    task<std::string, ipc::ReadError> read_message() override {
        auto delivery = co_await link->inbound.pop();
        if(!delivery) {
            co_await fail(
                ipc::ReadError{.kind = ipc::ReadError::Kind::Closed, .message = "input ended"});
        }
        if(!*delivery) {
            co_await fail(std::move(*delivery).error());
        }
        co_return std::move(**delivery);
    }

    /// Fails once the remote made writes fail, or once the output is closed,
    /// as a write to a closed stream does.
    task<void, ipc::Error> write_message(std::string_view payload) override {
        if(link->writes_fail) {
            co_await fail(ipc::Error("write failed"));
        }
        if(link->outbound.ended) {
            co_await fail(ipc::Error("output closed"));
        }
        link->outbound.push(std::string(payload));
    }

    /// Ends what the remote receives; the input stays open. Fails, and ends
    /// nothing, once the remote made it fail.
    task<void, ipc::Error> close_output() override {
        if(link->close_output_fails) {
            co_await fail(ipc::Error("close_output failed"));
        }
        link->outbound.end();
    }

    /// Ends both directions: a pending read returns nothing, and what the
    /// remote sent and the peer has not read is dropped.
    ipc::Result<void> close() override {
        link->closed = true;
        link->inbound.messages.clear();
        link->inbound.end();
        link->outbound.end();
        return {};
    }

    /// What the remote set, or the default: no limit.
    std::size_t max_payload() const noexcept override {
        return link->max_payload.value_or(Transport::max_payload());
    }

private:
    std::shared_ptr<Link> link;
};

/// The test's end of a Link.
class Remote {
public:
    Remote() : link(std::make_shared<Link>()) {}

    Remote(const Remote&) = delete;
    Remote& operator=(const Remote&) = delete;

    /// The peer's end, to hand to a Peer; taken once.
    std::unique_ptr<ipc::Transport> transport() {
        assert(!taken && "Remote::transport() is taken once");
        taken = true;
        return std::make_unique<MemoryTransport>(link);
    }

    /// Delivers `payload` to the peer's next read.
    void send(std::string payload) {
        link->inbound.push(std::move(payload));
    }

    /// Makes the peer's next read fail with `error`, as a frame it cannot
    /// read would.
    void send_unreadable(ipc::ReadError error) {
        link->inbound.push(std::unexpected(std::move(error)));
    }

    /// Ends the peer's input after what was sent: its read loop sees the end.
    void end_input() {
        link->inbound.end();
    }

    /// Makes every later write by the peer fail.
    void fail_writes() {
        link->writes_fail = true;
    }

    /// Makes the peer's close_output() fail.
    void fail_close_output() {
        link->close_output_fails = true;
    }

    /// Makes the transport carry no payload larger than `size`.
    void limit_payload(std::size_t size) {
        link->max_payload = size;
    }

    /// The next message the peer wrote, waiting for it; nothing once the
    /// peer closed its output and every message was received.
    task<std::optional<std::string>> receive() {
        return link->outbound.pop();
    }

    /// Every message the peer wrote that was not received, without waiting.
    std::vector<std::string> drain() {
        std::vector<std::string> messages(std::make_move_iterator(link->outbound.messages.begin()),
                                          std::make_move_iterator(link->outbound.messages.end()));
        link->outbound.messages.clear();
        return messages;
    }

    /// The peer closed its output, with close_output() or close().
    bool output_ended() const {
        return link->outbound.ended;
    }

    /// The peer closed its transport with close().
    bool closed() const {
        return link->closed;
    }

private:
    std::shared_ptr<Link> link;
    bool taken = false;
};

/// Passes every message `from`'s peer writes on to `to`'s peer, then the end
/// of that output as the end of `to`'s input. Two of these, one each way, link
/// two peers.
inline task<> forward(Remote& from, Remote& to) {
    while(auto message = co_await from.receive()) {
        to.send(std::move(*message));
    }
    to.end_input();
}

}  // namespace kota::test
