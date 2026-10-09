#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "stream_self.h"

namespace kota {

namespace {

/// libuv sends a write it has taken whatever happens to its task, and ends
/// it with ECANCELED if the stream closes first.
struct write_op : uv::request_op<write_op, uv_write_t> {
    uv_stream_t* stream;
    std::span<const uv_buf_t> bufs;

    write_op(uv_stream_t* stream, std::span<const uv_buf_t> bufs) noexcept :
        stream(stream), bufs(bufs) {}

    bool start() noexcept {
        return submitted(
            ::uv_write(&req, stream, bufs.data(), static_cast<unsigned int>(bufs.size()), on_done));
    }
};

/// Why `size` bytes cannot go out in one write, if they cannot: a write goes
/// out whole.
error refused_write(std::size_t size) {
    if(size == 0) {
        return error::invalid_argument;
    }
    if(size > stream::max_write_size) {
        return error::value_too_large_for_defined_data_type;
    }
    return {};
}

/// libuv shuts the write side once the writes before it have gone out,
/// whatever happens to its task, and ends it with ECANCELED if the stream
/// closes first.
struct shutdown_op : uv::request_op<shutdown_op, uv_shutdown_t> {
    uv_stream_t* stream;

    explicit shutdown_op(uv_stream_t* stream) noexcept : stream(stream) {}

