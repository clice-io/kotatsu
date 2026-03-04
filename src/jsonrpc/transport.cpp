#include "eventide/jsonrpc/transport.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace eventide::jsonrpc {

namespace {

constexpr std::size_t max_header_bytes = 8 * 1024;
constexpr std::size_t max_payload_bytes = 64 * 1024 * 1024;

std::string_view trim_ascii(std::string_view value) {
    auto start = value.find_first_not_of(" \t");
    if(start == std::string_view::npos) {
        return {};
    }
    auto end = value.find_last_not_of(" \t");
    return value.substr(start, end - start + 1);
}

bool iequals_ascii(std::string_view lhs, std::string_view rhs) {
    if(lhs.size() != rhs.size()) {
        return false;
    }
    for(std::size_t i = 0; i < lhs.size(); ++i) {
        if(std::tolower(static_cast<unsigned char>(lhs[i])) !=
           std::tolower(static_cast<unsigned char>(rhs[i]))) {
            return false;
        }
    }
    return true;
}

std::optional<std::size_t> parse_content_length(std::string_view header) {
    std::size_t pos = 0;
    while(pos < header.size()) {
        auto end = header.find("\r\n", pos);
        if(end == std::string_view::npos) {
            break;
        }

        auto line = header.substr(pos, end - pos);
        pos = end + 2;

        auto sep = line.find(':');
        if(sep == std::string_view::npos) {
            continue;
        }

        auto name = trim_ascii(line.substr(0, sep));
        if(!iequals_ascii(name, "Content-Length")) {
            continue;
        }

        auto value = trim_ascii(line.substr(sep + 1));
        if(value.empty()) {
            return std::nullopt;
        }

        std::size_t parsed = 0;
        for(char ch: value) {
            if(ch < '0' || ch > '9') {
                return std::nullopt;
            }
            const auto digit = static_cast<std::size_t>(ch - '0');
            if(parsed > ((std::numeric_limits<std::size_t>::max)() - digit) / 10) {
                return std::nullopt;
            }
            parsed = parsed * 10 + digit;
        }

        if(parsed > max_payload_bytes) {
            return std::nullopt;
        }
        return parsed;
    }
    return std::nullopt;
}

std::string to_error_text(error err) {
    return std::string(err.message());
}

std::expected<stream, std::string> to_stream(result<tcp_socket> socket) {
    if(!socket) {
        return std::unexpected(to_error_text(socket.error()));
    }
    return stream(std::move(*socket));
}

std::expected<stream, std::string> open_stdio_stream(int fd, bool readable, event_loop& loop) {
    switch(guess_handle(fd)) {
        case handle_type::tty: {
            auto opened = console::open(fd, console::options{readable}, loop);
            if(!opened) {
                return std::unexpected(to_error_text(opened.error()));
            }
            return stream(std::move(*opened));
        }

        case handle_type::pipe:
        case handle_type::file:
        case handle_type::unknown: {
            auto opened = pipe::open(fd, pipe::options{}, loop);
            if(!opened) {
                return std::unexpected(to_error_text(opened.error()));
            }
            return stream(std::move(*opened));
        }

        case handle_type::tcp: {
            auto opened = tcp_socket::open(fd, loop);
            if(!opened) {
                return std::unexpected(to_error_text(opened.error()));
            }
            return stream(std::move(*opened));
        }

        default: return std::unexpected("unsupported stdio handle type");
    }
}

}  // namespace

std::expected<void, std::string> Transport::close_output() {
    return std::unexpected("transport does not support closing output");
}

StreamTransport::StreamTransport(stream input, stream output) :
    read_stream(std::move(input)), write_stream(std::move(output)) {}

StreamTransport::StreamTransport(stream stream) :
    read_stream(std::move(stream)), shared_stream(true) {}

std::expected<std::unique_ptr<StreamTransport>, std::string>
    StreamTransport::open_stdio(event_loop& loop) {
    auto input = open_stdio_stream(0, true, loop);
    if(!input) {
        return std::unexpected(input.error());
    }

    auto output = open_stdio_stream(1, false, loop);
    if(!output) {
        return std::unexpected(output.error());
    }

    return std::make_unique<StreamTransport>(std::move(*input), std::move(*output));
}

task<std::expected<std::unique_ptr<StreamTransport>, std::string>>
    StreamTransport::connect_tcp(std::string_view host, int port, event_loop& loop) {
    auto connected = co_await tcp_socket::connect(host, port, loop);
    auto channel = to_stream(std::move(connected));
    if(!channel) {
        co_return std::unexpected(channel.error());
    }

    co_return std::make_unique<StreamTransport>(std::move(*channel));
}

std::expected<std::unique_ptr<StreamTransport>, std::string>
    StreamTransport::open_tcp(int fd, event_loop& loop) {
    auto channel = to_stream(tcp_socket::open(fd, loop));
    if(!channel) {
        return std::unexpected(channel.error());
    }
    return std::make_unique<StreamTransport>(std::move(*channel));
}

task<std::optional<std::string>> StreamTransport::read_message() {
    std::string header;
    std::optional<std::size_t> content_length;

    while(!content_length.has_value()) {
        auto chunk = co_await read_stream.read_chunk();
        if(!chunk) {
            co_return std::nullopt;
        }

        const auto old_size = header.size();
        header.append(chunk->data(), chunk->size());

        if(header.size() > max_header_bytes) {
            co_return std::nullopt;
        }

        auto marker = header.find("\r\n\r\n");
        if(marker == std::string::npos) {
            read_stream.consume(chunk->size());
            continue;
        }

        const auto header_end = marker + 4;
        if(header_end > max_header_bytes) {
            co_return std::nullopt;
        }
        const auto consumed_from_chunk = header_end > old_size ? header_end - old_size : 0;
        read_stream.consume(consumed_from_chunk);

        auto view = std::string_view(header.data(), header_end);
        content_length = parse_content_length(view);
        if(!content_length.has_value()) {
            co_return std::nullopt;
        }
    }

    std::string payload;
    payload.reserve(*content_length);

    while(payload.size() < *content_length) {
        auto chunk = co_await read_stream.read_chunk();
        if(!chunk) {
            co_return std::nullopt;
        }

        const auto need = *content_length - payload.size();
        const auto take = std::min<std::size_t>(need, chunk->size());
        payload.append(chunk->data(), take);
        read_stream.consume(take);
    }

    co_return payload;
}

task<bool> StreamTransport::write_message(std::string_view payload) {
    std::string framed;
    framed.reserve(32 + payload.size());
    framed.append("Content-Length: ");
    framed.append(std::to_string(payload.size()));
    framed.append("\r\n\r\n");
    framed.append(payload.data(), payload.size());

    auto& stream = shared_stream ? read_stream : write_stream;
    auto status = co_await stream.write(std::span<const char>(framed.data(), framed.size()));
    co_return !status.has_error();
}

std::expected<void, std::string> StreamTransport::close_output() {
    if(shared_stream) {
        read_stream = stream{};
        return {};
    }

    write_stream = stream{};
    return {};
}

}  // namespace eventide::jsonrpc
