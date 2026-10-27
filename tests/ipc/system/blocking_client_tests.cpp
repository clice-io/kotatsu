#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "ipc/harness/codec_bincode.h"
#include "ipc/harness/codec_json.h"
#include "ipc/harness/codec_kit.h"
#include "ipc/harness/fixtures.h"
#include "kota/ipc/blocking_client.h"
#include "kota/ipc/codec/bincode.h"
#include "kota/ipc/codec/json.h"
#include "kota/ipc/framing.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"

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
    Link() {
#ifdef _WIN32
        std::random_device random;
        const auto name = std::format(LR"(\\.\pipe\kota-{:08x}{:08x})", random(), random());
        remote = ::CreateNamedPipeW(name.c_str(),
                                    PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                        PIPE_REJECT_REMOTE_CLIENTS,
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
#else
        int ends[2] = {-1, -1};
        ZASSERT(::socketpair(AF_UNIX, SOCK_STREAM, 0, ends) == 0);
        client = ends[0];
        remote = ends[1];
#endif
    }

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
    Handle remote;
    FrameParser parser;
    std::string unread;
};

/// The cases of a BlockingClient over adapter A's codec.
template <test::CodecAdapter A>
void blocking_client_cases(const zest::CaseRegistrar& add_case) {
    using Client = BlockingClient<typename A::Codec>;

    // The answer is written before the request is sent: the pipe holds it.
    add_case("request_gets_its_result", [] {
        Link link;
        Client client(link.client);
        link.send(test::response<A>(1, AddResult{.sum = 3}));

        auto result =
            client.template send_request<AddResult>("test/add", AddParams{.a = 1, .b = 2});
        ZASSERT(result.has_value());
        ZEXPECT(result->sum == 3);
        auto request = link.receive_message<A>();
        ZASSERT(request.has_value());
        ZEXPECT(request->kind == Message::Kind::Request);
        ZEXPECT(request->id == RequestID(1));
        ZEXPECT(request->method == "test/add");
        auto params = test::decoded<AddParams, A>(request->body);
        ZASSERT(params.has_value());
        ZEXPECT(params->a == 1);
        ZEXPECT(params->b == 2);
    });

    add_case("request_ids_count_up_from_one", [] {
        Link link;
        Client client(link.client);
        link.send(test::response<A>(1, AddResult{.sum = 1}));
        link.send(test::response<A>(2, AddResult{.sum = 2}));

        auto first = client.template send_request<AddResult>("test/add", AddParams{});
        auto second = client.template send_request<AddResult>("test/add", AddParams{});
        ZASSERT(first.has_value());
        ZASSERT(second.has_value());
        ZEXPECT(first->sum == 1);
        ZEXPECT(second->sum == 2);
        auto one = link.receive_message<A>();
        auto two = link.receive_message<A>();
        ZASSERT(one.has_value());
        ZASSERT(two.has_value());
        ZEXPECT(one->id == RequestID(1));
        ZEXPECT(two->id == RequestID(2));
    });

    add_case("error_answer_fails_only_its_request", [] {
        Link link;
        Client client(link.client);
        link.send(A::error_response(RequestID(1), Error(ErrorCode::InvalidParams, "bad")));
        link.send(test::response<A>(2, AddResult{.sum = 2}));

        auto refused = client.template send_request<AddResult>("test/add", AddParams{});
        auto answered = client.template send_request<AddResult>("test/add", AddParams{});
        ZASSERT(refused.has_error());
        ZEXPECT(code_of(refused.error()) == ErrorCode::InvalidParams);
        ZEXPECT(refused.error().message == "bad");
        ZASSERT(answered.has_value());
        ZEXPECT(answered->sum == 2);
    });

    add_case("result_that_does_not_decode_fails_only_its_request", [] {
        Link link;
        Client client(link.client);
        link.send(A::response_raw(1, A::not_a_value));
        link.send(test::response<A>(2, AddResult{.sum = 2}));

        auto undecoded = client.template send_request<AddResult>("test/add", AddParams{});
        auto answered = client.template send_request<AddResult>("test/add", AddParams{});
        ZEXPECT(undecoded.has_error());
        ZASSERT(answered.has_value());
        ZEXPECT(answered->sum == 2);
    });

    // Nothing is written, and the next request goes out under the next id.
    add_case("params_that_do_not_encode_fail_unsent", [] {
        Link link;
        Client client(link.client);
        link.send(test::response<A>(2, AddResult{.sum = 2}));

        auto unsent =
            client.template send_request<AddResult>("test/unwritable", test::Unwritable{});
        auto answered = client.template send_request<AddResult>("test/add", AddParams{});
        ZEXPECT(unsent.has_error());
        ZASSERT(answered.has_value());
        ZEXPECT(answered->sum == 2);
        auto request = link.receive_message<A>();
        ZASSERT(request.has_value());
        ZEXPECT(request->id == RequestID(2));
        ZEXPECT(request->method == "test/add");
    });

    // The frame is skipped whole, so the conversation goes on after it.
    add_case("answer_over_the_limit_fails_only_its_request", [] {
        Link link;
        Client client(link.client, 64);
        link.send(test::response<A>(1, NoteParams{.text = std::string(200, 'x')}));
        link.send(test::response<A>(2, AddResult{.sum = 2}));

        auto skipped = client.template send_request<AddResult>("test/add", AddParams{});
        auto answered = client.template send_request<AddResult>("test/add", AddParams{});
        ZASSERT(skipped.has_error());
        ZEXPECT(code_of(skipped.error()) == ErrorCode::MessageTooLarge);
        ZASSERT(answered.has_value());
        ZEXPECT(answered->sum == 2);
    });

    add_case("message_over_the_limit_that_is_no_answer_ends_it", [] {
        Link link;
        Client client(link.client, 64);
        link.send(test::notification<A>("test/note", NoteParams{.text = std::string(200, 'x')}));

        auto ended = client.template send_request<AddResult>("test/add", AddParams{});
        ZASSERT(ended.has_error());
        ZEXPECT(code_of(ended.error()) == ErrorCode::ConnectionClosed);
        ZEXPECT(zest::contains(ended.error().message, "exceeds the limit of 64 bytes"));
    });

    // The request that read it fails, and so does the next, unsent.
    add_case("message_that_is_no_answer_ends_it", [] {
        const std::vector<std::pair<std::string, std::string>> cases = {
            {test::request<A>(7, "test/add", AddParams{}),
             "a request instead of the answer: test/add"},
            {test::notification<A>("test/note", NoteParams{}),
             "a notification instead of the answer: test/note"},
            {test::response<A>(7, AddResult{}), "the answer to another request"},
            {A::error_response(RequestID(7), Error("refused")),
             "an error answering another request: refused"},
            {A::error_response(std::nullopt, Error("unreadable")),
             "an error response without an id: unreadable"},
        };
        for(const auto& [sent, said]: cases) {
            ZEST_CONTEXT("{}", said);
            Link link;
            std::optional<Client> client(std::in_place, link.client);
            link.send(sent);

            auto ended = client->template send_request<AddResult>("test/add", AddParams{});
            auto after = client->template send_request<AddResult>("test/add", AddParams{});
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
    });

    // What the codec said of it.
    add_case("garbage_ends_it", [] {
        Link link;
        Client client(link.client);
        link.send(A::garbage);

        auto ended = client.template send_request<AddResult>("test/add", AddParams{});
        ZASSERT(ended.has_error());
        ZEXPECT(code_of(ended.error()) == ErrorCode::ConnectionClosed);
        ZEXPECT(!ended.error().message.empty());
    });

    add_case("malformed_frame_ends_it", [] {
        Link link;
        Client client(link.client);
        link.write("Content-Length: many\r\n\r\n");

        auto ended = client.template send_request<AddResult>("test/add", AddParams{});
        ZASSERT(ended.has_error());
        ZEXPECT(code_of(ended.error()) == ErrorCode::ConnectionClosed);
        ZEXPECT(zest::contains(ended.error().message, "Content-Length"));
    });

    // The remote closes once it has read the request, as a remote that went
    // before it answered.
    add_case("end_of_input_ends_it", [] {
        Link link;
        Client client(link.client);
        bool received = false;
        std::thread remote([&] {
            received = link.receive().has_value();
            link.close_remote();
        });

        auto ended = client.template send_request<AddResult>("test/add", AddParams{});
        remote.join();
        ZEXPECT(received);
        ZASSERT(ended.has_error());
        ZEXPECT(code_of(ended.error()) == ErrorCode::ConnectionClosed);
        ZEXPECT(ended.error().message == "the input ended");
    });

    // No SIGPIPE: the process lives on to see the failure.
    add_case("write_to_a_closed_remote_ends_it", [] {
        Link link;
        Client client(link.client);
        link.close_remote();

        auto ended = client.template send_request<AddResult>("test/add", AddParams{});
        ZASSERT(ended.has_error());
        ZEXPECT(code_of(ended.error()) == ErrorCode::ConnectionClosed);
        ZEXPECT(!ended.error().message.empty());
    });

    add_case("moved_client_keeps_the_connection", [] {
        Link first;
        Link second;
        first.send(test::response<A>(1, AddResult{.sum = 1}));
        Client moved(first.client);
        Client client(std::move(moved));
        Client replaced(second.client);
        replaced = std::move(client);

        auto answered = replaced.template send_request<AddResult>("test/add", AddParams{});
        ZASSERT(answered.has_value());
        ZEXPECT(answered->sum == 1);
        // The handle it held before is closed.
        ZEXPECT(!second.receive().has_value());
    });
}

ZEST_SUITE(ipc_blocking_client) {

ZEST_CASE_GROUP(bincode) {
    blocking_client_cases<test::BincodeAdapter>(add_case);
}

ZEST_CASE_GROUP(json) {
    blocking_client_cases<test::JSONAdapter>(add_case);
}

};  // ZEST_SUITE(ipc_blocking_client)

}  // namespace

}  // namespace kota::ipc
