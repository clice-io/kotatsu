#include <cassert>
#include <memory>
#include <type_traits>
#include <utility>

#include "awaiter.h"
#include "eventide/async/loop.h"

namespace eventide {

namespace {

template <typename T>
constexpr inline bool always_false_v = false;

result<unsigned int> to_uv_pipe_flags(const pipe::options& opts) {
    unsigned int out = 0;
#ifdef UV_PIPE_NO_TRUNCATE
    if(opts.no_truncate) {
        out |= UV_PIPE_NO_TRUNCATE;
    }
#else
    if(opts.no_truncate) {
        return std::unexpected(error::function_not_implemented);
    }
#endif
    return out;
}

result<unsigned int> to_uv_pipe_connect_flags(const pipe::options& opts) {
    return to_uv_pipe_flags(opts);
}

result<unsigned int> to_uv_tcp_bind_flags(const tcp_socket::options& opts) {
    unsigned int out = 0;
#ifdef UV_TCP_IPV6ONLY
    if(opts.ipv6_only) {
        out |= UV_TCP_IPV6ONLY;
    }
#else
    if(opts.ipv6_only) {
        return std::unexpected(error::function_not_implemented);
    }
#endif
#ifdef UV_TCP_REUSEPORT
    if(opts.reuse_port) {
        out |= UV_TCP_REUSEPORT;
    }
#else
    if(opts.reuse_port) {
        return std::unexpected(error::function_not_implemented);
    }
#endif
    return out;
}

template <typename Stream>
struct accept_await : uv::await_op<accept_await<Stream>> {
    using await_base = uv::await_op<accept_await<Stream>>;
    using promise_t = task<result<Stream>>::promise_type;
    using self_t = typename acceptor<Stream>::Self;

    // Acceptor self used for waiter registration and pending queueing.
    self_t* self;
    // Result slot populated by connection callbacks.
    result<Stream> outcome = std::unexpected(error());

    explicit accept_await(self_t* acceptor) : self(acceptor) {}

    static void on_cancel(system_op* op) {
        await_base::complete_cancel(op, [](auto& aw) {
            if(aw.self) {
                aw.self->disarm();
            }
        });
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
        self->arm(*this, outcome);
        return this->link_continuation(&waiting.promise(), location);
    }

    result<Stream> await_resume() noexcept {
        if(self) {
            self->disarm();
        }
        return std::move(outcome);
    }
};

template <typename Stream>
void on_connection(uv_stream_t* server, int status) {
    using self_t = typename acceptor<Stream>::Self;

    assert(server != nullptr && "on_connection requires non-null server");
    auto* listener = static_cast<self_t*>(server->data);
    assert(listener != nullptr && "on_connection requires listener state in server->data");

    auto err = uv::status_to_error(status);
    if(err) {
        listener->deliver(err);
        return;
    }

    auto self = stream::Self::make();
    if constexpr(std::is_same_v<Stream, pipe>) {
        err = uv::pipe_init(*server->loop, self->pipe, listener->pipe_ipc);
    } else if constexpr(std::is_same_v<Stream, tcp_socket>) {
        err = uv::tcp_init(*server->loop, self->tcp);
    } else {
        static_assert(always_false_v<Stream>, "unsupported accept stream type");
    }

    if(!err) {
        err = uv::accept(*server, self->stream);
    }

    if(err) {
        listener->deliver(err);
    } else {
        listener->deliver(Stream(std::move(self)));
    }
}

template <typename Stream>
struct connect_await : uv::await_op<connect_await<Stream>> {
    using await_base = uv::await_op<connect_await<Stream>>;
    using promise_t = task<result<Stream>>::promise_type;
    using self_ptr = stream::Self::pointer;

    // Candidate stream self; reset on cancel to close the handle.
    self_ptr self;
    // libuv connect request; req.data points back to this awaiter.
    uv_connect_t req{};
    // Pipe name kept alive for uv_pipe_connect2().
    std::string name;
    // Pipe connect flags.
    unsigned int flags = 0;
    // Resolved peer address for uv_tcp_connect().
    sockaddr_storage addr{};
    // Result slot returned from await_resume().
    result<Stream> outcome = std::unexpected(error());
    // Constructor-level validation flag.
    bool ready = true;

