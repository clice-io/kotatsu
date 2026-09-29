#pragma once

// HttpServer: a loopback HTTP/1.1 server on a test's event loop, for the
// http tests to send requests to. It reads each request whole, body
// included, whether it comes with a Content-Length or in chunks, keeps it,
// and answers it with the Reply the test's handler returns, closing the
// connection after the reply. It parses on its own, with none of the http
// module's helpers, so that it checks them rather than trusts them.
//
// Beside it: seeding(), a handler that sets a cookie; loopback_client(), a
// client no proxy of the environment comes between; and RefusingPort, a
// loopback port that refuses connections.

#include <charconv>
#include <cstddef>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "async/harness/socket.h"
#include "kota/http/http.h"
#include "kota/async/async.h"

namespace kota::test {

/// Whether `left` and `right` are equal but for the case of ASCII letters.
inline bool same_name(std::string_view left, std::string_view right) {
    auto lower = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    if(left.size() != right.size()) {
        return false;
    }
    for(std::size_t i = 0; i < left.size(); ++i) {
        if(lower(left[i]) != lower(right[i])) {
            return false;
        }
    }
    return true;
}

/// `text` without the spaces and tabs at either end.
inline std::string_view trimmed(std::string_view text) {
    auto begin = text.find_first_not_of(" \t");
    if(begin == std::string_view::npos) {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(" \t") - begin + 1);
}

/// A request as the server read it.
struct Received {
    std::string method;
    std::string target;
    /// In the order they came, spelled as they came.
    std::vector<http::header> headers;
    std::string body;
    /// The body came in chunks, without a Content-Length.
    bool chunked = false;

    /// The value of the first header named `name`, in any case.
    std::optional<std::string> header(std::string_view name) const {
        for(const auto& item: headers) {
            if(same_name(item.name, name)) {
                return item.value;
            }
        }
        return std::nullopt;
    }

    /// How many headers are named `name`, in any case.
    std::size_t count(std::string_view name) const {
        std::size_t found = 0;
        for(const auto& item: headers) {
            found += same_name(item.name, name) ? 1 : 0;
        }
        return found;
    }
};

/// What the server answers a request with: a status, headers and a body,
/// sent with a Content-Length and Connection: close unless the headers
/// give their own, and without the body for HEAD.
struct Reply {
    int status = 200;
    std::vector<http::header> headers = {};
    std::string body = {};
    /// Written as it is instead of the reply the fields above make; an empty
    /// one closes the connection without a word.
    std::optional<std::string> raw = std::nullopt;
    /// The server answers once this is set: until then the request stays in
    /// flight.
    event* hold = nullptr;
};

class HttpServer {
public:
    using Handler = std::function<Reply(const Received&)>;

    /// Listens on a loopback port of its own and answers each request with
    /// what `handler` returns for it, 200 with no body by default.
    explicit HttpServer(event_loop& loop, Handler handler = {}) : handler(std::move(handler)) {
        auto listener = tcp::listen("127.0.0.1", 0, loop);
        if(!listener) {
            return;
        }
        auto name = listener->getsockname();
        if(!name) {
            return;
        }
        acceptor = std::move(*listener);
        bound_port = name->port;
        accepting = serve();
        loop.schedule(accepting);
    }

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /// Whether it listens: false when it could not.
    bool listening() const noexcept {
        return bound_port != 0;
    }

    int port() const noexcept {
        return bound_port;
    }

    /// `path` on this server, as a url.
    std::string url(std::string_view path) const {
        return std::format("http://127.0.0.1:{}{}", bound_port, path);
    }

    /// The requests it has read, in the order it read them.
    const std::vector<Received>& requests() const noexcept {
        return seen;
    }

private:
    task<> serve() {
        while(true) {
            auto connection = co_await acceptor.accept();
            if(!connection) {
                co_return;
            }
            connections.spawn(answer(std::move(*connection)));
        }
    }

    task<> answer(tcp connection) {
        auto request = co_await read_request(connection);
        if(!request) {
            co_return;
        }
        seen.push_back(*request);
        auto reply = handler ? handler(*request) : Reply{};
        if(reply.hold != nullptr) {
            co_await reply.hold->wait();
        }
        auto payload = reply.raw ? std::move(*reply.raw) : render(*request, reply);
        if(!payload.empty()) {
            [[maybe_unused]] auto written = co_await connection.write(payload);
        }
    }

