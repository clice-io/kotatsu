#include <cstddef>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "kota/ipc/codec.h"
#include "kota/ipc/codec/json.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/codec/bincode/bincode.h"
#include "kota/codec/dyn/dyn.h"
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

ZEST_CASE(literal_of_other_text_fails) {
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

ZEST_CASE(self_containing_structure_roundtrip) {
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

ZEST_CASE(nested_structure_roundtrip) {
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
    EXPECT(
        zest::type_eq<protocol::RequestTraits<protocol::ShutdownParams>::Result, protocol::null>());
    EXPECT(protocol::RequestTraits<protocol::ShutdownParams>::method == "shutdown");
    EXPECT(protocol::NotificationTraits<protocol::ExitParams>::method == "exit");
}

// Clients send the params of shutdown and exit as null, or leave them out.
ZEST_CASE(parameterless_methods_read_null_params) {
    JsonCodec codec;
    for(std::string_view payload: {
            R"({"jsonrpc":"2.0","id":1,"method":"shutdown","params":null})",
            R"({"jsonrpc":"2.0","id":1,"method":"shutdown"})",
        }) {
        ZEST_CONTEXT("payload: {}", payload);
        auto parsed = codec.parse_message(payload);
        const auto* request = std::get_if<IncomingRequest>(&parsed);
        ASSERT(request != nullptr);
        EXPECT(codec.deserialize_value<protocol::ShutdownParams>(request->params).has_value());
    }
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","method":"exit","params":null})");
    const auto* notification = std::get_if<IncomingNotification>(&parsed);
    ASSERT(notification != nullptr);
    EXPECT(codec.deserialize_value<protocol::ExitParams>(notification->params).has_value());
}

// An untagged variant lists a structure before the one it derives from, so
// an edit with an annotationId reads as the AnnotatedTextEdit it is, not as a
// TextEdit that drops the id.
ZEST_CASE(untagged_variant_takes_the_alternative_the_input_fills) {
    auto edit = from_string<protocol::TextDocumentEdit, lsp_config>(std::format(
        R"({{"textDocument":{{"uri":"file:///a","version":null}},"edits":[{{"range":{},"newText":"x","annotationId":"a"}}]}})",
        range_json));
    ASSERT(edit);
    ASSERT(edit->edits.size() == 1U);
    EXPECT(std::holds_alternative<protocol::AnnotatedTextEdit>(edit->edits[0]));
}

// In a report, `cancellable: false` disables the cancel button, where leaving
// it out keeps the button as it is, so false is written.
ZEST_CASE(tri_state_bool_writes_false) {
    auto serialized = to_string<lsp_config>(protocol::WorkDoneProgressReport{.cancellable = false});
    ASSERT(serialized);
    EXPECT(zest::contains(*serialized, R"("cancellable":false)"));
}

// An optional member whose type admits null reads null as present.
ZEST_CASE(optional_nullable_member_reads_null_as_present) {
    auto params = from_string<protocol::InitializeParams, lsp_config>(
        R"({"processId":null,"rootUri":null,"capabilities":{},"workspaceFolders":null})");
    ASSERT(params);
    ASSERT(params->workspace_folders.has_value());
    EXPECT(!params->workspace_folders->has_value());
}

// So is an optional LSPAny, such as `data`: an item carries its null back
// to the server.
ZEST_CASE(optional_any_member_reads_null_as_present) {
    auto item = from_string<protocol::CompletionItem, lsp_config>(R"({"label":"a","data":null})");
    ASSERT(item);
    ASSERT(item->data.has_value());
    EXPECT(item->data->is_null());
    auto written = to_string<lsp_config>(*item);
    ASSERT(written);
    EXPECT(*written == R"({"label":"a","data":null})");
}

// A required `T | null` must be present: null is a value, absent is not.
ZEST_CASE(required_nullable_member_absent_fails) {
    auto params = from_string<protocol::InitializeParams, lsp_config>(
        R"({"rootUri":null,"capabilities":{}})");
    ASSERT(!params);
    EXPECT(zest::contains(params.error().to_string(), "processId"));
}

// Real LSP payloads nest up to about 130 levels: a SelectionRange parent
// chain, say.
ZEST_CASE(nesting_of_real_payloads_is_read) {
    JsonCodec codec;
    std::string chain;
    for(int level = 0; level < 125; ++level) {
        chain += R"({"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":1}})";
        chain += level + 1 < 125 ? R"(,"parent":)" : "";
    }
    chain += std::string(125, '}');
    auto parsed =
        codec.parse_message(std::format(R"({{"jsonrpc":"2.0","id":1,"result":[{}]}})", chain));
    const auto* response = std::get_if<IncomingResponse>(&parsed);
    ASSERT(response != nullptr);
    auto ranges = codec.deserialize_value<std::vector<protocol::SelectionRange>>(response->result);
    ASSERT(ranges.has_value());
    EXPECT(ranges->size() == 1U);
}

// Bincode writes every field, an absent optional member's too, and reads its
// three states back.
ZEST_CASE(optional_nullable_member_roundtrips_through_bincode) {
    for(auto active: {protocol::optional_nullable<protocol::nullable<protocol::uinteger>>{},
                      protocol::optional_nullable<protocol::nullable<protocol::uinteger>>{
                          protocol::nullable<protocol::uinteger>{}},
                      protocol::optional_nullable<protocol::nullable<protocol::uinteger>>{
                          protocol::nullable<protocol::uinteger>{2U}}}) {
        protocol::SignatureInformation info{.label = "f(int)", .active_parameter = active};
        auto bytes = codec::bincode::to_bytes(info);
        ASSERT(bytes);
        protocol::SignatureInformation back{};
        ASSERT(codec::bincode::from_bytes(std::span<const std::byte>(*bytes), back));
        EXPECT((back.active_parameter == info.active_parameter));
    }
}

// Diagnostics through bincode, each with its LSPAny `data` next to other
// optional members, some set and some left out: `data` absent, null and an
// object each read back as they were.
ZEST_CASE(diagnostic_data_roundtrips_through_bincode) {
    using Data = protocol::optional_nullable<protocol::LSPAny>;
    std::vector<protocol::Diagnostic> diagnostics;
    for(auto data: {Data{},
                    Data{protocol::LSPAny{}},
                    Data{protocol::LSPAny{
                        {"fix", "remove"},
                        {"index", 3},
                    }}}) {
        diagnostics.push_back(protocol::Diagnostic{
            .range = {.start = {.line = 3, .character = 4}, .end = {.line = 3, .character = 9}},
            .severity = protocol::DiagnosticSeverity::Warning,
            .code = "unused-variable",
            .source = "clang",
            .message = "unused variable 'x'",
            .tags = std::vector{protocol::DiagnosticTag::Unnecessary},
            .data = std::move(data),
        });
    }

    auto bytes = codec::bincode::to_bytes(diagnostics);
    ASSERT(bytes);
    std::vector<protocol::Diagnostic> back;
    ASSERT(codec::bincode::from_bytes(std::span<const std::byte>(*bytes), back));
    ASSERT(back.size() == diagnostics.size());
    // The generated structures have no operator==, and meta cannot compare
    // their optional members: each member is compared on its own.
    for(std::size_t i = 0; i < back.size(); ++i) {
        ZEST_CONTEXT("diagnostic {}", i);
        EXPECT((back[i].data == diagnostics[i].data));
        EXPECT(back[i].range == diagnostics[i].range);
        EXPECT((back[i].severity == diagnostics[i].severity));
        EXPECT((back[i].code == diagnostics[i].code));
        EXPECT(!back[i].code_description.has_value());
        EXPECT((back[i].source == diagnostics[i].source));
        const auto* message = std::get_if<std::string>(&back[i].message);
        ASSERT(message != nullptr);
        EXPECT(*message == "unused variable 'x'");
        EXPECT((back[i].tags == diagnostics[i].tags));
        EXPECT(!back[i].related_information.has_value());
    }
}

// dyn has no format of its own: a present null stays present through it.
ZEST_CASE(optional_nullable_member_roundtrips_through_dyn) {
    for(auto active: {protocol::optional_nullable<protocol::nullable<protocol::uinteger>>{},
                      protocol::optional_nullable<protocol::nullable<protocol::uinteger>>{
                          protocol::nullable<protocol::uinteger>{}},
                      protocol::optional_nullable<protocol::nullable<protocol::uinteger>>{
                          protocol::nullable<protocol::uinteger>{2U}}}) {
        protocol::SignatureInformation info{.label = "f(int)", .active_parameter = active};
        auto value = codec::dyn::to_dyn(info);
        ASSERT(value);
        auto back = codec::dyn::from_dyn<protocol::SignatureInformation>(*value);
        ASSERT(back);
        EXPECT((back->active_parameter == info.active_parameter));
    }
}

struct NullableMembers {
    protocol::nullable<protocol::integer> required;
    protocol::optional_nullable<protocol::nullable<protocol::integer>> optional;

    bool operator==(const NullableMembers&) const = default;
};

ZEST_CASE(nullable_members_compare) {
    NullableMembers a{.required = 1, .optional = protocol::nullable<protocol::integer>{}};
    NullableMembers b = a;
    EXPECT((a == b));
    b.optional.reset();
    EXPECT(!(a == b));
}

};  // ZEST_SUITE(ipc_lsp_protocol_model)

}  // namespace
}  // namespace kota::ipc::lsp