    connect_await(self_ptr self, std::string_view name, pipe::options opts) :
        self(std::move(self)), name(name) {
        if constexpr(std::is_same_v<Stream, pipe>) {
            if(this->name.empty()) {
                ready = false;
                outcome = std::unexpected(error::invalid_argument);
                return;
            }

            auto uv_flags = to_uv_pipe_connect_flags(opts);
            if(!uv_flags) {
                ready = false;
                outcome = std::unexpected(uv_flags.error());
                return;
            }
            flags = uv_flags.value();
        } else {
            static_assert(always_false_v<Stream>, "pipe constructor requires Stream=pipe");
        }
    }

    connect_await(self_ptr self, std::string_view host, int port) : self(std::move(self)) {
        if constexpr(std::is_same_v<Stream, tcp_socket>) {
            auto resolved = uv::resolve_addr(host, port);
            if(!resolved) {
                ready = false;
                outcome = std::unexpected(resolved.error());
                return;
            }
            addr = resolved->storage;
        } else {
            static_assert(always_false_v<Stream>, "tcp constructor requires Stream=tcp_socket");
        }
    }

    static void on_cancel(system_op* op) {
        auto* aw = static_cast<connect_await*>(op);
        if(aw->self) {
            // uv_connect_t can't be cancelled; close handle to trigger UV_ECANCELED callback.
            aw->self.reset();
        }
    }

    static void on_connect(uv_connect_t* req, int status) {
        auto* aw = static_cast<connect_await*>(req->data);
        assert(aw != nullptr && "on_connect requires awaiter in req->data");

        aw->mark_cancelled_if(status);

        auto err = uv::status_to_error(status);
        if(err) {
            aw->outcome = std::unexpected(err);
        } else if(aw->self) {
            if constexpr(std::is_same_v<Stream, pipe>) {
                aw->outcome = pipe(std::move(aw->self));
            } else if constexpr(std::is_same_v<Stream, tcp_socket>) {
                aw->outcome = tcp_socket(std::move(aw->self));
            } else {
                static_assert(always_false_v<Stream>, "unsupported connect stream type");
            }
        } else {
            aw->outcome = std::unexpected(error::invalid_argument);
        }

        aw->complete();
    }

    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<promise_t> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        if(!self || !ready) {
            return waiting;
        }

        req.data = this;

        error err{};
        if constexpr(std::is_same_v<Stream, pipe>) {
            err = uv::pipe_connect2(req, self->pipe, name.c_str(), name.size(), flags, on_connect);
        } else if constexpr(std::is_same_v<Stream, tcp_socket>) {
            err = uv::tcp_connect(req,
                                  self->tcp,
                                  reinterpret_cast<const sockaddr*>(&addr),
                                  on_connect);
        } else {
            static_assert(always_false_v<Stream>, "unsupported connect stream type");
        }

        if(err) {
            outcome = std::unexpected(err);
            return waiting;
        }

        return this->link_continuation(&waiting.promise(), location);
    }

    result<Stream> await_resume() noexcept {
        return std::move(outcome);
    }
};

}  // namespace

template <typename Stream>
acceptor<Stream>::acceptor() noexcept = default;

template <typename Stream>
acceptor<Stream>::acceptor(acceptor&& other) noexcept = default;

template <typename Stream>
acceptor<Stream>& acceptor<Stream>::operator=(acceptor&& other) noexcept = default;

template <typename Stream>
acceptor<Stream>::~acceptor() = default;

template <typename Stream>
typename acceptor<Stream>::Self* acceptor<Stream>::operator->() noexcept {
    return self.get();
}

template <typename Stream>
task<result<Stream>> acceptor<Stream>::accept() {
    if(!self) {
        co_return std::unexpected(error::invalid_argument);
    }

    if(self->has_pending()) {
        co_return self->take_pending();
    }

    if(self->has_waiter()) {
        co_return std::unexpected(error::connection_already_in_progress);
    }

    co_return co_await accept_await<Stream>{self.get()};
}

template <typename Stream>
error acceptor<Stream>::stop() {
    if(!self) {
        return error::invalid_argument;
    }

    self->deliver(error::operation_aborted);

    return {};
}

