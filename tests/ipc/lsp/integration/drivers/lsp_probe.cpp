// Answers questions about kota::ipc::lsp's URIs and positions, for uri.test.ts
// and position.test.ts to put the same questions to VS Code's libraries. It
// reads one JSON object a line and answers each with one:
//
//   {"fromFilePath": "<path>"}             {"uri": "<text>"} or {"error": "..."}
//   {"filePath": "<uri>"}                  {"path": "<path>"} or {"error": "..."}
//   {"text": "...", "offset": <byte>}      {"line": l, "character": c} or {}
//   {"text": "...", "line": l, "character": c}
//                                          {"offset": <byte>} or {}
//   {"text": "...", "line": l, "character": c, "clamped": true}
//                                          {"offset": <byte>}
//
// Positions count UTF-16 code units, as LSP's default encoding does. It exits
// with 0 when its input ends.

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <optional>
#include <print>
#include <string>
#include <utility>

#include "kota/ipc/codec/json.h"
#include "kota/codec/macro.h"
#include "kota/ipc/lsp/position.h"
#include "kota/ipc/lsp/uri.h"

namespace kota::test {
namespace {

namespace lsp = ipc::lsp;

struct Question {
    std::optional<std::string> from_file_path;
    std::optional<std::string> file_path;
    std::optional<std::string> text;
    std::optional<std::uint32_t> offset;
    std::optional<std::uint32_t> line;
    std::optional<std::uint32_t> character;
    std::optional<bool> clamped;
};

KOTATSU_ANNOTATION(left_out_if_none, skip_if = skip_when::none);

template <typename T>
using omittable = meta::annotate<left_out_if_none>::type<std::optional<T>>;

struct Answer {
    omittable<std::string> uri;
    omittable<std::string> path;
    omittable<std::uint32_t> line;
    omittable<std::uint32_t> character;
    omittable<std::uint32_t> offset;
    omittable<std::string> error;
};

Answer answer(const Question& question) {
    Answer answer{};
    if(question.from_file_path) {
        auto uri = lsp::URI::from_file_path(*question.from_file_path);
        if(uri) {
            answer.uri = uri->str();
        } else {
            answer.error = std::move(uri).error();
        }
    } else if(question.file_path) {
        auto path = lsp::URI::parse(*question.file_path).and_then([](const lsp::URI& uri) {
            return uri.file_path();
        });
        if(path) {
            answer.path = std::move(*path);
        } else {
            answer.error = std::move(path).error();
        }
    } else if(question.offset) {
        lsp::LineMap map(*question.text);
        if(auto position = map.to_position(*question.offset)) {
            answer.line = position->line;
            answer.character = position->character;
        }
    } else {
        lsp::LineMap map(*question.text);
        ipc::protocol::Position position{.line = *question.line, .character = *question.character};
        if(question.clamped.value_or(false)) {
            answer.offset = map.to_offset_clamped(position);
        } else {
            answer.offset = map.to_offset(position);
        }
    }
    return answer;
}

}  // namespace
}  // namespace kota::test

int main() {
    using namespace kota;
    std::string line;
    while(std::getline(std::cin, line)) {
        auto question = codec::json::from_string<test::Question, ipc::lsp_config>(line);
        if(!question) {
            std::println(stderr, "[error] not a question: {}", question.error().to_string());
            return 1;
        }
        auto answer = codec::json::to_string<ipc::lsp_config>(test::answer(*question));
        if(!answer) {
            std::println(stderr, "[error] {}", answer.error().to_string());
            return 1;
        }
        std::println("{}", *answer);
        std::fflush(stdout);
    }
    return 0;
}
