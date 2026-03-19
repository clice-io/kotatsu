#include <print>
#include <string>
#include <vector>

#include "eventide/ipc/peer.h"
#include "eventide/ipc/lsp/progress.h"
#include "eventide/ipc/lsp/protocol.h"

namespace et = eventide;
namespace ipc = et::ipc;
namespace lsp = ipc::lsp;
namespace proto = ipc::protocol;

namespace {

auto make_range(proto::uinteger line, proto::uinteger col, proto::uinteger end_col)
    -> proto::Range {
    return {
        {line, col    },
        {line, end_col}
    };
}

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
    et::event_loop loop;
    auto transport = ipc::StreamTransport::open_stdio(loop);
    if(!transport) {
        std::println(stderr, "failed to open stdio: {}", transport.error().message);
        return 1;
    }

    ipc::JsonPeer peer(loop, std::move(*transport));
    peer.set_logger(
        [](ipc::LogLevel lvl, std::string msg) {
            std::println(stderr, "[stub:{}] {}", static_cast<int>(lvl), msg);
        },
        ipc::LogLevel::trace);

    bool shutdown_requested = false;

    // initialize
    peer.on_request(
        [&](ipc::JsonPeer::RequestContext&,
            const proto::InitializeParams&) -> ipc::RequestResult<proto::InitializeParams> {
            co_return proto::InitializeResult{
                .capabilities = make_capabilities(),
                .server_info = proto::ServerInfo{.name = "stub-server", .version = "0.1.0"},
            };
        });

    // shutdown
    peer.on_request([&](ipc::JsonPeer::RequestContext&,
                        const proto::ShutdownParams&) -> ipc::RequestResult<proto::ShutdownParams> {
        shutdown_requested = true;
        co_return nullptr;
    });

    // initialized
    peer.on_notification([](const proto::InitializedParams&) {});

    // exit
    peer.on_notification([&](const proto::ExitParams&) { loop.stop(); });

    // textDocument/hover
    peer.on_request([](ipc::JsonPeer::RequestContext&,
                       const proto::HoverParams& p) -> ipc::RequestResult<proto::HoverParams> {
        auto uri = p.text_document_position_params.text_document.uri;
        if(uri == "file:///error") {
            co_return et::outcome_error(
                proto::Error(static_cast<proto::integer>(-32600), "hover error triggered"));
        }
        co_return proto::Hover{
            .contents =
                proto::MarkupContent{
                                     .kind = proto::MarkupKind::Markdown,
                                     .value = "stub hover",
                                     },
        };
    });

    // textDocument/completion
    peer.on_request(
        [&](ipc::JsonPeer::RequestContext&,
            const proto::CompletionParams& p) -> ipc::RequestResult<proto::CompletionParams> {
            auto uri = p.text_document_position_params.text_document.uri;
            if(uri == "file:///progress") {
                lsp::ProgressReporter reporter(peer,
                                               proto::ProgressToken(std::string("test-progress")));
                auto create_result = co_await reporter.create();
                if(create_result.has_error()) {
                    co_return et::outcome_error(create_result.error());
                }
                reporter.begin("Indexing", "starting...", 0);
                reporter.report("halfway...", 50);
                reporter.end("done");
            }
            co_return proto::CompletionList{
                .is_incomplete = false,
                .items = {proto::CompletionItem{.label = "stub_item"}},
            };
        });

    // textDocument/definition
    peer.on_request(
        [](ipc::JsonPeer::RequestContext&,
           const proto::DefinitionParams& p) -> ipc::RequestResult<proto::DefinitionParams> {
            co_return proto::Definition{
                proto::Location{
                                .uri = p.text_document_position_params.text_document.uri,
                                .range = make_range(10, 0, 5),
                                }
            };
        });

