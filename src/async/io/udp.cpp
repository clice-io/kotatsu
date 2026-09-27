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

    /// The pending recv, woken once the queue has something for it.
    uv::waiter_slot<void> slot;

    /// Datagrams and errors not taken by recv() yet, oldest first.
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
            recv_flags received_flags{
                .partial = (flags & UV_UDP_PARTIAL) != 0,
                .mmsg_chunk = (flags & UV_UDP_MMSG_CHUNK) != 0,
            };
            return recv_result{
                .data = std::string(buf->base, static_cast<std::size_t>(nread)),
                .sender = sender ? std::move(*sender) : endpoint{},
                .flags = received_flags,
            };
        }();

        if(self->received.size() < backlog) {
            self->received.push_back(std::move(got));
        }

        // Nothing may stop receiving inside this callback, as a task resumed
        // here could: after an EPOLLERR, libuv on Linux reads the socket's
        // error queue next and asserts that the socket still receives. So
        // the pending recv() resumes on a later turn, and the queue does not
        // make room by stopping either.
        if(self->slot.waiting()) {
            self->slot.deliver_later(*handle->loop);
        }
    }
};

namespace {

uv_membership to_uv(udp::membership m) {
    return m == udp::membership::join ? UV_JOIN_GROUP : UV_LEAVE_GROUP;
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
        return submitted(::uv_udp_send(&req, socket, &buf, 1, addr, on_done));
    }
};

}  // namespace

udp::udp() noexcept = default;

udp::udp(detail::unique_handle<Self> self) noexcept : self(std::move(self)) {}

udp::~udp() = default;

udp::udp(udp&& other) noexcept = default;

udp& udp::operator=(udp&& other) noexcept = default;

udp udp::create(event_loop& loop) {
    return create(create_options{}, loop);
}

udp udp::create(create_options options, event_loop& loop) {
    auto self = Self::make();
    ::uv_udp_init_ex(loop.native_handle(),
                     &self->udp,
                     options.recvmmsg ? static_cast<unsigned int>(UV_UDP_RECVMMSG) : 0U);
    return udp(std::move(self));
}

result<udp> udp::open(int fd, event_loop& loop) {
    auto opened = create(loop);
    if(auto err = error(::uv_udp_open(&opened.self->udp, fd))) {
        return outcome_error(err);
    }
    return opened;
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

    unsigned int flags = 0;
    if(options.ipv6_only) {
        flags |= UV_UDP_IPV6ONLY;
    }
    if(options.reuse_addr) {
        flags |= UV_UDP_REUSEADDR;
    }
    if(options.reuse_port) {
        flags |= UV_UDP_REUSEPORT;
    }
    return error(::uv_udp_bind(&self->udp, reinterpret_cast<const sockaddr*>(&*addr), flags));
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

    // The recv a datagram woke holds the slot until it resumes and takes it.
    if(self->slot.taken()) {
        co_await fail(error::resource_busy_or_locked);
    }

    if(self->received.empty()) {
        if(!self->receiving) {
            self->buffer.resize(64 * 1024);
            if(auto err = error(::uv_udp_recv_start(&self->udp, Self::on_alloc, Self::on_recv))) {
                co_await fail(err);
            }
            self->receiving = true;
        }
        if(auto err = co_await self->slot.wait()) {
            co_await fail(err);
        }
    }

    auto next = std::move(self->received.front());
    self->received.pop_front();
    co_return std::move(next);
}

error udp::stop() {
    if(!self) {
        return error::invalid_argument;
    }

    ::uv_udp_recv_stop(&self->udp);
    self->receiving = false;
    self->slot.abort(*self->handle.loop);
    return {};
}

result<endpoint> udp::getsockname() const {
    if(!self) {
        return outcome_error(error::invalid_argument);
    }
    return uv::name_of(self->udp, ::uv_udp_getsockname);
}

result<endpoint> udp::getpeername() const {
    if(!self) {
        return outcome_error(error::invalid_argument);
    }
    return uv::name_of(self->udp, ::uv_udp_getpeername);
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
