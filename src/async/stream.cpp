#include <algorithm>
#include <cassert>
#include <limits>
#include <utility>
#include <vector>

#include "awaiter.h"

namespace eventide {

namespace {

error ensure_reading(stream::Self* self,
                     stream::Self::read_mode mode,
                     uv_alloc_cb alloc_cb,
                     uv_read_cb read_cb) {
    if(self == nullptr) {
        return error::invalid_argument;
    }

    if(self->active_read_mode == mode) {
        return {};
    }

    if(self->active_read_mode != stream::Self::read_mode::none) {
        uv::read_stop(self->stream);
        self->active_read_mode = stream::Self::read_mode::none;
    }

    auto err = uv::read_start(self->stream, alloc_cb, read_cb);
    if(err) {
        return err;
    }

    self->active_read_mode = mode;
    return {};
}

struct stream_read_await : uv::await_op<stream_read_await> {
    using await_base = uv::await_op<stream_read_await>;
    // Stream self used to register reader waiter and store error status.
    stream::Self* self;

    explicit stream_read_await(stream::Self* self) : self(self) {}

    static void on_cancel(system_op* op) {
        await_base::complete_cancel(op, [](auto& aw) {
            if(aw.self) {
                if(aw.self->active_read_mode != stream::Self::read_mode::none) {
                    uv::read_stop(aw.self->stream);
                    aw.self->active_read_mode = stream::Self::read_mode::none;
                }
                aw.self->reader.disarm();
            }
        });
    }

    static void on_alloc(uv_handle_t* handle, size_t, uv_buf_t* buf) {
        auto s = static_cast<eventide::stream::Self*>(handle->data);
        assert(s != nullptr && "on_alloc requires stream state in handle->data");

        auto [dst, writable] = s->buffer.get_write_ptr();
        buf->base = dst;
        buf->len = writable;

        if(writable == 0) {
            uv::read_stop(*reinterpret_cast<uv_stream_t*>(handle));
            s->active_read_mode = stream::Self::read_mode::none;
        }
    }

    // When nread=0, it means no data was read but the stream is still alive (e.g., EAGAIN).
    static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t*) {
        auto s = static_cast<eventide::stream::Self*>(stream->data);
        assert(s != nullptr && "on_read requires stream state in stream->data");
        auto err = uv::status_to_error(nread);
        if(err) {
            uv::read_stop(*stream);
            s->active_read_mode = stream::Self::read_mode::none;
            if(s->reader.has_waiter()) {
                auto* reader = s->reader.waiter;
                s->reader.mark_cancelled_if(nread);
                s->reader.disarm();
                s->error_code = err;
                reader->complete();
            }
            return;
        }

        s->buffer.advance_write(static_cast<size_t>(nread));

        if(s->reader.has_waiter()) {
            auto* reader = s->reader.waiter;
            s->reader.disarm();
            s->error_code = {};
            reader->complete();
        }
    }

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        if(!self) {
            return waiting;
        }

        // Buffered reads intentionally leave libuv reading across await boundaries so later
        // read_chunk()/read() calls can wait for more bytes without tearing the watcher down.
        // If we are already in buffered mode, there is nothing to restart. If another read style
        // was active, switch callbacks by stopping that watcher first.
        auto err = ensure_reading(self, stream::Self::read_mode::buffered, on_alloc, on_read);
        if(err) {
            self->error_code = err;
            return waiting;
        }
        self->reader.arm(*this);
        return this->link_continuation(&waiting.promise(), location);
    }

    error await_resume() noexcept {
        return self->error_code;
    }
};

struct stream_read_some_await : uv::await_op<stream_read_some_await> {
    using await_base = uv::await_op<stream_read_some_await>;
    using promise_t = task<result<std::size_t>>::promise_type;

    // Stream self that owns the active read waiter.
    stream::Self* self;
    // Destination buffer provided by the caller.
    std::span<char> dst;
    // Final read result observed by await_resume().
    result<std::size_t> out = std::unexpected(error());

    stream_read_some_await(stream::Self* self, std::span<char> buffer) : self(self), dst(buffer) {}

