#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "handlers.h"
#include "kota/ipc/codec/json.h"
#include "kota/ipc/lsp/progress.h"
#include "kota/ipc/lsp/protocol.h"

namespace lsp = kota::ipc::lsp;
namespace proto = kota::ipc::protocol;

namespace kota::test {

void add_language_features(ipc::JSONPeer& peer) {
    // textDocument/hover
    peer.on_request([](ipc::JSONPeer::RequestContext&,
                       const proto::HoverParams& p) -> ipc::RequestResult<proto::HoverParams> {
        auto uri = p.text_document.uri;
        if(uri == "file:///error") {
            co_return kota::outcome_error(
                proto::Error(static_cast<proto::integer>(proto::LSPErrorCodes::RequestFailed),
                             "hover error triggered"));
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
        [&](ipc::JSONPeer::RequestContext&,
            const proto::CompletionParams& p) -> ipc::RequestResult<proto::CompletionParams> {
            auto uri = p.text_document.uri;
            if(uri == "file:///progress") {
                lsp::ProgressReporter reporter(peer,
                                               proto::ProgressToken(std::string("test-progress")));
                auto create_result = co_await reporter.create();
                if(create_result.has_error()) {
                    co_return kota::outcome_error(create_result.error());
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
        [](ipc::JSONPeer::RequestContext&,
           const proto::DefinitionParams& p) -> ipc::RequestResult<proto::DefinitionParams> {
            co_return proto::Definition{
                proto::Location{
                                .uri = p.text_document.uri,
                                .range = make_range(10, 0, 5),
                                }
            };
        });

    // textDocument/references
    peer.on_request(
        [](ipc::JSONPeer::RequestContext&,
           const proto::ReferenceParams& p) -> ipc::RequestResult<proto::ReferenceParams> {
            auto uri = p.text_document.uri;
            co_return std::vector<proto::Location>{
                {uri, make_range(1, 0, 3)},
                {uri, make_range(5, 0, 3)}
            };
        });

    // textDocument/documentSymbol
    peer.on_request(
        [](ipc::JSONPeer::RequestContext&,
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
    peer.on_request([](ipc::JSONPeer::RequestContext&, const proto::DocumentFormattingParams&)
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
        [](ipc::JSONPeer::RequestContext&,
           const proto::CodeActionParams&) -> ipc::RequestResult<proto::CodeActionParams> {
            co_return std::vector<proto::variant<proto::Command, proto::CodeAction>>{
                proto::CodeAction{
                                  .title = "stub action",
                                  .kind = proto::CodeActionKind("quickfix"),
                                  },
            };
        });

    // textDocument/signatureHelp
    peer.on_request([](ipc::JSONPeer::RequestContext&, const proto::SignatureHelpParams&)
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
    peer.on_request([](ipc::JSONPeer::RequestContext&, const proto::DocumentHighlightParams&)
                        -> ipc::RequestResult<proto::DocumentHighlightParams> {
        co_return std::vector<proto::DocumentHighlight>{
            {
             .range = make_range(0, 0, 5),
             .kind = proto::DocumentHighlightKind::Read,
             }
        };
    });

    // textDocument/rename
    peer.on_request([](ipc::JSONPeer::RequestContext&,
                       const proto::RenameParams& p) -> ipc::RequestResult<proto::RenameParams> {
        auto uri = p.text_document.uri;
        co_return proto::WorkspaceEdit{
            .changes =
                std::map<proto::DocumentUri, std::vector<proto::TextEdit>>{
                                                                           {uri, {{.range = make_range(0, 0, 3), .new_text = p.new_name}}},
                                                                           },
        };
    });

    // textDocument/prepareRename
    peer.on_request(
        [](ipc::JSONPeer::RequestContext&,
           const proto::PrepareRenameParams&) -> ipc::RequestResult<proto::PrepareRenameParams> {
            co_return proto::PrepareRenameResult{make_range(0, 0, 3)};
        });

    // textDocument/foldingRange
    peer.on_request(
        [](ipc::JSONPeer::RequestContext&,
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
        [](ipc::JSONPeer::RequestContext&,
           const proto::SelectionRangeParams&) -> ipc::RequestResult<proto::SelectionRangeParams> {
            // SelectionRange owns its parent, so it is move-only, and an
            // initializer_list can only be copied from.
            std::vector<proto::SelectionRange> ranges;
            ranges.push_back({.range = make_range(0, 0, 10)});
            co_return ranges;
        });

    // textDocument/declaration
    peer.on_request(
        [](ipc::JSONPeer::RequestContext&,
           const proto::DeclarationParams& p) -> ipc::RequestResult<proto::DeclarationParams> {
            co_return proto::Declaration{
                proto::Location{
                                .uri = p.text_document.uri,
                                .range = make_range(5, 0, 10),
                                }
            };
        });

    // textDocument/typeDefinition
    peer.on_request([](ipc::JSONPeer::RequestContext&, const proto::TypeDefinitionParams& p)
                        -> ipc::RequestResult<proto::TypeDefinitionParams> {
        co_return proto::Definition{
            proto::Location{
                            .uri = p.text_document.uri,
                            .range = make_range(20, 0, 8),
                            }
        };
    });

    // textDocument/implementation
    peer.on_request([](ipc::JSONPeer::RequestContext&, const proto::ImplementationParams& p)
                        -> ipc::RequestResult<proto::ImplementationParams> {
        co_return proto::Definition{
            proto::Location{
                            .uri = p.text_document.uri,
                            .range = make_range(30, 0, 12),
                            }
        };
    });

    // textDocument/documentLink
    peer.on_request(
        [](ipc::JSONPeer::RequestContext&,
           const proto::DocumentLinkParams&) -> ipc::RequestResult<proto::DocumentLinkParams> {
            co_return std::vector<proto::DocumentLink>{
                {
                 .range = make_range(0, 0, 20),
                 .target = std::string("file:///linked"),
                 }
            };
        });

    // textDocument/codeLens
    peer.on_request([](ipc::JSONPeer::RequestContext&,
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
        [](ipc::JSONPeer::RequestContext&,
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
    peer.on_request([](ipc::JSONPeer::RequestContext&, const proto::DocumentRangeFormattingParams&)
                        -> ipc::RequestResult<proto::DocumentRangeFormattingParams> {
        co_return std::vector<proto::TextEdit>{
            {
             .range = make_range(0, 0, 0),
             .new_text = "range formatted\n",
             }
        };
    });

    // workspace/symbol
    peer.on_request([](ipc::JSONPeer::RequestContext&, const proto::WorkspaceSymbolParams&)
                        -> ipc::RequestResult<proto::WorkspaceSymbolParams> {
        co_return std::vector<proto::SymbolInformation>{
            {
             .name = "GlobalFunc",
             .kind = proto::SymbolKind::Function,
             .location = {.uri = "file:///test.cpp", .range = make_range(0, 0, 10)},
             }
        };
    });
}

}  // namespace kota::test
