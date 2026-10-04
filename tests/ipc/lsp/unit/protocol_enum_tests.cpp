#include <cstdint>
#include <string_view>

#include "kota/ipc/codec/json.h"
#include "kota/zest/zest.h"
#include "kota/ipc/lsp/protocol.h"

namespace kota::ipc::lsp {
namespace {

using codec::json::from_string;

ZEST_SUITE(ipc_lsp_protocol_enum) {

ZEST_CASE(unknown_string_enum_value) {
    auto content =
        from_string<protocol::MarkupContent, lsp_config>(R"({"kind":"asciidoc","value":"body"})");
    ZASSERT(content);
    ZEXPECT(content->kind == "asciidoc");
    ZEXPECT(content->value == "body");
}

ZEST_CASE(known_string_enum_value) {
    auto content =
        from_string<protocol::MarkupContent, lsp_config>(R"({"kind":"markdown","value":"body"})");
    ZASSERT(content);
    ZEXPECT(content->kind == protocol::MarkupKind::Markdown);
}

ZEST_CASE(string_enum_roundtrip) {
    protocol::MarkupContent content{
        .kind = protocol::MarkupKind::Markdown,
        .value = "body",
    };
    auto serialized = codec::json::to_string<lsp_config>(content);
    ZASSERT(serialized);
    ZEXPECT(*serialized == R"({"kind":"markdown","value":"body"})");
}

ZEST_CASE(unknown_value_roundtrip) {
    constexpr std::string_view payload = R"({"kind":"asciidoc","value":"body"})";
    auto content = from_string<protocol::MarkupContent, lsp_config>(payload);
    ZASSERT(content);
    auto serialized = codec::json::to_string<lsp_config>(*content);
    ZASSERT(serialized);
    ZEXPECT(*serialized == payload);
}

ZEST_CASE(int_enum_unknown_encode) {
    protocol::ClientSymbolKindOptions options{
        .value_set = std::vector{protocol::SymbolKind::File, protocol::SymbolKind(9999)},
    };
    auto serialized = codec::json::to_string<lsp_config>(options);
    ZASSERT(serialized);
    ZEXPECT(*serialized == R"({"valueSet":[1,9999]})");
}

ZEST_CASE(string_literal_field_default) {
    auto serialized = codec::json::to_string<lsp_config>(protocol::FullDocumentDiagnosticReport{});
    ZASSERT(serialized);
    ZEXPECT(*serialized == R"({"kind":"full","items":[]})");
}

ZEST_CASE(initialize_unknown_enum_values) {
    constexpr std::string_view payload = R"({
        "processId": 12345,
        "rootUri": null,
        "capabilities": {
            "workspace": {
                "workspaceEdit": {
                    "resourceOperations": ["create", "rename", "delete", "futureOperation"],
                    "failureHandling": "futureFailureMode"
                },
                "symbol": {
                    "symbolKind": {"valueSet": [1, 9999, 4000000000]}
                }
            },
            "textDocument": {
                "hover": {"contentFormat": ["markdown", "asciidoc"]}
            },
            "unknownCapability": {"nested": [1, 2, 3]}
        },
        "trace": "compact"
    })";

    auto params = from_string<protocol::InitializeParams, lsp_config>(payload);
    ZASSERT(params);

    auto& init = *params;
    auto& caps = init.capabilities;

    ZASSERT(caps.workspace);
    ZASSERT(caps.workspace->workspace_edit);
    auto& edit = *caps.workspace->workspace_edit;
    ZASSERT(edit.resource_operations);
    ZASSERT(edit.resource_operations->size() == 4U);
    ZEXPECT((*edit.resource_operations)[0] == protocol::ResourceOperationKind::Create);
    ZEXPECT((*edit.resource_operations)[3] == "futureOperation");
    ZASSERT(edit.failure_handling);
    ZEXPECT(*edit.failure_handling == "futureFailureMode");

    ZASSERT(caps.workspace->symbol);
    ZASSERT(caps.workspace->symbol->symbol_kind);
    auto& kinds = caps.workspace->symbol->symbol_kind->value_set;
    ZASSERT(kinds);
    ZASSERT(kinds->size() == 3U);
    ZEXPECT((*kinds)[0] == protocol::SymbolKind::File);
    ZEXPECT(static_cast<std::uint32_t>((*kinds)[1]) == 9999U);
    ZEXPECT(static_cast<std::uint32_t>((*kinds)[2]) == 4000000000U);

    ZASSERT(caps.text_document);
    ZASSERT(caps.text_document->hover);
    auto& formats = caps.text_document->hover->content_format;
    ZASSERT(formats);
    ZEXPECT((*formats)[1] == "asciidoc");

    ZASSERT(init.trace);
    ZEXPECT(*init.trace == "compact");
}

};  // ZEST_SUITE(ipc_lsp_protocol_enum)

}  // namespace
}  // namespace kota::ipc::lsp