    static void on_cancel(system_op* op) {
        await_base::complete_cancel(op, [](auto& aw) {
            if(aw.self) {
                if(aw.self->active_read_mode != stream::Self::read_mode::none) {
                    uv::read_stop(aw.self->stream);
                    aw.self->active_read_mode = stream::Self::read_mode::none;
                }
                aw.self->reader.disarm();
            }
        });
    }

    static void on_alloc(uv_handle_t* handle, size_t, uv_buf_t* buf) {
        auto s = static_cast<eventide::stream::Self*>(handle->data);
        assert(s != nullptr && "on_alloc requires stream state in handle->data");

        auto* aw = static_cast<stream_read_some_await*>(s->reader.waiter);
        assert(aw != nullptr && "on_alloc requires active read_some awaiter");
        if(aw->dst.empty()) {
            buf->base = nullptr;
            buf->len = 0;
            return;
        }

        buf->base = aw->dst.data();
        buf->len = static_cast<unsigned int>(aw->dst.size());
    }

    // When nread=0, it means no data was read but the stream is still alive (e.g., EAGAIN).
    static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t*) {
        auto s = static_cast<eventide::stream::Self*>(stream->data);
        assert(s != nullptr && "on_read requires stream state in stream->data");

        auto* aw = static_cast<stream_read_some_await*>(s->reader.waiter);
        assert(aw != nullptr && "on_read requires active read_some awaiter");

        if(nread == UV_EOF) {
            aw->out = std::size_t{0};
        } else if(auto err = uv::status_to_error(nread)) {
            aw->out = std::unexpected(err);
            aw->mark_cancelled_if(nread);
        } else if(nread > 0) {
            aw->out = static_cast<std::size_t>(nread);
        } else {
            // nread=0 with no error means no data was read, but the stream is still alive (e.g.,
            // EAGAIN).
            return;
        }

        uv::read_stop(*stream);
        s->active_read_mode = stream::Self::read_mode::none;
        s->reader.disarm();
        aw->complete();
    }

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        if(!self) {
            return waiting;
        }

        self->reader.arm(*this);
        if(auto err = ensure_reading(self, stream::Self::read_mode::direct, on_alloc, on_read)) {
            out = std::unexpected(err);
            self->reader.disarm();
            return waiting;
        }

        return this->link_continuation(&waiting.promise(), location);
    }

    result<std::size_t> await_resume() noexcept {
        if(self) {
            self->reader.disarm();
        }
        return std::move(out);
    }
};

struct stream_write_await : uv::await_op<stream_write_await> {
    using promise_t = task<error>::promise_type;

    // Stream self that owns the active write waiter.
    stream::Self* self;
    // Owns outbound bytes until libuv invokes on_write().
    std::vector<char> storage;
    // libuv write request; req.data points back to this awaiter.
    uv_write_t req{};
    // Completion status returned from await_resume().
    error error_code;

    stream_write_await(stream::Self* self, std::span<const char> data) :
        self(self), storage(data.begin(), data.end()) {}

    static void on_cancel(system_op* op) {
        auto* aw = static_cast<stream_write_await*>(op);
        if(!aw->self) {
            return;
        }
        // uv_write_t is not cancellable via uv_cancel().
        // Keep the request in-flight and wait for on_write() to retire it.
    }

    static void on_write(uv_write_t* req, int status) {
        auto* aw = static_cast<stream_write_await*>(req->data);
        assert(aw != nullptr && "on_write requires awaiter in req->data");
        assert(aw->self != nullptr && "on_write requires stream state");

        aw->mark_cancelled_if(status);

        auto err = uv::status_to_error(status);
        if(err) {
            aw->error_code = err;
        }

        if(aw->self->writer.has_waiter()) {
            auto* w = aw->self->writer.waiter;
            aw->self->writer.disarm();
            w->complete();
        }
    }

    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<promise_t> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        if(!self) {
            return waiting;
        }

        self->writer.arm(*this);
        req.data = this;

        uv_buf_t buf = uv::buf_init(storage.empty() ? nullptr : storage.data(),
                                    static_cast<unsigned>(storage.size()));
        if(auto err = uv::write(req, self->stream, std::span<const uv_buf_t>{&buf, 1}, on_write)) {
            error_code = err;
            self->writer.disarm();
            return waiting;
        }

