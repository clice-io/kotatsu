// A program that reads and writes messages as catter's hook library does,
// built against the codec target alone, without the event loop: a reference
// into the loop fails its link. On Windows it checks besides that it imports
// none of the DLLs libuv brings, which would load into every process the hook
// runs in.
//
// It prints each check that fails, and exits with 1 when one did.

#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "kota/ipc/codec/bincode.h"
#include "kota/ipc/codec/json.h"
#include "kota/ipc/framing.h"

#ifdef _WIN32
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <windows.h>
#endif

namespace {

struct Params {
    std::string text;
    std::int64_t number = 0;
};

int failures = 0;

void check(bool passed, std::string_view what) {
    if(!passed) {
        std::fprintf(stderr, "failed: %s\n", std::string(what).c_str());
        ++failures;
    }
}

/// The payload of the one frame `stream` holds, read a byte at a time.
std::optional<std::string> unframe(std::string_view stream) {
    kota::ipc::FrameParser parser;
    for(std::size_t i = 0; i < stream.size(); ++i) {
        auto step = parser.feed(stream.substr(i, 1));
        if(step.frame) {
            if(!*step.frame || i + 1 != stream.size()) {
                return std::nullopt;
            }
            return std::move(**step.frame);
        }
    }
    return std::nullopt;
}

/// A request and its response, each written, framed, read back and decoded
/// by Codec.
template <typename Codec>
void exchange(std::string_view name) {
    using namespace kota::ipc;
    auto expect = [&](bool passed, std::string_view what) {
        check(passed, std::format("{}: {}", name, what));
    };
    Codec codec;
    const Params sent{.text = "hook", .number = 7};

    auto request = codec.encode_request(1, "catter/check", sent);
    expect(request.has_value(), "encode_request");
    if(!request) {
        return;
    }
    auto payload = unframe(frame_header(request->size()) + *request);
    expect(payload.has_value(), "the request's frame");
    if(!payload) {
        return;
    }
    auto incoming = codec.parse_message(std::move(*payload));
    auto* read = std::get_if<IncomingRequest>(&incoming);
    expect(read != nullptr, "parse_message of the request");
    if(read == nullptr) {
        return;
    }
    expect(read->method == "catter/check", "the request's method");
    auto params = codec.template deserialize_value<Params>(read->params);
    expect(params.has_value(), "deserialize_value of the params");
    if(params) {
        expect(params->text == sent.text, "the params' text");
        expect(params->number == sent.number, "the params' number");
    }

    auto response = codec.encode_success_response(read->id, std::string("done"));
    expect(response.has_value(), "encode_success_response");
    if(!response) {
        return;
    }
    auto answer = codec.parse_message(std::move(*response));
    auto* result = std::get_if<IncomingResponse>(&answer);
    expect(result != nullptr, "parse_message of the response");
    if(result == nullptr) {
        return;
    }
    expect(result->id == protocol::RequestID(1), "the response's id");
    auto value = codec.template deserialize_value<std::string>(result->result);
    expect(value.has_value() && *value == "done", "deserialize_value of the result");
}

#ifdef _WIN32

/// Whether this program imports from `dll`, in its import directory: a DLL
/// that loads with it.
bool imports(std::string_view dll) {
    auto* base = reinterpret_cast<const std::byte*>(::GetModuleHandleW(nullptr));
    const auto& dos = *reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto& nt = *reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos.e_lfanew);
    const auto& directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    auto same = [](unsigned char a, unsigned char b) {
        return std::tolower(a) == std::tolower(b);
    };
    for(auto* entry =
            reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
        entry->Name != 0;
        ++entry) {
        std::string_view name(reinterpret_cast<const char*>(base + entry->Name));
        if(std::ranges::equal(name, dll, same)) {
            return true;
        }
    }
    return false;
}

#endif

}  // namespace

int main() {
    exchange<kota::ipc::BincodeCodec>("BincodeCodec");
    exchange<kota::ipc::JSONCodec>("JSONCodec");
#ifdef _WIN32
    // What libuv links on Windows.
    constexpr std::array loop_dlls = {
        "advapi32.dll",
        "dbghelp.dll",
        "iphlpapi.dll",
        "ole32.dll",
        "psapi.dll",
        "shell32.dll",
        "user32.dll",
        "userenv.dll",
        "ws2_32.dll",
    };
    for(std::string_view dll: loop_dlls) {
        check(!imports(dll), std::format("imports {}", dll));
    }
#endif
    return failures == 0 ? 0 : 1;
}
