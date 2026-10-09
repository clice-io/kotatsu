#include <cstddef>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

// The methods without params, such as the refresh requests, say so in their
// traits: Peer sends them none, as other clients and servers expect.
ZEST_CASE(methods_without_params_say_so) {
    ZSTATIC_EXPECT(!protocol::RequestTraits<protocol::ShutdownParams>::takes_params);
    ZSTATIC_EXPECT(!protocol::RequestTraits<protocol::CodeLensRefreshParams>::takes_params);
    ZSTATIC_EXPECT(!protocol::NotificationTraits<protocol::ExitParams>::takes_params);
}

ZEST_CASE(literal_encodes_its_text) {
    auto serialized = to_string<lsp_config>(protocol::CreateFile{.uri = "file:///a"});
    ZASSERT(serialized);
    ZEXPECT(*serialized == R"({"kind":"create","uri":"file:///a"})");
}

ZEST_CASE(literal_of_other_text_fails) {
    for(auto payload: {R"({"kind":"delete","uri":"file:///a"})",
                       R"({"kind":1,"uri":"file:///a"})",
                       R"({"uri":"file:///a"})"}) {
        ZEXPECT(!(from_string<protocol::CreateFile, lsp_config>(payload).has_value()));
    }
}

ZEST_CASE(literal_selects_variant_alternative) {
    // CreateFile and DeleteFile have the same shape; only `kind` tells them apart.
    auto edit = from_string<protocol::WorkspaceEdit, lsp_config>(R"({"documentChanges":[
        {"kind":"create","uri":"file:///a"},
        {"kind":"rename","oldUri":"file:///a","newUri":"file:///b"},
        {"kind":"delete","uri":"file:///b"}
    ]})");
    ZASSERT(edit);
    ZASSERT(edit->document_changes);
    auto& changes = *edit->document_changes;
    ZASSERT(changes.size() == 3U);
    ZEXPECT(std::holds_alternative<protocol::CreateFile>(changes[0]));
    ZEXPECT(std::holds_alternative<protocol::RenameFile>(changes[1]));
    ZEXPECT(std::holds_alternative<protocol::DeleteFile>(changes[2]));
}

ZEST_CASE(variant_alternatives_by_required_members) {
    auto unchanged = from_string<protocol::DocumentDiagnosticReport, lsp_config>(
        R"({"kind":"unchanged","resultId":"1"})");
    ZASSERT(unchanged);
    ZEXPECT(std::holds_alternative<protocol::RelatedUnchangedDocumentDiagnosticReport>(*unchanged));

    using Change = protocol::TextDocumentContentChangeEvent;
    auto whole = from_string<Change, lsp_config>(R"({"text":"x"})");
    ZASSERT(whole);
    ZEXPECT(std::holds_alternative<protocol::TextDocumentContentChangeWholeDocument>(*whole));

    auto partial =
        from_string<Change, lsp_config>(std::format(R"({{"range":{},"text":"x"}})", range_json));
    ZASSERT(partial);
    ZEXPECT(std::holds_alternative<protocol::TextDocumentContentChangePartial>(*partial));
}

ZEST_CASE(empty_object_alternative) {
    constexpr std::string_view legend = R"("legend":{"tokenTypes":[],"tokenModifiers":[]})";
    auto options = from_string<protocol::SemanticTokensOptions, lsp_config>(
        std::format(R"({{{},"range":{{}},"full":true}})", legend));
    ZASSERT(options);
    ZASSERT(options->range);
    ZEXPECT(std::holds_alternative<protocol::EmptyObject>(*options->range));
    ZASSERT(options->full);
    ZEXPECT(std::holds_alternative<protocol::boolean>(*options->full));

    auto serialized = to_string<lsp_config>(
        protocol::SemanticTokensOptions{.legend = {}, .range = protocol::EmptyObject{}});
    ZASSERT(serialized);
    ZEXPECT(*serialized == std::format(R"({{{},"range":{{}}}})", legend));
}

ZEST_CASE(optional_bool_reads_absence_as_false) {
    auto item = from_string<protocol::CompletionItem, lsp_config>(R"({"label":"a"})");
    ZASSERT(item);
    ZEXPECT(!item->preselect);

    auto preselected =
        to_string<lsp_config>(protocol::CompletionItem{.label = "a", .preselect = true});
    ZASSERT(preselected);
    ZEXPECT(*preselected == R"({"label":"a","preselect":true})");

    auto plain = to_string<lsp_config>(protocol::CompletionItem{.label = "a", .preselect = false});
    ZASSERT(plain);
    ZEXPECT(*plain == R"({"label":"a"})");
}

ZEST_CASE(nullable_member_encodes_null) {
    auto serialized = to_string<lsp_config>(protocol::InitializeParams{});
    ZASSERT(serialized);
    ZEXPECT(*serialized == R"({"processId":null,"rootUri":null,"capabilities":{}})");
}

