// A program that reads and writes messages as catter's hook library does,
// with each codec and with a BlockingClient over a pipe, built against the
// codec target alone, without the event loop: a reference into the loop fails
// its link. On Windows it checks besides that it imports none of the DLLs
// libuv brings, which would load into every process the hook runs in. The
// codecs' and the client's behaviour is their tests'.

#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "kota/ipc/blocking_client.h"
#include "kota/ipc/codec/bincode.h"
#include "kota/ipc/codec/json.h"
#include "kota/ipc/framing.h"

#ifdef _WIN32
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <random>
#include <windows.h>
#else
#include <sys/socket.h>
#include <unistd.h>
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

/// Both ends of a connection: a pair of sockets, on Windows a named pipe and
/// a handle opened to it.
struct Ends {
    kota::ipc::BlockingClient<kota::ipc::BincodeCodec>::native_handle_type client;
    kota::ipc::BlockingClient<kota::ipc::BincodeCodec>::native_handle_type remote;
};

bool connect(Ends& ends) {
#ifdef _WIN32
    std::random_device random;
    const auto name = std::format(LR"(\\.\pipe\kota-{:08x}{:08x})", random(), random());
    ends.remote = ::CreateNamedPipeW(name.c_str(),
                                     PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                     PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                     1,
                                     4096,
                                     4096,
                                     0,
                                     nullptr);
    if(ends.remote == INVALID_HANDLE_VALUE) {
        return false;
    }
    ends.client = ::CreateFileW(name.c_str(),
                                GENERIC_READ | GENERIC_WRITE,
                                0,
                                nullptr,
                                OPEN_EXISTING,
                                0,
                                nullptr);
    return ends.client != INVALID_HANDLE_VALUE;
#else
    int fds[2] = {-1, -1};
    if(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return false;
    }
    ends.client = fds[0];
    ends.remote = fds[1];
    return true;
#endif
}

/// Writes all of `bytes` to the remote's end.
bool write_all(const Ends& ends, std::string_view bytes) {
#ifdef _WIN32
    DWORD written = 0;
    return ::WriteFile(ends.remote,
                       bytes.data(),
                       static_cast<DWORD>(bytes.size()),
                       &written,
                       nullptr) &&
           written == bytes.size();
#else
    return ::send(ends.remote, bytes.data(), bytes.size(), 0) == static_cast<ssize_t>(bytes.size());
#endif
}

void close_remote(const Ends& ends) {
#ifdef _WIN32
    ::CloseHandle(ends.remote);
#else
    ::close(ends.remote);
#endif
}

/// Asks with a BlockingClient, whose answer the remote's end holds already.
void ask(std::string_view name) {
    using namespace kota::ipc;
    Ends ends;
    if(!check(connect(ends), std::format("{}: connect", name))) {
        return;
    }
    BincodeCodec codec;
    auto answer = codec.encode_success_response(1, Params{.text = "catter"});
    if(check(answer.has_value(), std::format("{}: encode_success_response", name)) &&
       check(write_all(ends, frame_header(answer->size()) + *answer),
             std::format("{}: write", name))) {
        BlockingClient<BincodeCodec> client(ends.client);
        auto result = client.send_request<Params>("catter/check", Params{.text = "hook"});
        check(result && result->text == "catter", std::format("{}: send_request", name));
    }
    close_remote(ends);
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
    ask("BlockingClient");
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