template <typename Stream>
acceptor<Stream>::acceptor(unique_handle<Self> self) noexcept : self(std::move(self)) {}

template class acceptor<pipe>;
template class acceptor<tcp_socket>;

result<pipe> pipe::open(int fd, pipe::options opts, event_loop& loop) {
    auto pipe_res = create(opts, loop);
    if(!pipe_res) {
        return std::unexpected(pipe_res.error());
    }

    auto& handle = pipe_res->self->pipe;
    if(auto err = uv::pipe_open(handle, fd)) {
        return std::unexpected(err);
    }

    return std::move(*pipe_res);
}

result<pipe::acceptor> pipe::listen(std::string_view name, pipe::options opts, event_loop& loop) {
    auto self = pipe::acceptor::Self::make();
    if(auto err = uv::pipe_init(loop, self->pipe, opts.ipc ? 1 : 0)) {
        return std::unexpected(err);
    }

    auto& acc = *self;
    acc.pipe_ipc = opts.ipc ? 1 : 0;
    auto& handle = acc.pipe;

    auto uv_flags = to_uv_pipe_flags(opts);
    if(!uv_flags) {
        return std::unexpected(uv_flags.error());
    }

    if(name.empty()) {
        return std::unexpected(error::invalid_argument);
    }

    if(auto err = uv::pipe_bind2(handle, name.data(), name.size(), uv_flags.value())) {
        return std::unexpected(err);
    }

    if(auto err = uv::listen(handle, opts.backlog, on_connection<pipe>)) {
        return std::unexpected(err);
    }

    return pipe::acceptor(std::move(self));
}

pipe::pipe(unique_handle<Self> self) noexcept : stream(std::move(self)) {}

result<pipe> pipe::create(pipe::options opts, event_loop& loop) {
    auto self = Self::make();
    if(auto err = uv::pipe_init(loop, self->pipe, opts.ipc ? 1 : 0)) {
        return std::unexpected(err);
    }

    return pipe(std::move(self));
}

task<result<pipe>> pipe::connect(std::string_view name, pipe::options opts, event_loop& loop) {
    auto self = Self::make();
    if(auto err = uv::pipe_init(loop, self->pipe, opts.ipc ? 1 : 0)) {
        co_return std::unexpected(err);
    }

    co_return co_await connect_await<pipe>{std::move(self), name, opts};
}

tcp_socket::tcp_socket(unique_handle<Self> self) noexcept : stream(std::move(self)) {}

result<tcp_socket> tcp_socket::open(int fd, event_loop& loop) {
    auto self = Self::make();
    if(auto err = uv::tcp_init(loop, self->tcp)) {
        return std::unexpected(err);
    }

    if(auto err = uv::tcp_open(self->tcp, fd)) {
        return std::unexpected(err);
    }

    return tcp_socket(std::move(self));
}

task<result<tcp_socket>> tcp_socket::connect(std::string_view host, int port, event_loop& loop) {
    auto self = Self::make();
    if(auto err = uv::tcp_init(loop, self->tcp)) {
        co_return std::unexpected(err);
    }

    co_return co_await connect_await<tcp_socket>{std::move(self), host, port};
}

result<tcp_socket::acceptor> tcp_socket::listen(std::string_view host,
                                                int port,
                                                tcp_socket::options opts,
                                                event_loop& loop) {
    auto self = tcp_socket::acceptor::Self::make();
    if(auto err = uv::tcp_init(loop, self->tcp)) {
        return std::unexpected(err);
    }

    auto& acc = *self;
    auto& handle = acc.tcp;

    auto resolved = uv::resolve_addr(host, port);
    if(!resolved) {
        return std::unexpected(resolved.error());
    }

    ::sockaddr* addr_ptr = reinterpret_cast<sockaddr*>(&resolved->storage);

    auto uv_flags = to_uv_tcp_bind_flags(opts);
    if(!uv_flags) {
        return std::unexpected(uv_flags.error());
    }

    if(auto err = uv::tcp_bind(handle, addr_ptr, uv_flags.value())) {
        return std::unexpected(err);
    }

    if(auto err = uv::listen(handle, opts.backlog, on_connection<tcp_socket>)) {
        return std::unexpected(err);
    }

    return tcp_socket::acceptor(std::move(self));
}

}  // namespace eventide