    bool start() noexcept {
        return submitted(::uv_shutdown(&req, stream, on_done));
    }
};

}  // namespace

void stream::Self::on_alloc(uv_handle_t* handle, std::size_t, uv_buf_t* buf) {
    // Reading stops as the buffer fills, so there is always room here.
    auto room = static_cast<Self*>(handle->data)->buffer.writable();
    *buf = ::uv_buf_init(room.data(), static_cast<unsigned int>(room.size()));
}

void stream::Self::on_read(uv_stream_t* handle, ssize_t nread, const uv_buf_t*) {
    auto* self = static_cast<Self*>(handle->data);
    // libuv reports a read that found nothing to read (EAGAIN) as 0 bytes.
    if(nread == 0) {
        return;
    }

    if(nread < 0) {
        self->ended = uv::status_to_error(nread);
        self->stop_reading();
    } else {
        self->buffer.commit(static_cast<std::size_t>(nread));
        if(self->buffer.full()) {
            self->stop_reading();
        }
    }

    // A reader waits only on an empty buffer, so bytes arrive only while
    // nothing has ended reading: either way `ended` is what it gets.
    if(self->slot.waiting()) {
        self->slot.deliver(self->ended);
    }
}

uv::waiter_slot<void>::awaiter stream::Self::fill() {
    if(!buffer.empty()) {
        return slot.ready({});
    }
    if(ended) {
        return slot.ready(ended);
    }
    if(!reading) {
        if(auto err = error(::uv_read_start(&stream, on_alloc, on_read))) {
            return slot.ready(err);
        }
        reading = true;
    }
    return slot.wait();
}

void stream::Self::stop_reading() {
    ::uv_read_stop(&stream);
    reading = false;
}

stream::stream() noexcept = default;

stream::stream(stream&& other) noexcept = default;

stream& stream::operator=(stream&& other) noexcept = default;

stream::~stream() = default;

stream::stream(detail::unique_handle<Self> self) noexcept : self(std::move(self)) {}

handle_type guess_handle(int fd) {
    switch(::uv_guess_handle(fd)) {
        case UV_FILE: return handle_type::file;
        case UV_TTY: return handle_type::tty;
        case UV_NAMED_PIPE: return handle_type::pipe;
        case UV_TCP: return handle_type::tcp;
        case UV_UDP: return handle_type::udp;
        default: return handle_type::unknown;
    }
}

task<std::string, error> stream::read() {
    if(!self) {
        co_await fail(error::invalid_argument);
    }

    if(auto err = co_await self->fill()) {
        co_await fail(err);
    }

    // The unread bytes lie in two pieces once they wrap around the ring.
    std::string out;
    while(!self->buffer.empty()) {
        auto chunk = self->buffer.readable();
        out.append(chunk.begin(), chunk.end());
        self->buffer.consume(chunk.size());
    }
    co_return out;
}

task<std::size_t, error> stream::read_some(std::span<char> dst) {
    if(!self) {
        co_await fail(error::invalid_argument);
    }

    if(dst.empty()) {
        co_return 0;
    }

    if(auto err = co_await self->fill()) {
        if(err == error::end_of_file) {
            co_return 0;
        }
        co_await fail(err);
    }

    auto chunk = self->buffer.readable();
    auto count = std::min(dst.size(), chunk.size());
    std::ranges::copy(chunk.first(count), dst.begin());
    self->buffer.consume(count);
    co_return count;
}

task<stream::chunk, error> stream::read_chunk() {
    if(!self) {
        co_await fail(error::invalid_argument);
    }

    if(auto err = co_await self->fill()) {
        co_await fail(err);
    }

    co_return self->buffer.readable();
}

void stream::consume(std::size_t n) {
    if(self) {
        self->buffer.consume(n);
    }
}

task<std::string, error> stream::read_to_end() {
    std::string out;
    while(true) {
        auto chunk = co_await read_chunk();
        if(chunk.has_error()) {
            if(chunk.error() != error::end_of_file) {
                co_await fail(chunk.error());
            }
            co_return out;
        }
        out.append(chunk->begin(), chunk->end());
        consume(chunk->size());
    }
}

task<std::optional<std::string>, error> stream::read_line() {
    std::string line;
    while(true) {
        auto chunk = co_await read_chunk();
        if(chunk.has_error()) {
            if(chunk.error() != error::end_of_file) {
                co_await fail(chunk.error());
            }
            if(line.empty()) {
                co_return std::nullopt;
            }
            co_return line;
        }
        const std::string_view text(chunk->data(), chunk->size());
        const auto end = text.find('\n');
        if(end == std::string_view::npos) {
            line.append(text);
            consume(text.size());
            continue;
        }
        line.append(text.substr(0, end));
        consume(end + 1);
        // The '\r' of a "\r\n" may have come in an earlier chunk.
        if(line.ends_with('\r')) {
            line.pop_back();
        }
        co_return line;
    }
}

error stream::stop() {
    if(!self) {
        return error::invalid_argument;
    }

    self->stop_reading();
    self->slot.abort(*self->handle.loop);
    return {};
}

task<void, error> stream::write(std::span<const char> data) {
    if(!self) {
        co_await fail(error::invalid_argument);
    }
    if(auto err = refused_write(data.size())) {
        co_await fail(err);
    }

    // A named op: MSVC's ASan build gives up the tail call of symmetric
    // transfer from an await on a temporary this large.
    const auto buf = uv::buffer_of(data);
    write_op op(&self->stream, std::span(&buf, 1));
    if(auto err = co_await op) {
        co_await fail(err);
    }
}

task<void, error> stream::write_vectored(std::span<const std::span<const char>> pieces) {
    if(!self) {
        co_await fail(error::invalid_argument);
    }

    std::size_t size = 0;
    std::vector<uv_buf_t> bufs;
    bufs.reserve(pieces.size());
    for(auto piece: pieces) {
        if(piece.empty()) {
            continue;
        }
        size += piece.size();
        bufs.push_back(uv::buffer_of(piece));
    }
    if(auto err = refused_write(size)) {
        co_await fail(err);
    }

    write_op op(&self->stream, bufs);
    if(auto err = co_await op) {
        co_await fail(err);
    }
}

result<std::size_t> stream::try_write(std::span<const char> data) {
    if(!self) {
        return outcome_error(error::invalid_argument);
    }

    // Nothing is written at once everywhere, even where libuv refuses every
    // try_write, as it does for pipes on Windows.
    if(data.empty()) {
        return std::size_t{0};
    }

    auto buf = uv::buffer_of(data);
    auto written = ::uv_try_write(&self->stream, &buf, 1);
    if(written < 0) {
        return outcome_error(error(written));
    }
    return static_cast<std::size_t>(written);
}

task<void, error> stream::shutdown() {
    if(!self) {
        co_await fail(error::invalid_argument);
    }

    if(auto err = co_await shutdown_op(&self->stream)) {
        co_await fail(err);
    }
}

bool stream::readable() const noexcept {
    return self && ::uv_is_readable(&self->stream);
}

bool stream::writable() const noexcept {
    return self && ::uv_is_writable(&self->stream);
}

error stream::set_blocking(bool enabled) {
    if(!self) {
        return error::invalid_argument;
    }
    return error(::uv_stream_set_blocking(&self->stream, enabled ? 1 : 0));
}

}  // namespace kota
