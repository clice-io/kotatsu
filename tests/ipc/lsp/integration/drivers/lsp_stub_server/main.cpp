#include <print>

#include "features.h"
#include "ipc/harness/stderr_logger.h"
#include "kota/ipc/codec/json.h"
#include "kota/ipc/lsp/protocol.h"

namespace ipc = kota::ipc;
namespace proto = ipc::protocol;

namespace {

auto make_capabilities() -> proto::ServerCapabilities {
    proto::ServerCapabilities caps;
    caps.text_document_sync = proto::TextDocumentSyncKind::Full;
    caps.hover_provider = true;
    caps.completion_provider = proto::CompletionOptions{};
    caps.definition_provider = true;
    caps.references_provider = true;
    caps.document_symbol_provider = true;
    caps.document_formatting_provider = true;
    caps.code_action_provider = true;
    caps.signature_help_provider = proto::SignatureHelpOptions{};
    caps.document_highlight_provider = true;
    caps.rename_provider = proto::RenameOptions{.prepare_provider = true};
    caps.folding_range_provider = true;
    caps.selection_range_provider = true;
    caps.declaration_provider = true;
    caps.type_definition_provider = true;
    caps.implementation_provider = true;
    caps.document_link_provider = proto::DocumentLinkOptions{};
    caps.code_lens_provider = proto::CodeLensOptions{};
    caps.inlay_hint_provider = true;
    caps.document_range_formatting_provider = true;
    caps.workspace_symbol_provider = true;
    return caps;
}

}  // namespace

int main() {
    kota::event_loop loop;
    auto transport = ipc::StreamTransport::open_stdio(loop);
    if(!transport) {
        std::println(stderr, "failed to open stdio: {}", transport.error().message);
        return 1;
    }

    ipc::JSONPeer peer(loop, std::move(*transport));
    peer.set_logger(kota::test::stderr_logger(), ipc::LogLevel::trace);

    bool shutdown_requested = false;

    // initialize
    peer.on_request(
        [&](ipc::JSONPeer::RequestContext&,
            const proto::InitializeParams&) -> ipc::RequestResult<proto::InitializeParams> {
            co_return proto::InitializeResult{
                .capabilities = make_capabilities(),
                .server_info = proto::ServerInfo{.name = "stub-server", .version = "0.1.0"},
            };
        });

    // shutdown
    peer.on_request([&](ipc::JSONPeer::RequestContext&,
                        const proto::ShutdownParams&) -> ipc::RequestResult<proto::ShutdownParams> {
        shutdown_requested = true;
        co_return nullptr;
    });

    // initialized
    peer.on_notification([](const proto::InitializedParams&) {});

    // exit: closing the peer ends run(), and with it the loop.
    peer.on_notification([&](const proto::ExitParams&) { peer.close(); });

    kota::test::add_language_features(peer);

    // textDocument/didOpen → publish diagnostics
    peer.on_notification([&](const proto::DidOpenTextDocumentParams& p) {
        peer.send_notification(proto::PublishDiagnosticsParams{
            .uri = p.text_document.uri,
            .diagnostics = {proto::Diagnostic{
                .range = kota::test::make_range(0, 0, 5),
                .severity = proto::DiagnosticSeverity::Warning,
                .message = "stub warning",
            }},
        });
    });

    // textDocument/didChange, didClose, didSave — no-op
    peer.on_notification([](const proto::DidChangeTextDocumentParams&) {});
    peer.on_notification([](const proto::DidCloseTextDocumentParams&) {});
    peer.on_notification([](const proto::DidSaveTextDocumentParams&) {});

    loop.schedule(peer.run());
    loop.run();
    // LSP's exit code: 0 if a shutdown request came first, 1 otherwise.
    return shutdown_requested ? 0 : 1;
}
