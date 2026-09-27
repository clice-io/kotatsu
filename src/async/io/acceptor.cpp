#include <cstddef>
#include <utility>

#include "stream_self.h"
#include "kota/support/functional.h"

namespace kota {

template <typename Stream>
struct acceptor<Stream>::Self : uv::owned_handle<Self> {
    union {
        uv_handle_t handle;
        uv_stream_t stream;
        uv_pipe_t pipe;
        uv_tcp_t tcp;
    };

    /// The pending accept.
    uv::waiter_slot<Stream> slot;

    /// Connections that arrived while no accept() waited, which libuv holds
    /// until uv_accept() takes them: on Unix it stops listening meanwhile,
    /// so the kernel's backlog holds the rest.
    std::size_t ready = 0;

    /// What ended listening; only libuv on Windows reports that.
    error failed;

    result<Stream> accept_one() {
        auto client = stream::Self::make();
        if constexpr(std::same_as<Stream, kota::pipe>) {
            ::uv_pipe_init(stream.loop, &client->pipe, pipe.ipc);
        } else {
            ::uv_tcp_init(stream.loop, &client->tcp);
        }
        if(auto err = error(::uv_accept(&stream, &client->stream))) {
            return outcome_error(err);
        }
        return Stream(std::move(client));
    }

    static void on_connection(uv_stream_t* server, int status) {
        auto* self = static_cast<Self*>(server->data);
        if(status < 0) {
            self->failed = uv::status_to_error(status);
            if(self->slot.waiting()) {
                self->slot.deliver(outcome_error(self->failed));
            }
        } else if(self->slot.waiting()) {
            self->slot.deliver(self->accept_one());
        } else {
            self->ready += 1;
        }
    }
};

template <typename Stream>
acceptor<Stream>::acceptor() noexcept = default;

template <typename Stream>
acceptor<Stream>::acceptor(acceptor&& other) noexcept = default;

template <typename Stream>
acceptor<Stream>& acceptor<Stream>::operator=(acceptor&& other) noexcept = default;

template <typename Stream>
acceptor<Stream>::~acceptor() = default;

template <typename Stream>
acceptor<Stream>::acceptor(unique_handle<Self> self) noexcept : self(std::move(self)) {}

template <typename Stream>
task<Stream, error> acceptor<Stream>::accept() {
    if(!self) {
        co_await fail(error::invalid_argument);
    }

    if(self->failed) {
        co_await fail(self->failed);
    }

    if(self->ready > 0) {
        self->ready -= 1;
        co_return self->accept_one();
    }

    co_return co_await self->slot.wait();
}

template <typename Stream>
error acceptor<Stream>::stop() {
    if(!self) {
        return error::invalid_argument;
    }

    self->slot.abort(*self->handle.loop, error::operation_aborted);
    return {};
}

template <typename Stream>
result<endpoint> acceptor<Stream>::getsockname() const
    requires std::same_as<Stream, tcp> {
    if(!self) {
        return outcome_error(error::invalid_argument);
    }

    sockaddr_storage name{};
    int length = sizeof(name);
    if(auto err =
           error(::uv_tcp_getsockname(&self->tcp, reinterpret_cast<sockaddr*>(&name), &length))) {
        return outcome_error(err);
    }
    return uv::endpoint_of(reinterpret_cast<const sockaddr&>(name));
}

template class acceptor<pipe>;
template class acceptor<tcp>;

namespace {

/// Connects a new stream: `submit` hands the request to libuv. Cancelling
/// closes the stream, which makes libuv end the connect with ECANCELED.
template <typename Stream>
struct connect_op : uv::request_op<connect_op<Stream>, uv_connect_t> {
    Stream& connection;
    function_ref<int(uv_connect_t*, uv_connect_cb)> submit;

    connect_op(Stream& connection, function_ref<int(uv_connect_t*, uv_connect_cb)> submit) :
        connection(connection), submit(submit) {}

    bool start() noexcept {
        this->req.data = this;
        return this->submitted(submit(&this->req, &connect_op::on_done));
    }

