#pragma once

#include <optional>
#include <string>
#include <utility>

#include "kota/ipc/peer.h"
#include "kota/ipc/lsp/protocol.h"

namespace kota::ipc::lsp {

template <typename PeerT>
class ProgressReporter {
public:
    ProgressReporter(PeerT& peer, protocol::ProgressToken token) :
        peer(peer), token(std::move(token)) {}

    /// Send window/workDoneProgress/create request to register the token.
    task<void, Error> create(request_options opts = {}) {
        auto result = co_await peer.send_request(protocol::WorkDoneProgressCreateParams{token},
                                                 std::move(opts));
        co_await or_fail(result);
    }

    /// Send $/progress with kind=begin.
    Result<void> begin(std::string title,
                       std::optional<std::string> message = {},
                       std::optional<protocol::uinteger> percentage = {},
                       bool cancellable = false) {
        protocol::LSPObject value;
        value.emplace("kind", protocol::LSPAny(std::string("begin")));
        value.emplace("title", protocol::LSPAny(std::move(title)));
        if(cancellable) {
            value.emplace("cancellable", protocol::LSPAny(true));
        }
        if(message) {
            value.emplace("message", protocol::LSPAny(std::move(*message)));
        }
        if(percentage) {
            value.emplace("percentage", protocol::LSPAny(static_cast<std::int64_t>(*percentage)));
        }
        return send_progress(std::move(value));
    }

    /// Send $/progress with kind=report.
    Result<void> report(std::optional<std::string> message = {},
                        std::optional<protocol::uinteger> percentage = {},
                        std::optional<bool> cancellable = {}) {
        protocol::LSPObject value;
        value.emplace("kind", protocol::LSPAny(std::string("report")));
        if(cancellable) {
            value.emplace("cancellable", protocol::LSPAny(*cancellable));
        }
        if(message) {
            value.emplace("message", protocol::LSPAny(std::move(*message)));
        }
        if(percentage) {
            value.emplace("percentage", protocol::LSPAny(static_cast<std::int64_t>(*percentage)));
        }
        return send_progress(std::move(value));
    }

    /// Send $/progress with kind=end.
    Result<void> end(std::optional<std::string> message = {}) {
        protocol::LSPObject value;
        value.emplace("kind", protocol::LSPAny(std::string("end")));
        if(message) {
            value.emplace("message", protocol::LSPAny(std::move(*message)));
        }
        return send_progress(std::move(value));
    }

    PeerT& peer;
    protocol::ProgressToken token;

private:
    Result<void> send_progress(protocol::LSPObject value) {
        return peer.send_notification(
            protocol::ProgressParams{token, protocol::LSPAny(std::move(value))});
    }
};

}  // namespace kota::ipc::lsp