    // textDocument/references
    peer.on_request(
        [](ipc::JsonPeer::RequestContext&,
           const proto::ReferenceParams& p) -> ipc::RequestResult<proto::ReferenceParams> {
            auto uri = p.text_document_position_params.text_document.uri;
            co_return std::vector<proto::Location>{
                {uri, make_range(1, 0, 3)},
                {uri, make_range(5, 0, 3)}
            };
        });

    // textDocument/documentSymbol
    peer.on_request(
        [](ipc::JsonPeer::RequestContext&,
           const proto::DocumentSymbolParams&) -> ipc::RequestResult<proto::DocumentSymbolParams> {
            co_return std::vector<proto::DocumentSymbol>{
                {
                 .name = "StubSymbol",
                 .kind = proto::SymbolKind::Function,
                 .range = make_range(0, 0, 10),
                 .selection_range = make_range(0, 0, 10),
                 }
            };
        });

    // textDocument/formatting
    peer.on_request([](ipc::JsonPeer::RequestContext&, const proto::DocumentFormattingParams&)
                        -> ipc::RequestResult<proto::DocumentFormattingParams> {
        co_return std::vector<proto::TextEdit>{
            {
             .range = make_range(0, 0, 0),
             .new_text = "formatted\n",
             }
        };
    });

    // textDocument/codeAction
    peer.on_request(
        [](ipc::JsonPeer::RequestContext&,
           const proto::CodeActionParams&) -> ipc::RequestResult<proto::CodeActionParams> {
            co_return std::vector<proto::variant<proto::Command, proto::CodeAction>>{
                proto::CodeAction{
                                  .title = "stub action",
                                  .kind = proto::CodeActionKind("quickfix"),
                                  },
            };
        });

    // textDocument/signatureHelp
    peer.on_request([](ipc::JsonPeer::RequestContext&, const proto::SignatureHelpParams&)
                        -> ipc::RequestResult<proto::SignatureHelpParams> {
        co_return proto::SignatureHelp{
            .signatures = {proto::SignatureInformation{
                .label = "void foo(int x)",
                .parameters = std::vector<proto::ParameterInformation>{{
                    .label =
                        proto::variant<proto::string, std::tuple<proto::uinteger, proto::uinteger>>(
                            std::string("int x")),
                }},
            }},
            .active_signature = 0u,
            .active_parameter = 0u,
        };
    });

    // textDocument/documentHighlight
    peer.on_request([](ipc::JsonPeer::RequestContext&, const proto::DocumentHighlightParams&)
                        -> ipc::RequestResult<proto::DocumentHighlightParams> {
        co_return std::vector<proto::DocumentHighlight>{
            {
             .range = make_range(0, 0, 5),
             .kind = proto::DocumentHighlightKind::Read,
             }
        };
    });

    // textDocument/rename
    peer.on_request([](ipc::JsonPeer::RequestContext&,
                       const proto::RenameParams& p) -> ipc::RequestResult<proto::RenameParams> {
        auto uri = p.text_document_position_params.text_document.uri;
        co_return proto::WorkspaceEdit{
            .changes =
                std::map<proto::DocumentUri, std::vector<proto::TextEdit>>{
                                                                           {uri, {{.range = make_range(0, 0, 3), .new_text = p.new_name}}},
                                                                           },
        };
    });

    // textDocument/prepareRename
    peer.on_request(
        [](ipc::JsonPeer::RequestContext&,
           const proto::PrepareRenameParams&) -> ipc::RequestResult<proto::PrepareRenameParams> {
            co_return proto::PrepareRenameResult{make_range(0, 0, 3)};
        });

    // textDocument/foldingRange
    peer.on_request(
        [](ipc::JsonPeer::RequestContext&,
           const proto::FoldingRangeParams&) -> ipc::RequestResult<proto::FoldingRangeParams> {
            co_return std::vector<proto::FoldingRange>{
                proto::FoldingRange{
                                    .start_line = 0,
                                    .end_line = 10,
                                    .kind = proto::FoldingRangeKind("region"),
                                    }
            };
        });