    void cancel() noexcept {
        connection = Stream();
    }
};

unsigned int pipe_flags(const pipe::options& opts) {
    return opts.no_truncate ? UV_PIPE_NO_TRUNCATE : 0U;
}

}  // namespace

pipe::pipe(unique_handle<Self> self) noexcept : stream(std::move(self)) {}

pipe pipe::create(options opts, event_loop& loop) {
    auto self = Self::make();
    ::uv_pipe_init(loop.native_handle(), &self->pipe, opts.ipc);
    return pipe(std::move(self));
}

result<pipe> pipe::open(int fd, event_loop& loop) {
    return open(fd, options{}, loop);
}

result<pipe> pipe::open(int fd, options opts, event_loop& loop) {
    auto opened = create(opts, loop);
    if(auto err = error(::uv_pipe_open(&opened.self->pipe, fd))) {
        return outcome_error(err);
    }
    return opened;
}

task<pipe, error> pipe::connect(std::string_view name, event_loop& loop) {
    return connect(name, options{}, loop);
}

task<pipe, error> pipe::connect(std::string_view name, options opts, event_loop& loop) {
    auto connection = create(opts, loop);
    auto* handle = &connection.self->pipe;
    auto submit = [&](uv_connect_t* req, uv_connect_cb done) {
        return ::uv_pipe_connect2(req, handle, name.data(), name.size(), pipe_flags(opts), done);
    };
    if(auto err = co_await connect_op<pipe>(connection, submit)) {
        co_await fail(err);
    }
    co_return std::move(connection);
}

result<pipe::acceptor> pipe::listen(std::string_view name, event_loop& loop) {
    return listen(name, options{}, loop);
}

result<pipe::acceptor> pipe::listen(std::string_view name, options opts, event_loop& loop) {
    // An empty name would autobind to an abstract socket on Linux.
    if(name.empty()) {
        return outcome_error(error::invalid_argument);
    }

    auto self = acceptor::Self::make();
    ::uv_pipe_init(loop.native_handle(), &self->pipe, opts.ipc);
    if(auto err = error(::uv_pipe_bind2(&self->pipe, name.data(), name.size(), pipe_flags(opts)))) {
        return outcome_error(err);
    }
    if(auto err = error(::uv_listen(&self->stream, opts.backlog, acceptor::Self::on_connection))) {
        return outcome_error(err);
    }
    return acceptor(std::move(self));
}

tcp::tcp(unique_handle<Self> self) noexcept : stream(std::move(self)) {}

result<tcp> tcp::open(int fd, event_loop& loop) {
    auto self = Self::make();
    ::uv_tcp_init(loop.native_handle(), &self->tcp);
    if(auto err = error(::uv_tcp_open(&self->tcp, fd))) {
        return outcome_error(err);
    }
    return tcp(std::move(self));
}

task<tcp, error> tcp::connect(std::string_view host, int port, event_loop& loop) {
    auto addr = co_await or_fail(uv::resolve_addr(host, port));
    auto self = Self::make();
    ::uv_tcp_init(loop.native_handle(), &self->tcp);
    auto* handle = &self->tcp;
    tcp connection(std::move(self));
    auto submit = [&](uv_connect_t* req, uv_connect_cb done) {
        return ::uv_tcp_connect(req, handle, reinterpret_cast<const sockaddr*>(&addr), done);
    };
    if(auto err = co_await connect_op<tcp>(connection, submit)) {
        co_await fail(err);
    }
    co_return std::move(connection);
}

result<tcp::acceptor> tcp::listen(std::string_view host, int port, event_loop& loop) {
    return listen(host, port, options{}, loop);
}

result<tcp::acceptor> tcp::listen(std::string_view host, int port, options opts, event_loop& loop) {
    auto addr = uv::resolve_addr(host, port);
    if(!addr) {
        return outcome_error(addr.error());
    }

    unsigned int flags = 0;
    if(opts.ipv6_only) {
        flags |= UV_TCP_IPV6ONLY;
    }
    if(opts.reuse_port) {
        flags |= UV_TCP_REUSEPORT;
    }

    auto self = acceptor::Self::make();
    ::uv_tcp_init(loop.native_handle(), &self->tcp);
    if(auto err =
           error(::uv_tcp_bind(&self->tcp, reinterpret_cast<const sockaddr*>(&*addr), flags))) {
        return outcome_error(err);
    }
    if(auto err = error(::uv_listen(&self->stream, opts.backlog, acceptor::Self::on_connection))) {
        return outcome_error(err);
    }
    return acceptor(std::move(self));
}

}  // namespace kota
