#include <cstdint>
#include <string_view>

#include "kota/ipc/codec/json.h"
#include "kota/zest/zest.h"
#include "kota/ipc/lsp/protocol.h"

namespace kota::ipc::lsp {
namespace {

using codec::json::from_string;

TEST_SUITE(language_protocol_enums) {

TEST_CASE(unknown_string_enum_value) {
    auto content =
        from_string<protocol::MarkupContent, lsp_config>(R"({"kind":"asciidoc","value":"body"})");
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(content->kind, "asciidoc");
    EXPECT_EQ(content->value, "body");
}

TEST_CASE(known_string_enum_value) {
    auto content =
        from_string<protocol::MarkupContent, lsp_config>(R"({"kind":"markdown","value":"body"})");
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(content->kind, protocol::MarkupKind::Markdown);
}

TEST_CASE(string_enum_round_trip) {
    protocol::MarkupContent content{
        .kind = protocol::MarkupKind::Markdown,
        .value = "body",
    };
    auto serialized = codec::json::to_string<lsp_config>(content);
    ASSERT_TRUE(serialized.has_value());
    EXPECT_EQ(*serialized, R"({"kind":"markdown","value":"body"})");
}

TEST_CASE(unknown_value_round_trip) {
    constexpr std::string_view payload = R"({"kind":"asciidoc","value":"body"})";
    auto content = from_string<protocol::MarkupContent, lsp_config>(payload);
    ASSERT_TRUE(content.has_value());
    auto serialized = codec::json::to_string<lsp_config>(*content);
    ASSERT_TRUE(serialized.has_value());
    EXPECT_EQ(*serialized, payload);
}

TEST_CASE(int_enum_unknown_encode) {
    protocol::ClientSymbolKindOptions options{
        .value_set = std::vector{protocol::SymbolKind::File, protocol::SymbolKind(9999)},
    };
    auto serialized = codec::json::to_string<lsp_config>(options);
    ASSERT_TRUE(serialized.has_value());
    EXPECT_EQ(*serialized, R"({"valueSet":[1,9999]})");
}

TEST_CASE(string_literal_field_default) {
    auto serialized = codec::json::to_string<lsp_config>(protocol::FullDocumentDiagnosticReport{});
    ASSERT_TRUE(serialized.has_value());
    EXPECT_EQ(*serialized, R"({"kind":"full","items":[]})");
}

TEST_CASE(initialize_unknown_enum_values) {
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
    ASSERT_TRUE(params.has_value());

    auto& init = *params;
    auto& caps = init.capabilities;

    ASSERT_TRUE(caps.workspace.has_value());
    ASSERT_TRUE(caps.workspace->workspace_edit.has_value());
    auto& edit = *caps.workspace->workspace_edit;
    ASSERT_TRUE(edit.resource_operations.has_value());
    ASSERT_EQ(edit.resource_operations->size(), 4U);
    EXPECT_EQ((*edit.resource_operations)[0], protocol::ResourceOperationKind::Create);
    EXPECT_EQ((*edit.resource_operations)[3], "futureOperation");
    ASSERT_TRUE(edit.failure_handling.has_value());
    EXPECT_EQ(*edit.failure_handling, "futureFailureMode");

    ASSERT_TRUE(caps.workspace->symbol.has_value());
    ASSERT_TRUE(caps.workspace->symbol->symbol_kind.has_value());
    auto& kinds = caps.workspace->symbol->symbol_kind->value_set;
    ASSERT_TRUE(kinds.has_value());
    ASSERT_EQ(kinds->size(), 3U);
    EXPECT_EQ((*kinds)[0], protocol::SymbolKind::File);
    EXPECT_EQ(static_cast<std::uint32_t>((*kinds)[1]), 9999U);
    EXPECT_EQ(static_cast<std::uint32_t>((*kinds)[2]), 4000000000U);

    ASSERT_TRUE(caps.text_document.has_value());
    ASSERT_TRUE(caps.text_document->hover.has_value());
    auto& formats = caps.text_document->hover->content_format;
    ASSERT_TRUE(formats.has_value());
    EXPECT_EQ((*formats)[1], "asciidoc");

    ASSERT_TRUE(init.trace.has_value());
    EXPECT_EQ(*init.trace, "compact");
}

};  // TEST_SUITE(language_protocol_enums)

}  // namespace
}  // namespace kota::ipc::lsp
