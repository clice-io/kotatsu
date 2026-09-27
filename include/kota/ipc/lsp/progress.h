#pragma once

#include <optional>
#include <string>
#include <utility>

#include "kota/ipc/codec.h"
#include "kota/ipc/peer.h"
#include "kota/codec/dyn/dyn.h"
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
        return send_progress(protocol::WorkDoneProgressBegin{
            .title = std::move(title),
            // Absent reads as not cancellable, so false is left out.
            .cancellable = cancellable ? std::optional<bool>(true) : std::nullopt,
            .message = std::move(message),
            .percentage = percentage,
        });
    }

    /// Send $/progress with kind=report. An explicit `cancellable` sets the
    /// cancel button's state; left out, the button stays as it is.
    Result<void> report(std::optional<std::string> message = {},
                        std::optional<protocol::uinteger> percentage = {},
                        std::optional<bool> cancellable = {}) {
        return send_progress(protocol::WorkDoneProgressReport{
            .cancellable = cancellable,
            .message = std::move(message),
            .percentage = percentage,
        });
    }

    /// Send $/progress with kind=end.
    Result<void> end(std::optional<std::string> message = {}) {
        return send_progress(protocol::WorkDoneProgressEnd{.message = std::move(message)});
    }

    PeerT& peer;
    protocol::ProgressToken token;

private:
    /// Sends ProgressParams, its LSPAny the typed `value` made dynamic, with
    /// LSP's member names: every codec then writes the ProgressParams a
    /// receiver reads, a binary one included.
    template <typename Value>
    Result<void> send_progress(const Value& value) {
        auto any = codec::dyn::to_dyn<lsp_config>(value);
        if(!any) {
            return outcome_error(
                Error(protocol::ErrorCode::InternalError, any.error().to_string()));
        }
        return peer.send_notification(
            protocol::ProgressParams{.token = token, .value = std::move(*any)});
    }
};

}  // namespace kota::ipc::lsp
