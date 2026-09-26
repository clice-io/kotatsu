#include <cstdint>
#include <string_view>

#include "kota/ipc/codec/json.h"
#include "kota/zest/zest.h"
#include "kota/ipc/lsp/protocol.h"

namespace kota::ipc::lsp {
namespace {

using codec::json::from_string;

ZEST_SUITE(language_protocol_enums) {

ZEST_CASE(unknown_string_enum_value) {
    auto content =
        from_string<protocol::MarkupContent, lsp_config>(R"({"kind":"asciidoc","value":"body"})");
    ASSERT(content);
    EXPECT(content->kind == "asciidoc");
    EXPECT(content->value == "body");
}

ZEST_CASE(known_string_enum_value) {
    auto content =
        from_string<protocol::MarkupContent, lsp_config>(R"({"kind":"markdown","value":"body"})");
    ASSERT(content);
    EXPECT(content->kind == protocol::MarkupKind::Markdown);
}

ZEST_CASE(string_enum_round_trip) {
    protocol::MarkupContent content{
        .kind = protocol::MarkupKind::Markdown,
        .value = "body",
    };
    auto serialized = codec::json::to_string<lsp_config>(content);
    ASSERT(serialized);
    EXPECT(*serialized == R"({"kind":"markdown","value":"body"})");
}

ZEST_CASE(unknown_value_round_trip) {
    constexpr std::string_view payload = R"({"kind":"asciidoc","value":"body"})";
    auto content = from_string<protocol::MarkupContent, lsp_config>(payload);
    ASSERT(content);
    auto serialized = codec::json::to_string<lsp_config>(*content);
    ASSERT(serialized);
    EXPECT(*serialized == payload);
}

ZEST_CASE(int_enum_unknown_encode) {
    protocol::ClientSymbolKindOptions options{
        .value_set = std::vector{protocol::SymbolKind::File, protocol::SymbolKind(9999)},
    };
    auto serialized = codec::json::to_string<lsp_config>(options);
    ASSERT(serialized);
    EXPECT(*serialized == R"({"valueSet":[1,9999]})");
}

ZEST_CASE(string_literal_field_default) {
    auto serialized = codec::json::to_string<lsp_config>(protocol::FullDocumentDiagnosticReport{});
    ASSERT(serialized);
    EXPECT(*serialized == R"({"kind":"full","items":[]})");
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
    ASSERT(params);

    auto& init = *params;
    auto& caps = init.capabilities;

    ASSERT(caps.workspace);
    ASSERT(caps.workspace->workspace_edit);
    auto& edit = *caps.workspace->workspace_edit;
    ASSERT(edit.resource_operations);
    ASSERT(edit.resource_operations->size() == 4U);
    EXPECT((*edit.resource_operations)[0] == protocol::ResourceOperationKind::Create);
    EXPECT((*edit.resource_operations)[3] == "futureOperation");
    ASSERT(edit.failure_handling);
    EXPECT(*edit.failure_handling == "futureFailureMode");

    ASSERT(caps.workspace->symbol);
    ASSERT(caps.workspace->symbol->symbol_kind);
    auto& kinds = caps.workspace->symbol->symbol_kind->value_set;
    ASSERT(kinds);
    ASSERT(kinds->size() == 3U);
    EXPECT((*kinds)[0] == protocol::SymbolKind::File);
    EXPECT(static_cast<std::uint32_t>((*kinds)[1]) == 9999U);
    EXPECT(static_cast<std::uint32_t>((*kinds)[2]) == 4000000000U);

    ASSERT(caps.text_document);
    ASSERT(caps.text_document->hover);
    auto& formats = caps.text_document->hover->content_format;
    ASSERT(formats);
    EXPECT((*formats)[1] == "asciidoc");

    ASSERT(init.trace);
    EXPECT(*init.trace == "compact");
}

};  // ZEST_SUITE(language_protocol_enums)

}  // namespace
}  // namespace kota::ipc::lsp