ZEST_CASE(inherited_properties_are_members) {
    auto params = from_string<protocol::HoverParams, lsp_config>(
        R"({"textDocument":{"uri":"file:///a"},"position":{"line":1,"character":2},"workDoneToken":"t"})");
    ZASSERT(params);
    ZEXPECT(params->text_document.uri == "file:///a");
    ZEXPECT(params->position.character == 2U);
    ZASSERT(params->work_done_token);
    ZEXPECT(std::get<std::string>(*params->work_done_token) == "t");
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
    ZASSERT(serialized);
    ZEXPECT(*serialized == payload);

    auto decoded = from_string<protocol::SelectionRange, lsp_config>(payload);
    ZASSERT(decoded);
    ZASSERT(decoded->parent != nullptr);
    ZEXPECT(decoded->parent->parent == nullptr);
    ZEXPECT(decoded->parent->range.end.character == 1U);
}

ZEST_CASE(nested_structure_roundtrip) {
    auto payload = std::format(
        R"({{"name":"outer","kind":5,"range":{0},"selectionRange":{0},"children":[{{"name":"inner","kind":6,"range":{0},"selectionRange":{0}}}]}})",
        range_json);
    auto symbol = from_string<protocol::DocumentSymbol, lsp_config>(payload);
    ZASSERT(symbol);
    ZASSERT(symbol->children);
    ZASSERT(symbol->children->size() == 1U);
    ZEXPECT((*symbol->children)[0].name == "inner");

    auto serialized = to_string<lsp_config>(*symbol);
    ZASSERT(serialized);
    ZEXPECT(*serialized == payload);
}

ZEST_CASE(lsp_any_is_a_dynamic_value) {
    constexpr std::string_view payload =
        R"({"title":"t","command":"c","arguments":[1,"two",{"three":3.5},null,true]})";
    auto command = from_string<protocol::Command, lsp_config>(payload);
    ZASSERT(command);
    ZASSERT(command->arguments);
    auto& arguments = *command->arguments;
    ZASSERT(arguments.size() == 5U);
    ZEXPECT(arguments[0].get_int() == 1);
    ZEXPECT(arguments[1].get_string() == "two");
    ZEXPECT(arguments[2]["three"].get_double() == 3.5);
    ZEXPECT(arguments[3].is_null());
    ZEXPECT(arguments[4].get_bool() == true);

    auto serialized = to_string<lsp_config>(*command);
    ZASSERT(serialized);
    ZEXPECT(*serialized == payload);
}

ZEST_CASE(lsp_any_keys_are_not_renamed) {
    // lsp_config renames members to lowerCamel; dynamic object keys are data.
    constexpr std::string_view payload =
        R"({"title":"t","command":"c","arguments":[{"snake_key":1,"PascalKey":2}]})";
    auto command = from_string<protocol::Command, lsp_config>(payload);
    ZASSERT(command);
    auto serialized = to_string<lsp_config>(*command);
    ZASSERT(serialized);
    ZEXPECT(*serialized == payload);
}

ZEST_CASE(nullable_result) {
    using Result = protocol::RequestTraits<protocol::CompletionParams>::Result;

    auto none = from_string<Result, lsp_config>("null");
    ZASSERT(none);
    ZEXPECT(!none->has_value());

    auto list = from_string<Result, lsp_config>(R"({"isIncomplete":true,"items":[]})");
    ZASSERT(list);
    ZASSERT(list->has_value());
    ZEXPECT(std::holds_alternative<protocol::CompletionList>(**list));
}

ZEST_CASE(parameterless_methods) {
    ZEXPECT(
        zest::type_eq<protocol::RequestTraits<protocol::ShutdownParams>::Result, protocol::null>());
    ZEXPECT(protocol::RequestTraits<protocol::ShutdownParams>::method == "shutdown");
    ZEXPECT(protocol::NotificationTraits<protocol::ExitParams>::method == "exit");
}

// Clients send the params of shutdown and exit as null, or leave them out.
ZEST_CASE(parameterless_methods_read_null_params) {
    JSONCodec codec;
    for(std::string_view payload: {
            R"({"jsonrpc":"2.0","id":1,"method":"shutdown","params":null})",
            R"({"jsonrpc":"2.0","id":1,"method":"shutdown"})",
        }) {
        ZEST_CONTEXT("payload: {}", payload);
        auto parsed = codec.parse_message(std::string(payload));
        auto* request = std::get_if<IncomingRequest>(&parsed);
        ZASSERT(request != nullptr);
        ZEXPECT(codec.deserialize_value<protocol::ShutdownParams>(request->params).has_value());
    }
    auto parsed = codec.parse_message(R"({"jsonrpc":"2.0","method":"exit","params":null})");
    auto* notification = std::get_if<IncomingNotification>(&parsed);
    ZASSERT(notification != nullptr);
    ZEXPECT(codec.deserialize_value<protocol::ExitParams>(notification->params).has_value());
}

// An untagged variant lists a structure before the one it derives from, so
// an edit with an annotationId reads as the AnnotatedTextEdit it is, not as a
// TextEdit that drops the id.
ZEST_CASE(untagged_variant_takes_the_alternative_the_input_fills) {
    auto edit = from_string<protocol::TextDocumentEdit, lsp_config>(std::format(
        R"({{"textDocument":{{"uri":"file:///a","version":null}},"edits":[{{"range":{},"newText":"x","annotationId":"a"}}]}})",
        range_json));
    ZASSERT(edit);
    ZASSERT(edit->edits.size() == 1U);
    ZEXPECT(std::holds_alternative<protocol::AnnotatedTextEdit>(edit->edits[0]));
}

// In a report, `cancellable: false` disables the cancel button, where leaving
// it out keeps the button as it is, so false is written.
ZEST_CASE(tri_state_bool_writes_false) {
    auto serialized = to_string<lsp_config>(protocol::WorkDoneProgressReport{.cancellable = false});
    ZASSERT(serialized);
    ZEXPECT(zest::contains(*serialized, R"("cancellable":false)"));
}

// An optional member whose type admits null reads null as present.
ZEST_CASE(optional_nullable_member_reads_null_as_present) {
    auto params = from_string<protocol::InitializeParams, lsp_config>(
        R"({"processId":null,"rootUri":null,"capabilities":{},"workspaceFolders":null})");
    ZASSERT(params);
    ZASSERT(params->workspace_folders.has_value());
    ZEXPECT(!params->workspace_folders->has_value());
}

// So is an optional LSPAny, such as `data`: an item carries its null back
// to the server.
ZEST_CASE(optional_any_member_reads_null_as_present) {
    auto item = from_string<protocol::CompletionItem, lsp_config>(R"({"label":"a","data":null})");
    ZASSERT(item);
    ZASSERT(item->data.has_value());
    ZEXPECT(item->data->is_null());
    auto written = to_string<lsp_config>(*item);
    ZASSERT(written);
    ZEXPECT(*written == R"({"label":"a","data":null})");
}

// A required `T | null` must be present: null is a value, absent is not.
ZEST_CASE(required_nullable_member_absent_fails) {
    auto params = from_string<protocol::InitializeParams, lsp_config>(
        R"({"rootUri":null,"capabilities":{}})");
    ZASSERT(!params);
    ZEXPECT(zest::contains(params.error().to_string(), "processId"));
}

// Real LSP payloads nest up to about 130 levels: a SelectionRange parent
// chain, say.
ZEST_CASE(nesting_of_real_payloads_is_read) {
    JSONCodec codec;
    std::string chain;
    for(int level = 0; level < 125; ++level) {
        chain += R"({"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":1}})";
        chain += level + 1 < 125 ? R"(,"parent":)" : "";
    }
    chain += std::string(125, '}');
    auto parsed =
        codec.parse_message(std::format(R"({{"jsonrpc":"2.0","id":1,"result":[{}]}})", chain));
    auto* response = std::get_if<IncomingResponse>(&parsed);
    ZASSERT(response != nullptr);
    auto ranges = codec.deserialize_value<std::vector<protocol::SelectionRange>>(response->result);
    ZASSERT(ranges.has_value());
    ZEXPECT(ranges->size() == 1U);
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
        ZASSERT(bytes);
        protocol::SignatureInformation back{};
        ZASSERT(codec::bincode::from_bytes(std::span<const std::byte>(*bytes), back));
        ZEXPECT((back.active_parameter == info.active_parameter));
    }
}