    // textDocument/selectionRange
    peer.on_request(
        [](ipc::JsonPeer::RequestContext&,
           const proto::SelectionRangeParams&) -> ipc::RequestResult<proto::SelectionRangeParams> {
            co_return std::vector<proto::SelectionRange>{
                {
                 .range = make_range(0, 0, 10),
                 }
            };
        });

    // textDocument/declaration
    peer.on_request(
        [](ipc::JsonPeer::RequestContext&,
           const proto::DeclarationParams& p) -> ipc::RequestResult<proto::DeclarationParams> {
            co_return proto::Declaration{
                proto::Location{
                                .uri = p.text_document_position_params.text_document.uri,
                                .range = make_range(5, 0, 10),
                                }
            };
        });

    // textDocument/typeDefinition
    peer.on_request([](ipc::JsonPeer::RequestContext&, const proto::TypeDefinitionParams& p)
                        -> ipc::RequestResult<proto::TypeDefinitionParams> {
        co_return proto::Definition{
            proto::Location{
                            .uri = p.text_document_position_params.text_document.uri,
                            .range = make_range(20, 0, 8),
                            }
        };
    });

    // textDocument/implementation
    peer.on_request([](ipc::JsonPeer::RequestContext&, const proto::ImplementationParams& p)
                        -> ipc::RequestResult<proto::ImplementationParams> {
        co_return proto::Definition{
            proto::Location{
                            .uri = p.text_document_position_params.text_document.uri,
                            .range = make_range(30, 0, 12),
                            }
        };
    });

    // textDocument/documentLink
    peer.on_request(
        [](ipc::JsonPeer::RequestContext&,
           const proto::DocumentLinkParams&) -> ipc::RequestResult<proto::DocumentLinkParams> {
            co_return std::vector<proto::DocumentLink>{
                {
                 .range = make_range(0, 0, 20),
                 .target = std::string("file:///linked"),
                 }
            };
        });

    // textDocument/codeLens
    peer.on_request([](ipc::JsonPeer::RequestContext&,
                       const proto::CodeLensParams&) -> ipc::RequestResult<proto::CodeLensParams> {
        co_return std::vector<proto::CodeLens>{
            {
             .range = make_range(0, 0, 10),
             .command = proto::Command{.title = "Run Test", .command = "test.run"},
             }
        };
    });

    // textDocument/inlayHint
    peer.on_request(
        [](ipc::JsonPeer::RequestContext&,
           const proto::InlayHintParams&) -> ipc::RequestResult<proto::InlayHintParams> {
            co_return std::vector<proto::InlayHint>{
                {
                 .position = {0, 5},
                 .label = std::string(": int"),
                 .kind = proto::InlayHintKind::Type,
                 }
            };
        });

    // textDocument/rangeFormatting
    peer.on_request([](ipc::JsonPeer::RequestContext&, const proto::DocumentRangeFormattingParams&)
                        -> ipc::RequestResult<proto::DocumentRangeFormattingParams> {
        co_return std::vector<proto::TextEdit>{
            {
             .range = make_range(0, 0, 0),
             .new_text = "range formatted\n",
             }
        };
    });

    // workspace/symbol
    peer.on_request([](ipc::JsonPeer::RequestContext&, const proto::WorkspaceSymbolParams&)
                        -> ipc::RequestResult<proto::WorkspaceSymbolParams> {
        co_return std::vector<proto::SymbolInformation>{
            {
             .name = "GlobalFunc",
             .kind = proto::SymbolKind::Function,
             .location = {.uri = "file:///test.cpp", .range = make_range(0, 0, 10)},
             }
        };
    });

    // textDocument/didOpen → publish diagnostics
    peer.on_notification([&](const proto::DidOpenTextDocumentParams& p) {
        peer.send_notification(proto::PublishDiagnosticsParams{
            .uri = p.text_document.uri,
            .diagnostics = {proto::Diagnostic{
                .range = make_range(0, 0, 5),
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
    return loop.run();
}
