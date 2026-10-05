// Decodes and encodes again values of the types in kota/ipc/lsp/protocol.h,
// for roundtrip.test.ts. It reads one JSON object a line,
//
//   {"type": "<name>", "value": <JSON>}
//
// where the name is a type's, or `result:<method>` for a request's result,
// and answers each with a line: {"value": <the value encoded again>}, or
// {"error": "<why it did not decode>"}. It exits with 0 when its input ends.
//
// Instantiating the codec for every protocol type makes this the largest
// translation unit of the tests: with clang, about 2 minutes and 2.7 GB in
// Debug, 5 minutes and 4.2 GB at -O2, and 25 minutes and 8.9 GB under ASan and
// UBSan at -O2, and MinGW's assembler takes minutes over its object. So only
// plain Debug builds, MinGW GCC's aside, build and run it (tests/CMakeLists.txt). Splitting the
// table does not help: every part instantiates most of the types again, as members of the ones it
// has.

#include <cstdio>
#include <iostream>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "kota/ipc/codec/json.h"
#include "kota/codec/macro.h"
#include "kota/ipc/lsp/protocol.h"

namespace kota::test {
namespace {

namespace protocol = ipc::protocol;

struct Input {
    std::string type;
    codec::RawValue value;
};

/// The value encoded again, or why it did not decode.
struct Output {
    KOTATSU_ANNOTATE(skip_if = skip_when::none)
    <std::optional<codec::RawValue>> value;
    KOTATSU_ANNOTATE(skip_if = skip_when::none)
    <std::optional<std::string>> error;
};

template <typename T>
Output roundtrip(std::string_view json) {
    auto decoded = codec::json::from_string<T, ipc::lsp_config>(json);
    if(!decoded) {
        return {.value = std::nullopt, .error = decoded.error().to_string()};
    }
    auto encoded = codec::json::to_string<ipc::lsp_config>(*decoded);
    if(!encoded) {
        return {.value = std::nullopt, .error = encoded.error().to_string()};
    }
    return {.value = codec::RawValue{std::move(*encoded)}, .error = std::nullopt};
}

const std::unordered_map<std::string_view, Output (*)(std::string_view)> types = {
#define KOTA_LSP_TYPE(name) {#name, &roundtrip<protocol::name>},
#define KOTA_LSP_RESULT(method, params)                                                            \
    {"result:" method, &roundtrip<protocol::RequestTraits<protocol::params>::Result>},
#include "ipc/lsp/harness/protocol_types.inc"
#undef KOTA_LSP_RESULT
#undef KOTA_LSP_TYPE
};

}  // namespace
}  // namespace kota::test

int main() {
    using namespace kota;
    std::string line;
    while(std::getline(std::cin, line)) {
        auto input = codec::json::from_string<test::Input>(line);
        if(!input) {
            std::println(stderr, "[error] not an input line: {}", input.error().to_string());
            return 1;
        }
        auto found = test::types.find(input->type);
        if(found == test::types.end()) {
            std::println(stderr, "[error] no type {}", input->type);
            return 1;
        }
        auto output = codec::json::to_string(found->second(input->value.data));
        if(!output) {
            std::println(stderr, "[error] {}", output.error().to_string());
            return 1;
        }
        std::println("{}", *output);
        std::fflush(stdout);
    }
    return 0;
}
