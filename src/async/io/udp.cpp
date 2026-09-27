#include "kota/async/io/udp.h"

#include <cstddef>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "awaiter.h"

namespace kota {

struct udp::Self : uv::owned_handle<Self> {
    /// Datagrams nobody waited for that the socket keeps.
    constexpr static std::size_t backlog = 64;

    union {
        uv_handle_t handle;
        uv_udp_t udp;
    };

    /// The pending recv.
    uv::waiter_slot<recv_result> slot;

    /// Datagrams and errors that arrived while no recv() waited, oldest
    /// first.
    std::deque<result<recv_result>> received;

    /// Room for one datagram of the largest size; allocated by the first
    /// recv().
    std::vector<char> buffer;

    bool receiving = false;

    static void on_alloc(uv_handle_t* handle, std::size_t, uv_buf_t* buf) {
        auto& buffer = static_cast<Self*>(handle->data)->buffer;
        *buf = ::uv_buf_init(buffer.data(), static_cast<unsigned int>(buffer.size()));
    }

    static void on_recv(uv_udp_t* handle,
                        ssize_t nread,
                        const uv_buf_t* buf,
                        const sockaddr* addr,
                        unsigned flags) {
        auto* self = static_cast<Self*>(handle->data);
        // Zero bytes from no address is libuv reporting that the socket is
        // drained (or releasing a recvmmsg buffer), not an empty datagram:
        // an empty datagram always comes with its sender.
        if(nread == 0 && addr == nullptr) {
            return;
        }

        auto got = [&]() -> result<recv_result> {
            if(nread < 0) {
#ifdef _WIN32
                // libuv on Windows stops receiving before it reports an
                // error, so the next recv() has to start it again.
                self->receiving = false;
#endif
                return outcome_error(uv::status_to_error(nread));
            }
            auto sender = uv::endpoint_of(*addr);
            return recv_result{
                .data = std::string(buf->base, static_cast<std::size_t>(nread)),
                .sender = sender ? std::move(*sender) : endpoint{},
                .flags = {.partial = (flags & UV_UDP_PARTIAL) != 0,
                                                                 .mmsg_chunk = (flags & UV_UDP_MMSG_CHUNK) != 0},
            };
        }();

        // Receiving is not stopped here to make room: on Linux libuv goes on
        // to read the socket's error queue after this callback, and asserts
        // that the socket still receives.
        if(self->slot.waiting()) {
            self->slot.deliver(std::move(got));
        } else if(self->received.size() < backlog) {
            self->received.push_back(std::move(got));
        }
    }
};

namespace {

unsigned int bind_flags(const udp::bind_options& options) {
    unsigned int out = 0;
    if(options.ipv6_only) {
        out |= UV_UDP_IPV6ONLY;
    }
    if(options.reuse_addr) {
        out |= UV_UDP_REUSEADDR;
    }
    if(options.reuse_port) {
        out |= UV_UDP_REUSEPORT;
    }
    return out;
}

uv_membership to_uv(udp::membership m) {
    return m == udp::membership::join ? UV_JOIN_GROUP : UV_LEAVE_GROUP;
}

/// The name libuv reports for a socket, as an endpoint.
template <typename Query>
result<endpoint> name_of(Query query, const uv_udp_t& socket) {
    sockaddr_storage name{};
    int length = sizeof(name);
    if(auto err = error(query(&socket, reinterpret_cast<sockaddr*>(&name), &length))) {
        return outcome_error(err);
    }
    return uv::endpoint_of(reinterpret_cast<const sockaddr&>(name));
}

/// libuv sends a datagram it has taken whatever happens to its task, and
/// ends it with ECANCELED if the socket closes first.
struct send_op : uv::request_op<send_op, uv_udp_send_t> {
    uv_udp_t* socket;
    uv_buf_t buf;
    /// Where to, or null for the connected peer.
    const sockaddr* addr;

    send_op(uv_udp_t* socket, uv_buf_t buf, const sockaddr* addr) noexcept :
        socket(socket), buf(buf), addr(addr) {}

    bool start() noexcept {
        req.data = this;
        return submitted(::uv_udp_send(&req, socket, &buf, 1, addr, on_done));
    }

