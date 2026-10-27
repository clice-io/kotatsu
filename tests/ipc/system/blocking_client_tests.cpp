#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "fs/harness/files.h"
#include "ipc/harness/codec_bincode.h"
#include "ipc/harness/codec_json.h"
#include "ipc/harness/codec_kit.h"
#include "ipc/harness/fixtures.h"
#include "kota/ipc/blocking_client.h"
#include "kota/ipc/codec/bincode.h"
#include "kota/ipc/codec/json.h"
#include "kota/ipc/framing.h"
#include "kota/ipc/peer/bincode.h"
#include "kota/ipc/transport.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace kota::ipc {

namespace {

using protocol::ErrorCode;
using protocol::RequestID;
using test::AddParams;
using test::AddResult;
using test::code_of;
using test::Message;
using test::NoteParams;

using Handle = BlockingClient<BincodeCodec>::native_handle_type;

/// A connection, whose `client` end a BlockingClient takes, and whose other
/// end the test plays the remote on, with plain blocking reads and writes: a
/// pair of sockets, on Windows a named pipe of a random name and a handle
/// opened to it as a client's is.
class Link {
public:
#ifdef _WIN32
    /// Over a pipe of messages, which the client's end reads as messages.
    struct Messages {};

    Link() {
        open(PIPE_TYPE_BYTE | PIPE_READMODE_BYTE);
    }

    explicit Link(Messages) {
        open(PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE);
        DWORD mode = PIPE_READMODE_MESSAGE;
        ZASSERT(::SetNamedPipeHandleState(client, &mode, nullptr, nullptr));
    }
#else
    Link() {
        int ends[2] = {-1, -1};
        ZASSERT(::socketpair(AF_UNIX, SOCK_STREAM, 0, ends) == 0);
        client = ends[0];
        remote = ends[1];
    }
#endif

    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    ~Link() {
        close_remote();
    }

    /// The end a BlockingClient takes, and closes.
    Handle client;

    /// Writes `bytes` as they are.
    void write(std::string_view bytes) {
        while(!bytes.empty()) {
#ifdef _WIN32
            DWORD written = 0;
            ZASSERT(::WriteFile(remote,
                                bytes.data(),
                                static_cast<DWORD>(bytes.size()),
                                &written,
                                nullptr));
#else
            const auto written = ::send(remote, bytes.data(), bytes.size(), 0);
            ZASSERT(written > 0);
#endif
            bytes.remove_prefix(static_cast<std::size_t>(written));
        }
    }

#ifdef _WIN32
    /// Writes no bytes, which the client reads as a read of none.
    void write_nothing() {
        DWORD written = 0;
        ZASSERT(::WriteFile(remote, "", 0, &written, nullptr));
    }
#endif

    /// Writes `payload` in a frame.
    void send(std::string_view payload) {
        write(frame_header(payload.size()));
        write(payload);
    }

    /// The payload of the next frame the client wrote, waiting for it; none
    /// once the client's end has closed.
    std::optional<std::string> receive() {
        std::array<char, 4096> buffer;
        while(true) {
            auto step = parser.feed(unread);
            unread.erase(0, step.consumed);
            if(step.frame) {
                ZASSERT(step.frame->has_value());
                return std::move(**step.frame);
            }
            std::size_t got = 0;
#ifdef _WIN32
            DWORD read = 0;
            if(::ReadFile(remote,
                          buffer.data(),
                          static_cast<DWORD>(buffer.size()),
                          &read,
                          nullptr)) {
                got = read;
            }
#else
            if(const auto read = ::recv(remote, buffer.data(), buffer.size(), 0); read > 0) {
                got = static_cast<std::size_t>(read);
            }
#endif
            if(got == 0) {
                return std::nullopt;
            }
            unread.append(buffer.data(), got);
        }
    }

    /// The next message the client wrote, as adapter A reads it.
    template <test::CodecAdapter A>
    std::optional<Message> receive_message() {
        auto payload = receive();
        if(!payload) {
            return std::nullopt;
        }
        auto message = A::read(*payload);
        ZASSERT(message.has_value());
        return std::move(*message);
    }

