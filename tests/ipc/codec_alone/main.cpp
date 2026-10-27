// A program that reads and writes messages as catter's hook library does,
// built against the codec target alone, without the event loop: a reference
// into the loop fails its link. On Windows it checks besides that it imports
// none of the DLLs libuv brings, which would load into every process the hook
// runs in. The codecs' behaviour is their unit tests'.

#include <cstdio>
#include <format>
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
};

int failures = 0;

/// Whether `passed`; prints `what` when not.
bool check(bool passed, std::string_view what) {
    if(!passed) {
        std::fprintf(stderr, "failed: %s\n", std::string(what).c_str());
        ++failures;
    }
    return passed;
}

/// Writes a request with Codec, frames it, reads it back and decodes its
/// params.
template <typename Codec>
void exchange(std::string_view name) {
    using namespace kota::ipc;
    Codec codec;
    auto request = codec.encode_request(1, "catter/check", Params{.text = "hook"});
    if(!check(request.has_value(), std::format("{}: encode_request", name))) {
        return;
    }
    FrameParser parser;
    auto step = parser.feed(frame_header(request->size()) + *request);
    if(!check(step.frame && *step.frame, std::format("{}: FrameParser", name))) {
        return;
    }
    auto incoming = codec.parse_message(std::move(**step.frame));
    auto* read = std::get_if<IncomingRequest>(&incoming);
    if(!check(read != nullptr, std::format("{}: parse_message", name))) {
        return;
    }
    auto params = codec.template deserialize_value<Params>(read->params);
    check(params && params->text == "hook", std::format("{}: deserialize_value", name));
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