    void cancel() noexcept {}
};

}  // namespace

udp::udp() noexcept = default;

udp::udp(unique_handle<Self> self) noexcept : self(std::move(self)) {}

udp::~udp() = default;

udp::udp(udp&& other) noexcept = default;

udp& udp::operator=(udp&& other) noexcept = default;

result<udp> udp::create(event_loop& loop) {
    return create(create_options{}, loop);
}

result<udp> udp::create(create_options options, event_loop& loop) {
    auto self = Self::make();
    ::uv_udp_init_ex(loop.native_handle(), &self->udp, options.recvmmsg ? UV_UDP_RECVMMSG : 0U);
    return udp(std::move(self));
}

result<udp> udp::open(int fd, event_loop& loop) {
    auto self = Self::make();
    ::uv_udp_init(loop.native_handle(), &self->udp);
    if(auto err = error(::uv_udp_open(&self->udp, fd))) {
        return outcome_error(err);
    }
    return udp(std::move(self));
}

error udp::bind(std::string_view host, int port) {
    return bind(host, port, bind_options{});
}

error udp::bind(std::string_view host, int port, bind_options options) {
    if(!self) {
        return error::invalid_argument;
    }

    auto addr = uv::resolve_addr(host, port);
    if(!addr) {
        return addr.error();
    }
    return error(
        ::uv_udp_bind(&self->udp, reinterpret_cast<const sockaddr*>(&*addr), bind_flags(options)));
}

error udp::connect(std::string_view host, int port) {
    if(!self) {
        return error::invalid_argument;
    }

    auto addr = uv::resolve_addr(host, port);
    if(!addr) {
        return addr.error();
    }
    return error(::uv_udp_connect(&self->udp, reinterpret_cast<const sockaddr*>(&*addr)));
}

error udp::disconnect() {
    if(!self) {
        return error::invalid_argument;
    }
    return error(::uv_udp_connect(&self->udp, nullptr));
}

task<void, error> udp::send(std::span<const char> data, std::string_view host, int port) {
    if(!self) {
        co_await fail(error::invalid_argument);
    }

    auto addr = co_await or_fail(uv::resolve_addr(host, port));
    if(auto err = co_await send_op(&self->udp,
                                   uv::buffer_of(data),
                                   reinterpret_cast<const sockaddr*>(&addr))) {
        co_await fail(err);
    }
}

task<void, error> udp::send(std::span<const char> data) {
    if(!self) {
        co_await fail(error::invalid_argument);
    }

    if(auto err = co_await send_op(&self->udp, uv::buffer_of(data), nullptr)) {
        co_await fail(err);
    }
}

error udp::try_send(std::span<const char> data, std::string_view host, int port) {
    if(!self) {
        return error::invalid_argument;
    }

    auto addr = uv::resolve_addr(host, port);
    if(!addr) {
        return addr.error();
    }
    auto buf = uv::buffer_of(data);
    return uv::status_to_error(
        ::uv_udp_try_send(&self->udp, &buf, 1, reinterpret_cast<const sockaddr*>(&*addr)));
}

error udp::try_send(std::span<const char> data) {
    if(!self) {
        return error::invalid_argument;
    }

    auto buf = uv::buffer_of(data);
    return uv::status_to_error(::uv_udp_try_send(&self->udp, &buf, 1, nullptr));
}

task<udp::recv_result, error> udp::recv() {
    if(!self) {
        co_await fail(error::invalid_argument);
    }

    if(!self->received.empty()) {
        auto next = std::move(self->received.front());
        self->received.pop_front();
        co_return std::move(next);
    }

    if(!self->receiving) {
        self->buffer.resize(64 * 1024);
        if(auto err = error(::uv_udp_recv_start(&self->udp, Self::on_alloc, Self::on_recv))) {
            co_await fail(err);
        }
        self->receiving = true;
    }

    co_return co_await self->slot.wait();
}

error udp::stop() {
    if(!self) {
        return error::invalid_argument;
    }

    ::uv_udp_recv_stop(&self->udp);
    self->receiving = false;
    self->slot.abort(*self->handle.loop, error::operation_aborted);
    return {};
}

result<endpoint> udp::getsockname() const {
    if(!self) {
        return outcome_error(error::invalid_argument);
    }
    return name_of(::uv_udp_getsockname, self->udp);
}

result<endpoint> udp::getpeername() const {
    if(!self) {
        return outcome_error(error::invalid_argument);
    }
    return name_of(::uv_udp_getpeername, self->udp);
}

error udp::set_membership(std::string_view multicast_addr,
                          std::string_view interface_addr,
                          membership m) {
    if(!self) {
        return error::invalid_argument;
    }
    return error(::uv_udp_set_membership(&self->udp,
                                         std::string(multicast_addr).c_str(),
                                         std::string(interface_addr).c_str(),
                                         to_uv(m)));
}

error udp::set_source_membership(std::string_view multicast_addr,
                                 std::string_view interface_addr,
                                 std::string_view source_addr,
                                 membership m) {
    if(!self) {
        return error::invalid_argument;
    }
    return error(::uv_udp_set_source_membership(&self->udp,
                                                std::string(multicast_addr).c_str(),
                                                std::string(interface_addr).c_str(),
                                                std::string(source_addr).c_str(),
                                                to_uv(m)));
}

error udp::set_multicast_loop(bool on) {
    if(!self) {
        return error::invalid_argument;
    }
    return error(::uv_udp_set_multicast_loop(&self->udp, on ? 1 : 0));
}

error udp::set_multicast_ttl(int ttl) {
    if(!self) {
        return error::invalid_argument;
    }
    return error(::uv_udp_set_multicast_ttl(&self->udp, ttl));
}

error udp::set_multicast_interface(std::string_view interface_addr) {
    if(!self) {
        return error::invalid_argument;
    }
    return error(::uv_udp_set_multicast_interface(&self->udp, std::string(interface_addr).c_str()));
}

error udp::set_broadcast(bool on) {
    if(!self) {
        return error::invalid_argument;
    }
    return error(::uv_udp_set_broadcast(&self->udp, on ? 1 : 0));
}

error udp::set_ttl(int ttl) {
    if(!self) {
        return error::invalid_argument;
    }
    return error(::uv_udp_set_ttl(&self->udp, ttl));
}

bool udp::using_recvmmsg() const {
    return self && ::uv_udp_using_recvmmsg(&self->udp);
}

std::size_t udp::send_queue_size() const {
    return self ? ::uv_udp_get_send_queue_size(&self->udp) : 0;
}

std::size_t udp::send_queue_count() const {
    return self ? ::uv_udp_get_send_queue_count(&self->udp) : 0;
}

}  // namespace kota
