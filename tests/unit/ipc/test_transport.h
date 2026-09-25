#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kota/ipc/peer.h"
#include "kota/async/async.h"

namespace kota::ipc {

class FakeTransport final : public Transport {
public:
    explicit FakeTransport(std::vector<std::string> incoming) :
        incoming_messages(std::move(incoming)) {}

    task<std::optional<std::string>, Error> read_message() override {
        if(read_index >= incoming_messages.size()) {
            co_return std::nullopt;
        }
        co_return incoming_messages[read_index++];
    }

    task<void, Error> write_message(std::string_view payload) override {
        outgoing_messages.emplace_back(payload);
        co_return;
    }

    const std::vector<std::string>& outgoing() const {
        return outgoing_messages;
    }

private:
    std::vector<std::string> incoming_messages;
    std::vector<std::string> outgoing_messages;
    std::size_t read_index = 0;
};

class ScriptedTransport final : public Transport {
public:
    using WriteHook = std::function<void(std::string_view, ScriptedTransport&)>;

    ScriptedTransport(std::vector<std::string> incoming, WriteHook hook) :
        incoming_messages(std::move(incoming)), write_hook(std::move(hook)) {
        if(!incoming_messages.empty()) {
            readable.set();
        }
    }

    task<std::optional<std::string>, Error> read_message() override {
        while(true) {
            if(read_failure) {
                co_await kota::fail(*std::exchange(read_failure, std::nullopt));
            }
            if(read_index < incoming_messages.size()) {
                co_return incoming_messages[read_index++];
            }
            if(closed) {
                co_return std::nullopt;
            }

            co_await readable.wait();
            readable.reset();
        }
    }

    task<void, Error> write_message(std::string_view payload) override {
        if(fail_writes) {
            co_await kota::fail(Error("write failed"));
        }
        outgoing_messages.emplace_back(payload);
        if(write_hook) {
            write_hook(payload, *this);
        }
        co_return;
    }

    void set_fail_writes(bool fail) {
        fail_writes = fail;
    }

    /// Make the next read fail with `reason`, the way a real transport reports
    /// a frame it cannot deliver.
    void fail_next_read(Error reason) {
        read_failure = std::move(reason);
        readable.set();
    }

    void push_incoming(std::string payload) {
        incoming_messages.push_back(std::move(payload));
        readable.set();
    }

    Result<void> close() override {
        closed = true;
        readable.set();
        return {};
    }

    const std::vector<std::string>& outgoing() const {
        return outgoing_messages;
    }

private:
    std::vector<std::string> incoming_messages;
    std::vector<std::string> outgoing_messages;
    std::size_t read_index = 0;
    WriteHook write_hook;
    event readable;
    bool closed = false;
    bool fail_writes = false;
    std::optional<Error> read_failure;
};

inline std::string frame(std::string_view payload) {
    std::string out;
    out.reserve(payload.size() + 32);
    out.append("Content-Length: ");
    out.append(std::to_string(payload.size()));
    out.append("\r\n\r\n");
    out.append(payload);
    return out;
}

}  // namespace kota::ipc
