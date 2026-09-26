#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "kota/ipc/codec/json.h"
#include "kota/zest/zest.h"
#include "kota/ipc/lsp/protocol.h"

namespace kota::ipc::lsp {
namespace {

using codec::json::from_string;
using codec::json::to_string;

constexpr std::string_view range_json =
    R"({"start":{"line":0,"character":0},"end":{"line":0,"character":1}})";

ZEST_SUITE(ipc_lsp_protocol_model) {

ZEST_CASE(literal_encodes_its_text) {
    auto serialized = to_string<lsp_config>(protocol::CreateFile{.uri = "file:///a"});
    ASSERT(serialized);
    EXPECT(*serialized == R"({"kind":"create","uri":"file:///a"})");
}

ZEST_CASE(literal_rejects_other_input) {
    for(auto payload: {R"({"kind":"delete","uri":"file:///a"})",
                       R"({"kind":1,"uri":"file:///a"})",
                       R"({"uri":"file:///a"})"}) {
        EXPECT(!(from_string<protocol::CreateFile, lsp_config>(payload).has_value()));
    }
}

ZEST_CASE(literal_selects_variant_alternative) {
    // CreateFile and DeleteFile have the same shape; only `kind` tells them apart.
    auto edit = from_string<protocol::WorkspaceEdit, lsp_config>(R"({"documentChanges":[
        {"kind":"create","uri":"file:///a"},
        {"kind":"rename","oldUri":"file:///a","newUri":"file:///b"},
        {"kind":"delete","uri":"file:///b"}
    ]})");
    ASSERT(edit);
    ASSERT(edit->document_changes);
    auto& changes = *edit->document_changes;
    ASSERT(changes.size() == 3U);
    EXPECT(std::holds_alternative<protocol::CreateFile>(changes[0]));
    EXPECT(std::holds_alternative<protocol::RenameFile>(changes[1]));
    EXPECT(std::holds_alternative<protocol::DeleteFile>(changes[2]));
}

ZEST_CASE(variant_alternatives_by_required_members) {
    auto unchanged = from_string<protocol::DocumentDiagnosticReport, lsp_config>(
        R"({"kind":"unchanged","resultId":"1"})");
    ASSERT(unchanged);
    EXPECT(std::holds_alternative<protocol::RelatedUnchangedDocumentDiagnosticReport>(*unchanged));

    using Change = protocol::TextDocumentContentChangeEvent;
    auto whole = from_string<Change, lsp_config>(R"({"text":"x"})");
    ASSERT(whole);
    EXPECT(std::holds_alternative<protocol::TextDocumentContentChangeWholeDocument>(*whole));

    auto partial =
        from_string<Change, lsp_config>(std::format(R"({{"range":{},"text":"x"}})", range_json));
    ASSERT(partial);
    EXPECT(std::holds_alternative<protocol::TextDocumentContentChangePartial>(*partial));
}

ZEST_CASE(empty_object_alternative) {
    constexpr std::string_view legend = R"("legend":{"tokenTypes":[],"tokenModifiers":[]})";
    auto options = from_string<protocol::SemanticTokensOptions, lsp_config>(
        std::format(R"({{{},"range":{{}},"full":true}})", legend));
    ASSERT(options);
    ASSERT(options->range);
    EXPECT(std::holds_alternative<protocol::EmptyObject>(*options->range));
    ASSERT(options->full);
    EXPECT(std::holds_alternative<protocol::boolean>(*options->full));

    auto serialized = to_string<lsp_config>(
        protocol::SemanticTokensOptions{.legend = {}, .range = protocol::EmptyObject{}});
    ASSERT(serialized);
    EXPECT(*serialized == std::format(R"({{{},"range":{{}}}})", legend));
}

ZEST_CASE(optional_bool_reads_absence_as_false) {
    auto item = from_string<protocol::CompletionItem, lsp_config>(R"({"label":"a"})");
    ASSERT(item);
    EXPECT(!item->preselect);

    auto preselected =
        to_string<lsp_config>(protocol::CompletionItem{.label = "a", .preselect = true});
    ASSERT(preselected);
    EXPECT(*preselected == R"({"label":"a","preselect":true})");

    auto plain = to_string<lsp_config>(protocol::CompletionItem{.label = "a", .preselect = false});
    ASSERT(plain);
    EXPECT(*plain == R"({"label":"a"})");
}

ZEST_CASE(nullable_member_encodes_null) {
    auto serialized = to_string<lsp_config>(protocol::InitializeParams{});
    ASSERT(serialized);
    EXPECT(*serialized == R"({"processId":null,"rootUri":null,"capabilities":{}})");
}

ZEST_CASE(inherited_properties_are_members) {
    auto params = from_string<protocol::HoverParams, lsp_config>(
        R"({"textDocument":{"uri":"file:///a"},"position":{"line":1,"character":2},"workDoneToken":"t"})");
    ASSERT(params);
    EXPECT(params->text_document.uri == "file:///a");
    EXPECT(params->position.character == 2U);
    ASSERT(params->work_done_token);
    EXPECT(std::get<std::string>(*params->work_done_token) == "t");
}

ZEST_CASE(self_containing_structure_round_trip) {
    constexpr protocol::Range range{
        .start = {.line = 0, .character = 0},
        .end = {.line = 0, .character = 1}
    };
    protocol::SelectionRange selection{
        .range = range,
        .parent =
            std::make_unique<protocol::SelectionRange>(protocol::SelectionRange{.range = range}),
    };
    auto payload = std::format(R"({{"range":{0},"parent":{{"range":{0}}}}})", range_json);
    auto serialized = to_string<lsp_config>(selection);
    ASSERT(serialized);
    EXPECT(*serialized == payload);

    auto decoded = from_string<protocol::SelectionRange, lsp_config>(payload);
    ASSERT(decoded);
    ASSERT(decoded->parent != nullptr);
    EXPECT(decoded->parent->parent == nullptr);
    EXPECT(decoded->parent->range.end.character == 1U);
}

ZEST_CASE(nested_structure_round_trip) {
    auto payload = std::format(
        R"({{"name":"outer","kind":5,"range":{0},"selectionRange":{0},"children":[{{"name":"inner","kind":6,"range":{0},"selectionRange":{0}}}]}})",
        range_json);
    auto symbol = from_string<protocol::DocumentSymbol, lsp_config>(payload);
    ASSERT(symbol);
    ASSERT(symbol->children);
    ASSERT(symbol->children->size() == 1U);
    EXPECT((*symbol->children)[0].name == "inner");

    auto serialized = to_string<lsp_config>(*symbol);
    ASSERT(serialized);
    EXPECT(*serialized == payload);
}

ZEST_CASE(lsp_any_is_a_dynamic_value) {
    constexpr std::string_view payload =
        R"({"title":"t","command":"c","arguments":[1,"two",{"three":3.5},null,true]})";
    auto command = from_string<protocol::Command, lsp_config>(payload);
    ASSERT(command);
    ASSERT(command->arguments);
    auto& arguments = *command->arguments;
    ASSERT(arguments.size() == 5U);
    EXPECT(arguments[0].get_int() == 1);
    EXPECT(arguments[1].get_string() == "two");
    EXPECT(arguments[2]["three"].get_double() == 3.5);
    EXPECT(arguments[3].is_null());
    EXPECT(arguments[4].get_bool() == true);

    auto serialized = to_string<lsp_config>(*command);
    ASSERT(serialized);
    EXPECT(*serialized == payload);
}

ZEST_CASE(lsp_any_keys_are_not_renamed) {
    // lsp_config renames members to lowerCamel; dynamic object keys are data.
    constexpr std::string_view payload =
        R"({"title":"t","command":"c","arguments":[{"snake_key":1,"PascalKey":2}]})";
    auto command = from_string<protocol::Command, lsp_config>(payload);
    ASSERT(command);
    auto serialized = to_string<lsp_config>(*command);
    ASSERT(serialized);
    EXPECT(*serialized == payload);
}

ZEST_CASE(nullable_result) {
    using Result = protocol::RequestTraits<protocol::CompletionParams>::Result;

    auto none = from_string<Result, lsp_config>("null");
    ASSERT(none);
    EXPECT(!none->has_value());

    auto list = from_string<Result, lsp_config>(R"({"isIncomplete":true,"items":[]})");
    ASSERT(list);
    ASSERT(list->has_value());
    EXPECT(std::holds_alternative<protocol::CompletionList>(**list));
}

ZEST_CASE(parameterless_methods) {
    static_assert(
        std::is_same_v<protocol::RequestTraits<protocol::ShutdownParams>::Result, protocol::null>);
    EXPECT(protocol::RequestTraits<protocol::ShutdownParams>::method == "shutdown");
    EXPECT(protocol::NotificationTraits<protocol::ExitParams>::method == "exit");
}

};  // ZEST_SUITE(ipc_lsp_protocol_model)

}  // namespace
}  // namespace kota::ipc::lsp