    /// The next request on `connection`, or nothing when the connection
    /// ends before one has come whole.
    static task<std::optional<Received>> read_request(tcp& connection) {
        std::string buffer;
        // Reads until `enough` holds; false if the connection ends first.
        auto fill = [&](auto enough) -> task<bool> {
            while(!enough()) {
                auto more = co_await connection.read();
                if(!more) {
                    co_return false;
                }
                buffer += *more;
            }
            co_return true;
        };

        if(!co_await fill([&] { return buffer.find("\r\n\r\n") != std::string::npos; })) {
            co_return std::nullopt;
        }
        const auto head_end = buffer.find("\r\n\r\n");
        std::string_view head(buffer.data(), head_end);
        Received request;
        auto line_end = head.find("\r\n");
        auto start_line = head.substr(0, line_end);
        auto space = start_line.find(' ');
        auto second = start_line.find(' ', space + 1);
        if(space == std::string_view::npos || second == std::string_view::npos) {
            co_return std::nullopt;
        }
        request.method = start_line.substr(0, space);
        request.target = start_line.substr(space + 1, second - space - 1);
        while(line_end != std::string_view::npos) {
            head.remove_prefix(line_end + 2);
            line_end = head.find("\r\n");
            auto line = head.substr(0, line_end);
            auto colon = line.find(':');
            if(colon != std::string_view::npos) {
                request.headers.push_back({
                    .name = std::string(line.substr(0, colon)),
                    .value = std::string(trimmed(line.substr(colon + 1))),
                });
            }
        }

        if(auto expect = request.header("expect"); expect && same_name(*expect, "100-continue")) {
            constexpr std::string_view go_on = "HTTP/1.1 100 Continue\r\n\r\n";
            if(!co_await connection.write(go_on)) {
                co_return std::nullopt;
            }
        }

        auto at = head_end + 4;
        auto encoding = request.header("transfer-encoding");
        request.chunked = encoding && same_name(*encoding, "chunked");
        if(!request.chunked) {
            std::size_t length = 0;
            if(auto given = request.header("content-length")) {
                std::from_chars(given->data(), given->data() + given->size(), length);
            }
            if(!co_await fill([&] { return buffer.size() >= at + length; })) {
                co_return std::nullopt;
            }
            request.body = buffer.substr(at, length);
            co_return request;
        }

        // Chunks: a hexadecimal size line, the data and CRLF each, ended by a
        // chunk of size 0, trailer lines and an empty line.
        while(true) {
            if(!co_await fill([&] { return buffer.find("\r\n", at) != std::string::npos; })) {
                co_return std::nullopt;
            }
            auto size_end = buffer.find("\r\n", at);
            std::size_t size = 0;
            std::from_chars(buffer.data() + at, buffer.data() + size_end, size, 16);
            at = size_end + 2;
            if(size == 0) {
                break;
            }
            if(!co_await fill([&] { return buffer.size() >= at + size + 2; })) {
                co_return std::nullopt;
            }
            request.body += buffer.substr(at, size);
            at += size + 2;
        }
        while(true) {
            if(!co_await fill([&] { return buffer.find("\r\n", at) != std::string::npos; })) {
                co_return std::nullopt;
            }
            auto trailer_end = buffer.find("\r\n", at);
            if(trailer_end == at) {
                co_return request;
            }
            at = trailer_end + 2;
        }
    }

    static std::string render(const Received& request, const Reply& reply) {
        auto out = std::format("HTTP/1.1 {} Reply\r\n", reply.status);
        bool has_length = false;
        bool has_connection = false;
        for(const auto& [name, value]: reply.headers) {
            has_length = has_length || same_name(name, "content-length");
            has_connection = has_connection || same_name(name, "connection");
            out += std::format("{}: {}\r\n", name, value);
        }
        if(!has_length) {
            out += std::format("Content-Length: {}\r\n", reply.body.size());
        }
        if(!has_connection) {
            out += "Connection: close\r\n";
        }
        out += "\r\n";
        if(request.method != "HEAD") {
            out += reply.body;
        }
        return out;
    }

    Handler handler;
    tcp::acceptor acceptor;
    int bound_port = 0;
    std::vector<Received> seen;
    /// Let go when the server goes: each is cancelled, and ends there
    /// unless a write of its own is still on its way.
    task_group<> connections;
    /// Goes first, ending the wait for the next connection.
    task<> accepting;
};

/// A handler that sets `cookie` in its reply to /seed, and answers every
/// other request plainly.
inline HttpServer::Handler seeding(std::string cookie) {
    return [cookie = std::move(cookie)](const Received& request) {
        if(request.target == "/seed") {
            return Reply{.headers = {{"Set-Cookie", cookie + "; Path=/"}}};
        }
        return Reply{};
    };
}

/// A client that goes straight to loopback servers: without no_proxy(), a
/// proxy the environment names (http_proxy and the like) comes between.
inline http::client loopback_client() {
    return http::client().no_proxy();
}

/// A loopback port that refuses connections: a socket holds it bound, so
/// that nothing else takes it, and never listens. A port a listener has just
/// left will not do: WSL's loopback relay goes on taking connections to it
/// for a while, and resets them later.
struct RefusingPort {
    RawSocket bound;
    /// 0 when no port could be bound.
    int port = 0;

    RefusingPort() {
        bound.fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(bound.fd != invalid_socket) {
            port = bind_loopback_raw(bound.fd);
        }
    }

    std::string url() const {
        return std::format("http://127.0.0.1:{}/", port);
    }
};

}  // namespace kota::test