    /// Closes the remote's end: the client reads the end of its input, and
    /// its writes fail.
    void close_remote() {
#ifdef _WIN32
        if(remote != INVALID_HANDLE_VALUE) {
            ::CloseHandle(remote);
            remote = INVALID_HANDLE_VALUE;
        }
#else
        if(remote != -1) {
            ::close(remote);
            remote = -1;
        }
#endif
    }

private:
#ifdef _WIN32
    /// Opens a named pipe of a random name with `type`, as a server does, and
    /// a handle to it, as a client opens one.
    void open(DWORD type) {
        std::random_device random;
        const auto name = std::format(LR"(\\.\pipe\kota-{:08x}{:08x})", random(), random());
        remote = ::CreateNamedPipeW(name.c_str(),
                                    PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                    type | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                    1,
                                    64 * 1024,
                                    64 * 1024,
                                    0,
                                    nullptr);
        ZASSERT(remote != INVALID_HANDLE_VALUE);
        client = ::CreateFileW(name.c_str(),
                               GENERIC_READ | GENERIC_WRITE,
                               0,
                               nullptr,
                               OPEN_EXISTING,
                               0,
                               nullptr);
        ZASSERT(client != INVALID_HANDLE_VALUE);
    }
#endif

    Handle remote;
    FrameParser parser;
    std::string unread;
};

using Bincode = test::BincodeAdapter;
using Client = BlockingClient<BincodeCodec>;

/// A name for pipe::listen() nobody else uses: a socket in `dir`, or on
/// Windows a named pipe named after it.
std::string pipe_name(const test::TempDir& dir) {
#ifdef _WIN32
    return R"(\\.\pipe\)" + dir.path.filename().string();
#else
    return dir.file("socket");
#endif
}

/// A handle connected to what pipe::listen() listens on at `name`, opened as
/// a program without an event loop opens it.
Handle connect_to(const std::string& name) {
#ifdef _WIN32
    const std::wstring wide(name.begin(), name.end());
    Handle handle = ::CreateFileW(wide.c_str(),
                                  GENERIC_READ | GENERIC_WRITE,
                                  0,
                                  nullptr,
                                  OPEN_EXISTING,
                                  0,
                                  nullptr);
    ZASSERT(handle != INVALID_HANDLE_VALUE);
#else
    Handle handle = ::socket(AF_UNIX, SOCK_STREAM, 0);
    ZASSERT(handle >= 0);
    sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    ZASSERT(name.size() < sizeof(address.sun_path));
    std::memcpy(address.sun_path, name.data(), name.size());
    ZASSERT(::connect(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
#endif
    return handle;
}

ZEST_SUITE(ipc_blocking_client, zest::LoopFixture) {

// The answer is written before the request is sent: the pipe holds it.
ZEST_CASE(request_gets_its_result) {
    Link link;
    Client client(link.client);
    link.send(test::response<Bincode>(1, AddResult{.sum = 3}));

    auto result = client.send_request<AddResult>("test/add", AddParams{.a = 1, .b = 2});
    ZASSERT(result.has_value());
    ZEXPECT(result->sum == 3);
    auto request = link.receive_message<Bincode>();
    ZASSERT(request.has_value());
    ZEXPECT(request->kind == Message::Kind::Request);
    ZEXPECT(request->id == RequestID(1));
    ZEXPECT(request->method == "test/add");
    auto params = test::decoded<AddParams, Bincode>(request->body);
    ZASSERT(params.has_value());
    ZEXPECT(params->a == 1);
    ZEXPECT(params->b == 2);
}

ZEST_CASE(request_ids_count_up_from_one) {
    Link link;
    Client client(link.client);
    link.send(test::response<Bincode>(1, AddResult{.sum = 1}));
    link.send(test::response<Bincode>(2, AddResult{.sum = 2}));

    auto first = client.send_request<AddResult>("test/add", AddParams{});
    auto second = client.send_request<AddResult>("test/add", AddParams{});
    ZASSERT(first.has_value());
    ZASSERT(second.has_value());
    ZEXPECT(first->sum == 1);
    ZEXPECT(second->sum == 2);
    auto one = link.receive_message<Bincode>();
    auto two = link.receive_message<Bincode>();
    ZASSERT(one.has_value());
    ZASSERT(two.has_value());
    ZEXPECT(one->id == RequestID(1));
    ZEXPECT(two->id == RequestID(2));
}

ZEST_CASE(request_answered_with_an_error_fails) {
    Link link;
    Client client(link.client);
    link.send(Bincode::error_response(RequestID(1), Error(ErrorCode::InvalidParams, "bad")));
    link.send(test::response<Bincode>(2, AddResult{.sum = 2}));

    auto refused = client.send_request<AddResult>("test/add", AddParams{});
    auto answered = client.send_request<AddResult>("test/add", AddParams{});
    ZASSERT(refused.has_error());
    ZEXPECT(code_of(refused.error()) == ErrorCode::InvalidParams);
    ZEXPECT(refused.error().message == "bad");
    ZASSERT(answered.has_value());
    ZEXPECT(answered->sum == 2);
}

// The next request is answered: only the result was wrong.
ZEST_CASE(request_whose_result_does_not_decode_fails) {
    Link link;
    Client client(link.client);
    link.send(Bincode::response_raw(1, Bincode::not_a_value));
    link.send(test::response<Bincode>(2, AddResult{.sum = 2}));

    auto undecoded = client.send_request<AddResult>("test/add", AddParams{});
    auto answered = client.send_request<AddResult>("test/add", AddParams{});
    ZASSERT(undecoded.has_error());
    ZEXPECT(code_of(undecoded.error()) == ErrorCode::RequestFailed);
    ZASSERT(answered.has_value());
    ZEXPECT(answered->sum == 2);
}

// Nothing is written, and the next request goes out under the next id.
ZEST_CASE(request_whose_params_do_not_encode_fails) {
    Link link;
    Client client(link.client);
    link.send(test::response<Bincode>(2, AddResult{.sum = 2}));

    auto unsent = client.send_request<AddResult>("test/unwritable", test::Unwritable{});
    auto answered = client.send_request<AddResult>("test/add", AddParams{});
    ZASSERT(unsent.has_error());
    ZEXPECT(code_of(unsent.error()) == ErrorCode::InternalError);
    ZASSERT(answered.has_value());
    ZEXPECT(answered->sum == 2);
    auto request = link.receive_message<Bincode>();
    ZASSERT(request.has_value());
    ZEXPECT(request->id == RequestID(2));
    ZEXPECT(request->method == "test/add");
}

// Larger than the pipe holds, so written as the client reads it.
ZEST_CASE(answer_larger_than_a_read_is_read_whole) {
    Link link;
    Client client(link.client);
    const std::string text(256 * 1024, 'x');
    std::thread remote([&] { link.send(test::response<Bincode>(1, NoteParams{.text = text})); });

    auto result = client.send_request<NoteParams>("test/note", NoteParams{});
    remote.join();
    ZASSERT(result.has_value());
    ZEXPECT(result->text == text);
}

// The frame is skipped whole, over many reads, and the conversation goes on
// after it.
ZEST_CASE(request_answered_over_the_limit_fails) {
    Link link;
    Client client(link.client, 64);
    std::thread remote([&] {
        link.send(test::response<Bincode>(1, NoteParams{.text = std::string(256 * 1024, 'x')}));
        link.send(test::response<Bincode>(2, AddResult{.sum = 2}));
    });

    auto skipped = client.send_request<AddResult>("test/add", AddParams{});
    auto answered = client.send_request<AddResult>("test/add", AddParams{});
    remote.join();
    ZASSERT(skipped.has_error());
    ZEXPECT(code_of(skipped.error()) == ErrorCode::MessageTooLarge);
    ZASSERT(answered.has_value());
    ZEXPECT(answered->sum == 2);
}

ZEST_CASE(oversized_message_that_is_no_answer_ends_the_conversation) {
    Link link;
    Client client(link.client, 64);
    link.send(test::notification<Bincode>("test/note", NoteParams{.text = std::string(200, 'x')}));

    auto ended = client.send_request<AddResult>("test/add", AddParams{});
    ZASSERT(ended.has_error());
    ZEXPECT(code_of(ended.error()) == ErrorCode::ConnectionClosed);
    ZEXPECT(zest::contains(ended.error().message, "exceeds the limit of 64 bytes"));
}

// The request that read it fails, and so does the next, unsent.
ZEST_CASE(message_that_is_no_answer_ends_the_conversation) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {test::request<Bincode>(7, "test/add", AddParams{}),
         "a request instead of the answer: test/add"},
        {test::notification<Bincode>("test/note", NoteParams{}),
         "a notification instead of the answer: test/note"},
        {test::response<Bincode>(7, AddResult{}), "the answer to another request"},
        {Bincode::error_response(RequestID(7), Error("refused")),
         "an error answering another request: refused"},
        {Bincode::error_response(std::nullopt, Error("unreadable")),
         "an error response without an id: unreadable"},
    };
    for(const auto& [sent, said]: cases) {
        ZEST_CONTEXT("{}", said);
        Link link;
        std::optional<Client> client(std::in_place, link.client);
        link.send(sent);

        auto ended = client->send_request<AddResult>("test/add", AddParams{});
        auto after = client->send_request<AddResult>("test/add", AddParams{});
        client.reset();
        ZASSERT(ended.has_error());
        ZEXPECT(code_of(ended.error()) == ErrorCode::ConnectionClosed);
        ZEXPECT(ended.error().message == said);
        ZASSERT(after.has_error());
        ZEXPECT(code_of(after.error()) == ErrorCode::ConnectionClosed);
        ZEXPECT(after.error().message == said);
        // The first request, then the end the client's close made.
        ZEXPECT(link.receive().has_value());
        ZEXPECT(!link.receive().has_value());
    }
}

// With what the codec said of it.
ZEST_CASE(garbage_ends_the_conversation) {
    Link link;
    Client client(link.client);
    link.send(Bincode::garbage);

    auto ended = client.send_request<AddResult>("test/add", AddParams{});
    ZASSERT(ended.has_error());
    ZEXPECT(code_of(ended.error()) == ErrorCode::ConnectionClosed);
    auto read = BincodeCodec().parse_message(std::string(Bincode::garbage));
    auto* unreadable = std::get_if<IncomingParseError>(&read);
    ZASSERT(unreadable != nullptr);
    ZEXPECT(ended.error().message == unreadable->error.message);
}

ZEST_CASE(malformed_frame_ends_the_conversation) {
    Link link;
    Client client(link.client);
    link.write("Content-Length: many\r\n\r\n");

    auto ended = client.send_request<AddResult>("test/add", AddParams{});
    ZASSERT(ended.has_error());
    ZEXPECT(code_of(ended.error()) == ErrorCode::ConnectionClosed);
    ZEXPECT(zest::contains(ended.error().message, "Content-Length"));
}

// The remote closes once it has read the request, as a remote that went
// before it answered. The client closes before the join, so that a request
// it never wrote fails the case rather than hangs it.
ZEST_CASE(end_of_input_ends_the_conversation) {
    Link link;
    std::optional<Client> client(std::in_place, link.client);
    bool received = false;
    std::thread remote([&] {
        received = link.receive().has_value();
        link.close_remote();
    });

    auto ended = client->send_request<AddResult>("test/add", AddParams{});
    client.reset();
    remote.join();
    ZEXPECT(received);
    ZASSERT(ended.has_error());
    ZEXPECT(code_of(ended.error()) == ErrorCode::ConnectionClosed);
    ZEXPECT(ended.error().message == "the input ended");
}

// The write fails, rather than the read after it, and the next request fails
// with it. That no SIGPIPE ends the process is the codec_alone check's: an
// event loop, which another case of this process may have run, ignores it.
ZEST_CASE(write_to_a_closed_remote_fails) {
    Link link;
    Client client(link.client);
    link.close_remote();

    auto ended = client.send_request<AddResult>("test/add", AddParams{});
    auto after = client.send_request<AddResult>("test/add", AddParams{});
    ZASSERT(ended.has_error());
    ZEXPECT(code_of(ended.error()) == ErrorCode::ConnectionClosed);
    ZEXPECT(!ended.error().message.empty());
    ZEXPECT(ended.error().message != "the input ended");
    ZASSERT(after.has_error());
    ZEXPECT(after.error().message == ended.error().message);
}

#ifdef _WIN32
// A pipe reads a write of no bytes as a read of none, which is not its end.
ZEST_CASE(write_of_no_bytes_is_no_end) {
    Link link;
    Client client(link.client);
    link.write_nothing();
    link.send(test::response<Bincode>(1, AddResult{.sum = 3}));

    auto result = client.send_request<AddResult>("test/add", AddParams{});
    ZASSERT(result.has_value());
    ZEXPECT(result->sum == 3);
}

// Each write a message, the payload's larger than one read: it is read in
// pieces, as in byte mode.
ZEST_CASE(message_larger_than_a_read_is_read_whole) {
    Link link(Link::Messages{});
    Client client(link.client);
    const std::string text(16 * 1024, 'x');
    link.send(test::response<Bincode>(1, NoteParams{.text = text}));

    auto result = client.send_request<NoteParams>("test/note", NoteParams{});
    ZASSERT(result.has_value());
    ZEXPECT(result->text == text);
}
#endif

// As a function that opens the connection returns it.
ZEST_CASE(moved_client_keeps_the_connection) {
    Link link;
    link.send(test::response<Bincode>(1, AddResult{.sum = 1}));
    std::optional<Client> opened(std::in_place, link.client);
    Client client(std::move(*opened));
    opened.reset();

    auto answered = client.send_request<AddResult>("test/add", AddParams{});
    ZASSERT(answered.has_value());
    ZEXPECT(answered->sum == 1);
}

// What a hook library asks: a BincodePeer over a connection pipe::listen()
// accepted, on a loop the client knows nothing of. The peer's run() ends once
// the client, done, closes its end.
ZEST_CASE(requests_reach_a_peer_over_a_pipe) {
    test::TempDir dir;
    const auto name = pipe_name(dir);
    auto listener = pipe::listen(name, loop);
    ZASSERT(listener.has_value());
    auto serve = [&]() -> task<> {
        auto accepted = co_await listener->accept();
        ZASSERT(accepted.has_value());
        BincodePeer peer(loop, std::make_unique<StreamTransport>(stream(std::move(*accepted))));
        peer.on_request(
            "test/add",
            [](BincodePeer::RequestContext&, const AddParams& params) -> RequestResult<AddParams> {
                co_return AddResult{.sum = params.a + params.b};
            });
        co_await peer.run();
    };
    std::vector<std::optional<std::int64_t>> sums;
    std::thread asking([&] {
        Client client(connect_to(name));
        for(std::int64_t a: {1, 2}) {
            auto result = client.send_request<AddResult>("test/add", AddParams{.a = a, .b = 10});
            sums.push_back(result ? std::optional(result->sum) : std::nullopt);
        }
    });

    auto [served] = run(serve());
    asking.join();
    ZEXPECT(served.has_value());
    ZEXPECT(sums == std::vector<std::optional<std::int64_t>>{11, 12});
}

// The client is one over any codec: JSON carries what bincode does.
ZEST_CASE(json_request_gets_its_result) {
    using JSON = test::JSONAdapter;
    Link link;
    BlockingClient<JSONCodec> client(link.client);
    link.send(test::response<JSON>(1, AddResult{.sum = 3}));

    auto result = client.send_request<AddResult>("test/add", AddParams{.a = 1, .b = 2});
    ZASSERT(result.has_value());
    ZEXPECT(result->sum == 3);
    auto request = link.receive_message<JSON>();
    ZASSERT(request.has_value());
    ZEXPECT(request->id == RequestID(1));
    ZEXPECT(request->method == "test/add");
}

// It names this request, but is no answer the protocol allows.
ZEST_CASE(unreadable_response_ends_the_conversation) {
    using JSON = test::JSONAdapter;
    Link link;
    BlockingClient<JSONCodec> client(link.client);
    link.send(R"({"jsonrpc":"2.0","id":1})");

    auto ended = client.send_request<AddResult>("test/add", AddParams{});
    ZASSERT(ended.has_error());
    ZEXPECT(code_of(ended.error()) == ErrorCode::ConnectionClosed);
    ZEXPECT(ended.error().message == "response must contain exactly one of result or error");
    auto request = link.receive_message<JSON>();
    ZASSERT(request.has_value());
}

};  // ZEST_SUITE(ipc_blocking_client)

}  // namespace

}  // namespace kota::ipc