        return this->link_continuation(&waiting.promise(), location);
    }

    error await_resume() noexcept {
        if(self) {
            self->writer.disarm();
        }
        return this->error_code;
    }
};

}  // namespace

stream::stream() noexcept = default;

stream::stream(stream&& other) noexcept = default;

stream& stream::operator=(stream&& other) noexcept = default;

stream::~stream() = default;

stream::Self* stream::operator->() noexcept {
    return self.get();
}

void* stream::handle() noexcept {
    return self ? &self->stream : nullptr;
}

const void* stream::handle() const noexcept {
    return self ? &self->stream : nullptr;
}

handle_type guess_handle(int fd) {
    switch(uv::guess_handle(fd)) {
        case UV_FILE: return handle_type::file;
        case UV_TTY: return handle_type::tty;
        case UV_NAMED_PIPE: return handle_type::pipe;
        case UV_TCP: return handle_type::tcp;
        case UV_UDP: return handle_type::udp;
        default: return handle_type::unknown;
    }
}

task<result<std::string>> stream::read() {
    if(!self) {
        co_return std::unexpected(error::invalid_argument);
    }

    if(self->buffer.readable_bytes() == 0) {
        if(auto err = co_await stream_read_await{self.get()}) {
            co_return std::unexpected(err);
        }
    }

    std::string out;
    out.resize(self->buffer.readable_bytes());
    self->buffer.read(out.data(), out.size());
    co_return out;
}

task<result<std::size_t>> stream::read_some(std::span<char> dst) {
    if(!self) {
        co_return std::unexpected(error::invalid_argument);
    }

    if(dst.empty()) {
        co_return std::size_t{0};
    }

    if(self->buffer.readable_bytes() != 0) {
        const auto available = self->buffer.readable_bytes();
        const auto to_read = std::min(dst.size(), available);
        self->buffer.read(dst.data(), to_read);
        co_return to_read;
    }

    co_return co_await stream_read_some_await{self.get(), dst};
}

task<result<stream::chunk>> stream::read_chunk() {
    chunk out{};
    if(!self) {
        co_return std::unexpected(error::invalid_argument);
    }

    if(self->buffer.readable_bytes() == 0) {
        if(auto err = co_await stream_read_await{self.get()}) {
            co_return std::unexpected(err);
        }
    }

    auto [ptr, len] = self->buffer.get_read_ptr();
    out = std::span<const char>(ptr, len);
    co_return out;
}

void stream::consume(std::size_t n) {
    if(!self) {
        return;
    }

    self->buffer.advance_read(n);
}

task<error> stream::write(std::span<const char> data) {
    if(!self || !self->initialized() || data.empty()) {
        co_return error::invalid_argument;
    }

    if(self->writer.has_waiter()) {
        assert(false && "stream::write supports a single writer at a time");
        co_return error::invalid_argument;
    }

    co_return co_await stream_write_await{self.get(), data};
}

result<std::size_t> stream::try_write(std::span<const char> data) {
    if(!self || !self->initialized()) {
        return std::unexpected(error::invalid_argument);
    }

    if(data.empty()) {
        return std::size_t{0};
    }

    if(data.size() > static_cast<std::size_t>(std::numeric_limits<unsigned>::max())) {
        return std::unexpected(error::value_too_large_for_defined_data_type);
    }

    uv_buf_t buf = uv::buf_init(const_cast<char*>(data.data()), static_cast<unsigned>(data.size()));
    auto res = uv::try_write(self->stream, std::span<const uv_buf_t>{&buf, 1});
    if(!res) {
        return std::unexpected(res.error());
    }

    return *res;
}

bool stream::readable() const noexcept {
    if(!self || !self->initialized()) {
        return false;
    }

    return uv::is_readable(self->stream);
}

bool stream::writable() const noexcept {
    if(!self || !self->initialized()) {
        return false;
    }

    return uv::is_writable(self->stream);
}

error stream::set_blocking(bool enabled) {
    if(!self || !self->initialized()) {
        return error::invalid_argument;
    }

    if(auto err = uv::stream_set_blocking(self->stream, enabled)) {
        return err;
    }

    return {};
}

stream::stream(unique_handle<Self> self) noexcept : self(std::move(self)) {}

}  // namespace eventide