// LSPAny inside a structure, beside other optional members: bincode reads
// each of its states back, a present null and an object included.
ZEST_CASE(diagnostic_data_roundtrips_through_bincode) {
    using Data = protocol::optional_nullable<protocol::LSPAny>;
    const std::pair<std::string_view, Data> states[] = {
        {"absent", Data{}                  },
        {"null",   Data{protocol::LSPAny{}}},
        {"object",
         Data{protocol::LSPAny{
             {"fix", "insert"},
             {"at", std::int64_t{3}},
         }}                                },
    };
    for(const auto& [state, data]: states) {
        ZEST_CONTEXT("data {}", state);
        protocol::Diagnostic diagnostic{
            .range = {.start = {.line = 1, .character = 2}, .end = {.line = 1, .character = 5}},
            .severity = protocol::DiagnosticSeverity::Warning,
            .source = "clice",
            .message = "unused variable",
            .data = data,
        };
        auto bytes = codec::bincode::to_bytes(diagnostic);
        ZASSERT(bytes);
        protocol::Diagnostic back{};
        ZASSERT(codec::bincode::from_bytes(std::span<const std::byte>(*bytes), back));
        ZEXPECT((back.data == diagnostic.data));
        ZEXPECT(back.severity == diagnostic.severity);
        ZEXPECT(back.source == diagnostic.source);
        ZEXPECT(back.range.end.character == 5U);
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
        ZASSERT(value);
        auto back = codec::dyn::from_dyn<protocol::SignatureInformation>(*value);
        ZASSERT(back);
        ZEXPECT((back->active_parameter == info.active_parameter));
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
    ZEXPECT((a == b));
    b.optional.reset();
    ZEXPECT(!(a == b));
}

};  // ZEST_SUITE(ipc_lsp_protocol_model)

}  // namespace
}  // namespace kota::ipc::lsp
