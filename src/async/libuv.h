#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "uv.h"
#include "eventide/async/error.h"
#include "eventide/async/frame.h"
#include "eventide/async/owned.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#if defined(NDEBUG)
#if defined(_MSC_VER)
#define ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define ALWAYS_INLINE inline
#endif
#else
#define ALWAYS_INLINE inline
#endif

namespace eventide {

namespace uv {

template <typename T>
using bare_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T>
concept handle_like =
    std::same_as<bare_t<T>, uv_handle_t> || std::same_as<bare_t<T>, uv_stream_t> ||
    std::same_as<bare_t<T>, uv_tcp_t> || std::same_as<bare_t<T>, uv_pipe_t> ||
    std::same_as<bare_t<T>, uv_tty_t> || std::same_as<bare_t<T>, uv_udp_t> ||
    std::same_as<bare_t<T>, uv_timer_t> || std::same_as<bare_t<T>, uv_idle_t> ||
    std::same_as<bare_t<T>, uv_prepare_t> || std::same_as<bare_t<T>, uv_check_t> ||
    std::same_as<bare_t<T>, uv_signal_t> || std::same_as<bare_t<T>, uv_fs_event_t> ||
    std::same_as<bare_t<T>, uv_process_t>;

template <typename T>
concept stream_like = std::same_as<bare_t<T>, uv_stream_t> || std::same_as<bare_t<T>, uv_tcp_t> ||
                      std::same_as<bare_t<T>, uv_pipe_t> || std::same_as<bare_t<T>, uv_tty_t>;

template <typename T>
concept req_like = std::same_as<bare_t<T>, uv_req_t> || std::same_as<bare_t<T>, uv_fs_t> ||
                   std::same_as<bare_t<T>, uv_work_t> || std::same_as<bare_t<T>, uv_write_t> ||
                   std::same_as<bare_t<T>, uv_udp_send_t> || std::same_as<bare_t<T>, uv_connect_t>;

template <handle_like H>
ALWAYS_INLINE uv_handle_t* as_handle(H& handle) noexcept {
    if constexpr(std::same_as<bare_t<H>, uv_handle_t>) {
        return &handle;
    } else {
        return reinterpret_cast<uv_handle_t*>(&handle);
    }
}

template <handle_like H>
ALWAYS_INLINE const uv_handle_t* as_handle(const H& handle) noexcept {
    if constexpr(std::same_as<bare_t<H>, uv_handle_t>) {
        return &handle;
    } else {
        return reinterpret_cast<const uv_handle_t*>(&handle);
    }
}

template <stream_like S>
ALWAYS_INLINE uv_stream_t* as_stream(S& stream) noexcept {
    if constexpr(std::same_as<bare_t<S>, uv_stream_t>) {
        return &stream;
    } else {
        return reinterpret_cast<uv_stream_t*>(&stream);
    }
}

template <stream_like S>
ALWAYS_INLINE const uv_stream_t* as_stream(const S& stream) noexcept {
    if constexpr(std::same_as<bare_t<S>, uv_stream_t>) {
        return &stream;
    } else {
        return reinterpret_cast<const uv_stream_t*>(&stream);
    }
}

struct tty_winsize {
    int width = 0;
    int height = 0;
};

template <typename StatusT>
ALWAYS_INLINE error status_to_error(StatusT status) noexcept {
    if(static_cast<long long>(status) < 0) {
        return error(static_cast<int>(status));
    }
    return {};
}

template <handle_like H>
ALWAYS_INLINE bool is_active(const H& handle) noexcept {
    return ::uv_is_active(as_handle(handle)) != 0;
}

template <stream_like S>
ALWAYS_INLINE bool is_readable(const S& stream) noexcept {
    return ::uv_is_readable(as_stream(stream)) != 0;
}

template <stream_like S>
ALWAYS_INLINE bool is_writable(const S& stream) noexcept {
    return ::uv_is_writable(as_stream(stream)) != 0;
}

template <handle_like H>
ALWAYS_INLINE bool is_closing(const H& handle) noexcept {
    return ::uv_is_closing(as_handle(handle)) != 0;
}

template <handle_like H>
ALWAYS_INLINE void ref(H& handle) noexcept {
    ::uv_ref(as_handle(handle));
}

template <handle_like H>
ALWAYS_INLINE void unref(H& handle) noexcept {
    ::uv_unref(as_handle(handle));
}

template <handle_like H>
ALWAYS_INLINE void close(H& handle, uv_close_cb cb = nullptr) noexcept {
    ::uv_close(as_handle(handle), cb);
}

template <req_like R>
ALWAYS_INLINE error cancel(R& req) noexcept {
    // Errors: UV_EINVAL (req type not cancellable), UV_EBUSY (already running/done).
    return status_to_error(::uv_cancel(reinterpret_cast<uv_req_t*>(&req)));
}

ALWAYS_INLINE error loop_init(uv_loop_t& loop) noexcept {
    // Errors: UV_ENOMEM and platform init failures.
    return status_to_error(::uv_loop_init(&loop));
}

ALWAYS_INLINE error loop_close(uv_loop_t& loop) noexcept {
    // Errors: UV_EBUSY when active handles/requests remain.
    return status_to_error(::uv_loop_close(&loop));
}

ALWAYS_INLINE int run(uv_loop_t& loop, uv_run_mode mode) noexcept {
    return ::uv_run(&loop, mode);
}

ALWAYS_INLINE void stop(uv_loop_t& loop) noexcept {
    ::uv_stop(&loop);
}

ALWAYS_INLINE void walk(uv_loop_t& loop, uv_walk_cb cb, void* arg) noexcept {
    assert(cb != nullptr && "uv::walk requires non-null callback");
    ::uv_walk(&loop, cb, arg);
}

ALWAYS_INLINE void idle_init(uv_loop_t& loop, uv_idle_t& handle) noexcept {
    int rc = ::uv_idle_init(&loop, &handle);
    assert(rc == 0 && "uv::idle_init failed");
}

ALWAYS_INLINE void idle_start(uv_idle_t& handle, uv_idle_cb cb) noexcept {
    assert(cb != nullptr && "uv::idle_start requires non-null callback");
    int rc = ::uv_idle_start(&handle, cb);
    assert(rc == 0 && "uv::idle_start failed");
}

ALWAYS_INLINE void idle_stop(uv_idle_t& handle) noexcept {
    int rc = ::uv_idle_stop(&handle);
    assert(rc == 0 && "uv::idle_stop failed");
}

ALWAYS_INLINE void prepare_init(uv_loop_t& loop, uv_prepare_t& handle) noexcept {
    int rc = ::uv_prepare_init(&loop, &handle);
    assert(rc == 0 && "uv::prepare_init failed");
}

ALWAYS_INLINE void prepare_start(uv_prepare_t& handle, uv_prepare_cb cb) noexcept {
    assert(cb != nullptr && "uv::prepare_start requires non-null callback");
    int rc = ::uv_prepare_start(&handle, cb);
    assert(rc == 0 && "uv::prepare_start failed");
}

ALWAYS_INLINE void prepare_stop(uv_prepare_t& handle) noexcept {
    int rc = ::uv_prepare_stop(&handle);
    assert(rc == 0 && "uv::prepare_stop failed");
}

ALWAYS_INLINE void check_init(uv_loop_t& loop, uv_check_t& handle) noexcept {
    int rc = ::uv_check_init(&loop, &handle);
    assert(rc == 0 && "uv::check_init failed");
}

ALWAYS_INLINE void check_start(uv_check_t& handle, uv_check_cb cb) noexcept {
    assert(cb != nullptr && "uv::check_start requires non-null callback");
    int rc = ::uv_check_start(&handle, cb);
    assert(rc == 0 && "uv::check_start failed");
}

ALWAYS_INLINE void check_stop(uv_check_t& handle) noexcept {
    int rc = ::uv_check_stop(&handle);
    assert(rc == 0 && "uv::check_stop failed");
}

ALWAYS_INLINE void timer_init(uv_loop_t& loop, uv_timer_t& handle) noexcept {
    int rc = ::uv_timer_init(&loop, &handle);
    assert(rc == 0 && "uv::timer_init failed");
}

ALWAYS_INLINE void timer_start(uv_timer_t& handle,
                               uv_timer_cb cb,
                               std::uint64_t timeout,
                               std::uint64_t repeat) noexcept {
    assert(cb != nullptr && "uv::timer_start requires non-null callback");
    int rc = ::uv_timer_start(&handle, cb, timeout, repeat);
    assert(rc == 0 && "uv::timer_start failed");
}

ALWAYS_INLINE void timer_stop(uv_timer_t& handle) noexcept {
    int rc = ::uv_timer_stop(&handle);
    assert(rc == 0 && "uv::timer_stop failed");
}

ALWAYS_INLINE error signal_init(uv_loop_t& loop, uv_signal_t& handle) noexcept {
    // Errors: backend signal-loop init failures.
    return status_to_error(::uv_signal_init(&loop, &handle));
}

ALWAYS_INLINE error signal_start(uv_signal_t& handle, uv_signal_cb cb, int signum) noexcept {
    assert(cb != nullptr && "uv::signal_start requires non-null callback");
    // Errors: UV_EINVAL for invalid signum/cb and backend register failures.
    return status_to_error(::uv_signal_start(&handle, cb, signum));
}

ALWAYS_INLINE error signal_stop(uv_signal_t& handle) noexcept {
    return status_to_error(::uv_signal_stop(&handle));
}

ALWAYS_INLINE error queue_work(uv_loop_t& loop,
                               uv_work_t& req,
                               uv_work_cb work_cb,
                               uv_after_work_cb after_work_cb) noexcept {
    assert(work_cb != nullptr && "uv::queue_work requires non-null work callback");
    // Errors: UV_EINVAL for invalid arguments.
    return status_to_error(::uv_queue_work(&loop, &req, work_cb, after_work_cb));
}

ALWAYS_INLINE error fs_event_init(uv_loop_t& loop, uv_fs_event_t& handle) noexcept {
    return status_to_error(::uv_fs_event_init(&loop, &handle));
}

ALWAYS_INLINE error fs_event_start(uv_fs_event_t& handle,
                                   uv_fs_event_cb cb,
                                   const char* path,
                                   unsigned flags) noexcept {
    assert(cb != nullptr && path != nullptr &&
           "uv::fs_event_start requires non-null callback and path");
    // Errors: UV_EINVAL and backend/inotify errors.
    return status_to_error(::uv_fs_event_start(&handle, cb, path, flags));
}

ALWAYS_INLINE error fs_event_stop(uv_fs_event_t& handle) noexcept {
    return status_to_error(::uv_fs_event_stop(&handle));
}

template <stream_like S>
ALWAYS_INLINE error read_start(S& stream, uv_alloc_cb alloc_cb, uv_read_cb read_cb) noexcept {
    assert(alloc_cb != nullptr && read_cb != nullptr &&
           "uv::read_start requires non-null callbacks");
    // Errors: UV_EINVAL / UV_EALREADY / UV_ENOTCONN
    return status_to_error(::uv_read_start(as_stream(stream), alloc_cb, read_cb));
}

template <stream_like S>
ALWAYS_INLINE void read_stop(S& stream) noexcept {
    int rc = ::uv_read_stop(as_stream(stream));
    assert(rc == 0 && "uv::read_stop failed");
}

template <stream_like S>
ALWAYS_INLINE error
    write(uv_write_t& req, S& stream, std::span<const uv_buf_t> bufs, uv_write_cb cb) noexcept {
    assert(!bufs.empty() && "uv::write requires a non-empty buffer span");
    // Errors: stream state/fd/write precondition failures.
    return status_to_error(
        ::uv_write(&req, as_stream(stream), bufs.data(), static_cast<unsigned>(bufs.size()), cb));
}

template <stream_like Server, stream_like Client>
ALWAYS_INLINE error accept(Server& server, Client& client) noexcept {
    return status_to_error(::uv_accept(as_stream(server), as_stream(client)));
}

template <stream_like S>
ALWAYS_INLINE error listen(S& stream, int backlog, uv_connection_cb cb) noexcept {
    assert(cb != nullptr && "uv::listen requires non-null callback");
    return status_to_error(::uv_listen(as_stream(stream), backlog, cb));
}

ALWAYS_INLINE error pipe_init(uv_loop_t& loop, uv_pipe_t& handle, int ipc) noexcept {
    return status_to_error(::uv_pipe_init(&loop, &handle, ipc));
}

ALWAYS_INLINE error pipe_open(uv_pipe_t& handle, uv_file fd) noexcept {
    return status_to_error(::uv_pipe_open(&handle, fd));
}

ALWAYS_INLINE error pipe_bind2(uv_pipe_t& handle,
                               const char* name,
                               std::size_t namelen,
                               unsigned flags) noexcept {
    assert(name != nullptr && namelen > 0 && "uv::pipe_bind2 requires non-null non-empty name");
    return status_to_error(::uv_pipe_bind2(&handle, name, namelen, flags));
}

ALWAYS_INLINE error pipe_connect2(uv_connect_t& req,
                                  uv_pipe_t& handle,
                                  const char* name,
                                  std::size_t namelen,
                                  unsigned flags,
                                  uv_connect_cb cb) noexcept {
    assert(cb != nullptr && name != nullptr && namelen > 0 &&
           "uv::pipe_connect2 requires non-null callback and non-empty name");
    return status_to_error(::uv_pipe_connect2(&req, &handle, name, namelen, flags, cb));
}

ALWAYS_INLINE error tcp_init(uv_loop_t& loop, uv_tcp_t& handle) noexcept {
    return status_to_error(::uv_tcp_init(&loop, &handle));
}

ALWAYS_INLINE error tcp_open(uv_tcp_t& handle, uv_os_sock_t sock) noexcept {
    return status_to_error(::uv_tcp_open(&handle, sock));
}

ALWAYS_INLINE error tcp_bind(uv_tcp_t& handle, const sockaddr* addr, unsigned flags) noexcept {
    assert(addr != nullptr && "uv::tcp_bind requires non-null address");
    return status_to_error(::uv_tcp_bind(&handle, addr, flags));
}

ALWAYS_INLINE error tcp_connect(uv_connect_t& req,
                                uv_tcp_t& handle,
                                const sockaddr* addr,
                                uv_connect_cb cb) noexcept {
    assert(addr != nullptr && cb != nullptr &&
           "uv::tcp_connect requires non-null address and callback");
    return status_to_error(::uv_tcp_connect(&req, &handle, addr, cb));
}

ALWAYS_INLINE uv_handle_type guess_handle(uv_file file) noexcept {
    return ::uv_guess_handle(file);
}

template <stream_like S>
ALWAYS_INLINE result<std::size_t> try_write(S& stream, std::span<const uv_buf_t> bufs) noexcept {
    assert(!bufs.empty() && "uv::try_write requires a non-empty buffer span");
    int rc = ::uv_try_write(as_stream(stream), bufs.data(), static_cast<unsigned>(bufs.size()));
    if(rc < 0) {
        return std::unexpected(error(rc));
    }
    return static_cast<std::size_t>(rc);
}

template <stream_like S>
ALWAYS_INLINE error stream_set_blocking(S& stream, bool enabled) noexcept {
    return status_to_error(::uv_stream_set_blocking(as_stream(stream), enabled ? 1 : 0));
}

ALWAYS_INLINE error tty_init(uv_loop_t& loop,
                             uv_tty_t& handle,
                             uv_file fd,
                             bool readable) noexcept {
    return status_to_error(::uv_tty_init(&loop, &handle, fd, readable ? 1 : 0));
}

ALWAYS_INLINE error tty_set_mode(uv_tty_t& handle, uv_tty_mode_t mode) noexcept {
    return status_to_error(::uv_tty_set_mode(&handle, mode));
}

ALWAYS_INLINE error tty_reset_mode() noexcept {
    return status_to_error(::uv_tty_reset_mode());
}

ALWAYS_INLINE result<tty_winsize> tty_get_winsize(uv_tty_t& handle) noexcept {
    tty_winsize out{};
    int rc = ::uv_tty_get_winsize(&handle, &out.width, &out.height);
    if(rc != 0) {
        return std::unexpected(error(rc));
    }
    return out;
}

ALWAYS_INLINE void tty_set_vterm_state(uv_tty_vtermstate_t state) noexcept {
    ::uv_tty_set_vterm_state(state);
}

ALWAYS_INLINE result<uv_tty_vtermstate_t> tty_get_vterm_state() noexcept {
    uv_tty_vtermstate_t out = UV_TTY_UNSUPPORTED;
    int rc = ::uv_tty_get_vterm_state(&out);
    if(rc != 0) {
        return std::unexpected(error(rc));
    }
    return out;
}

ALWAYS_INLINE error udp_init(uv_loop_t& loop, uv_udp_t& handle) noexcept {
    return status_to_error(::uv_udp_init(&loop, &handle));
}

ALWAYS_INLINE error udp_init_ex(uv_loop_t& loop, uv_udp_t& handle, unsigned flags) noexcept {
    return status_to_error(::uv_udp_init_ex(&loop, &handle, flags));
}

ALWAYS_INLINE error udp_open(uv_udp_t& handle, uv_os_sock_t sock) noexcept {
    return status_to_error(::uv_udp_open(&handle, sock));
}

ALWAYS_INLINE error udp_bind(uv_udp_t& handle, const sockaddr* addr, unsigned flags) noexcept {
    assert(addr != nullptr && "uv::udp_bind requires non-null address");
    return status_to_error(::uv_udp_bind(&handle, addr, flags));
}

ALWAYS_INLINE error udp_connect(uv_udp_t& handle, const sockaddr* addr) noexcept {
    return status_to_error(::uv_udp_connect(&handle, addr));
}

ALWAYS_INLINE error udp_recv_start(uv_udp_t& handle,
                                   uv_alloc_cb alloc_cb,
                                   uv_udp_recv_cb recv_cb) noexcept {
    assert(alloc_cb != nullptr && recv_cb != nullptr &&
           "uv::udp_recv_start requires non-null callbacks");
    return status_to_error(::uv_udp_recv_start(&handle, alloc_cb, recv_cb));
}

ALWAYS_INLINE void udp_recv_stop(uv_udp_t& handle) noexcept {
    int rc = ::uv_udp_recv_stop(&handle);
    assert(rc == 0 && "uv::udp_recv_stop failed");
}

ALWAYS_INLINE error udp_send(uv_udp_send_t& req,
                             uv_udp_t& handle,
                             std::span<const uv_buf_t> bufs,
                             const sockaddr* addr,
                             uv_udp_send_cb cb) noexcept {
    assert(!bufs.empty() && "uv::udp_send requires a non-empty buffer span");
    return status_to_error(
        ::uv_udp_send(&req, &handle, bufs.data(), static_cast<unsigned>(bufs.size()), addr, cb));
}

ALWAYS_INLINE result<std::size_t> udp_try_send(uv_udp_t& handle,
                                               std::span<const uv_buf_t> bufs,
                                               const sockaddr* addr) noexcept {
    assert(!bufs.empty() && "uv::udp_try_send requires a non-empty buffer span");
    int rc = ::uv_udp_try_send(&handle, bufs.data(), static_cast<unsigned>(bufs.size()), addr);
    if(rc < 0) {
        return std::unexpected(error(rc));
    }
    return static_cast<std::size_t>(rc);
}

ALWAYS_INLINE error udp_getsockname(const uv_udp_t& handle, sockaddr& name, int& namelen) noexcept {
    return status_to_error(::uv_udp_getsockname(&handle, &name, &namelen));
}

ALWAYS_INLINE error udp_getpeername(const uv_udp_t& handle, sockaddr& name, int& namelen) noexcept {
    return status_to_error(::uv_udp_getpeername(&handle, &name, &namelen));
}

ALWAYS_INLINE error udp_set_membership(uv_udp_t& handle,
                                       const char* multicast_addr,
                                       const char* interface_addr,
                                       uv_membership membership) noexcept {
    return status_to_error(
        ::uv_udp_set_membership(&handle, multicast_addr, interface_addr, membership));
}

ALWAYS_INLINE error udp_set_source_membership(uv_udp_t& handle,
                                              const char* multicast_addr,
                                              const char* interface_addr,
                                              const char* source_addr,
                                              uv_membership membership) noexcept {
    return status_to_error(::uv_udp_set_source_membership(&handle,
                                                          multicast_addr,
                                                          interface_addr,
                                                          source_addr,
                                                          membership));
}

ALWAYS_INLINE error udp_set_multicast_loop(uv_udp_t& handle, bool on) noexcept {
    return status_to_error(::uv_udp_set_multicast_loop(&handle, on ? 1 : 0));
}

ALWAYS_INLINE error udp_set_multicast_ttl(uv_udp_t& handle, int ttl) noexcept {
    return status_to_error(::uv_udp_set_multicast_ttl(&handle, ttl));
}

ALWAYS_INLINE error udp_set_multicast_interface(uv_udp_t& handle,
                                                const char* interface_addr) noexcept {
    return status_to_error(::uv_udp_set_multicast_interface(&handle, interface_addr));
}

ALWAYS_INLINE error udp_set_broadcast(uv_udp_t& handle, bool on) noexcept {
    return status_to_error(::uv_udp_set_broadcast(&handle, on ? 1 : 0));
}

ALWAYS_INLINE error udp_set_ttl(uv_udp_t& handle, int ttl) noexcept {
    return status_to_error(::uv_udp_set_ttl(&handle, ttl));
}

ALWAYS_INLINE bool udp_using_recvmmsg(const uv_udp_t& handle) noexcept {
    return ::uv_udp_using_recvmmsg(&handle) != 0;
}

ALWAYS_INLINE std::size_t udp_get_send_queue_size(const uv_udp_t& handle) noexcept {
    return ::uv_udp_get_send_queue_size(&handle);
}

ALWAYS_INLINE std::size_t udp_get_send_queue_count(const uv_udp_t& handle) noexcept {
    return ::uv_udp_get_send_queue_count(&handle);
}

ALWAYS_INLINE error spawn(uv_loop_t& loop,
                          uv_process_t& process,
                          const uv_process_options_t& options) noexcept {
    assert(options.file != nullptr && "uv::spawn requires options.file");
    return status_to_error(::uv_spawn(&loop, &process, &options));
}

ALWAYS_INLINE error process_kill(uv_process_t& process, int signum) noexcept {
    return status_to_error(::uv_process_kill(&process, signum));
}

ALWAYS_INLINE uv_buf_t buf_init(char* base, unsigned int len) noexcept {
    return ::uv_buf_init(base, len);
}

ALWAYS_INLINE error ip4_addr(const char* ip, int port, sockaddr_in& out) noexcept {
    assert(ip != nullptr && "uv::ip4_addr requires non-null ip");
    return status_to_error(::uv_ip4_addr(ip, port, &out));
}

ALWAYS_INLINE error ip6_addr(const char* ip, int port, sockaddr_in6& out) noexcept {
    assert(ip != nullptr && "uv::ip6_addr requires non-null ip");
    return status_to_error(::uv_ip6_addr(ip, port, &out));
}

ALWAYS_INLINE error ip4_name(const sockaddr_in& src, char* dst, std::size_t size) noexcept {
    assert(dst != nullptr && size > 0 && "uv::ip4_name requires non-null destination and size > 0");
    return status_to_error(::uv_ip4_name(&src, dst, size));
}

ALWAYS_INLINE error ip6_name(const sockaddr_in6& src, char* dst, std::size_t size) noexcept {
    assert(dst != nullptr && size > 0 && "uv::ip6_name requires non-null destination and size > 0");
    return status_to_error(::uv_ip6_name(&src, dst, size));
}

ALWAYS_INLINE std::string_view strerror(int code) noexcept {
    const char* msg = ::uv_strerror(code);
    return msg == nullptr ? std::string_view{} : std::string_view(msg);
}

ALWAYS_INLINE void fs_req_cleanup(uv_fs_t& req) noexcept {
    ::uv_fs_req_cleanup(&req);
}

ALWAYS_INLINE error fs_unlink(uv_loop_t& loop,
                              uv_fs_t& req,
                              const char* path,
                              uv_fs_cb cb) noexcept {
    assert(path != nullptr && "uv::fs_unlink requires non-null path");
    return status_to_error(::uv_fs_unlink(&loop, &req, path, cb));
}

ALWAYS_INLINE error
    fs_mkdir(uv_loop_t& loop, uv_fs_t& req, const char* path, int mode, uv_fs_cb cb) noexcept {
    assert(path != nullptr && "uv::fs_mkdir requires non-null path");
    return status_to_error(::uv_fs_mkdir(&loop, &req, path, mode, cb));
}

ALWAYS_INLINE error fs_stat(uv_loop_t& loop, uv_fs_t& req, const char* path, uv_fs_cb cb) noexcept {
    assert(path != nullptr && "uv::fs_stat requires non-null path");
    return status_to_error(::uv_fs_stat(&loop, &req, path, cb));
}

ALWAYS_INLINE error fs_copyfile(uv_loop_t& loop,
                                uv_fs_t& req,
                                const char* path,
                                const char* new_path,
                                int flags,
                                uv_fs_cb cb) noexcept {
    assert(path != nullptr && new_path != nullptr &&
           "uv::fs_copyfile requires non-null source and destination paths");
    return status_to_error(::uv_fs_copyfile(&loop, &req, path, new_path, flags, cb));
}

ALWAYS_INLINE error fs_mkdtemp(uv_loop_t& loop,
                               uv_fs_t& req,
                               const char* tpl,
                               uv_fs_cb cb) noexcept {
    assert(tpl != nullptr && "uv::fs_mkdtemp requires non-null template");
    return status_to_error(::uv_fs_mkdtemp(&loop, &req, tpl, cb));
}

ALWAYS_INLINE error fs_mkstemp(uv_loop_t& loop,
                               uv_fs_t& req,
                               const char* tpl,
                               uv_fs_cb cb) noexcept {
    assert(tpl != nullptr && "uv::fs_mkstemp requires non-null template");
    return status_to_error(::uv_fs_mkstemp(&loop, &req, tpl, cb));
}

ALWAYS_INLINE error fs_rmdir(uv_loop_t& loop,
                             uv_fs_t& req,
                             const char* path,
                             uv_fs_cb cb) noexcept {
    assert(path != nullptr && "uv::fs_rmdir requires non-null path");
    return status_to_error(::uv_fs_rmdir(&loop, &req, path, cb));
}

ALWAYS_INLINE error
    fs_scandir(uv_loop_t& loop, uv_fs_t& req, const char* path, int flags, uv_fs_cb cb) noexcept {
    assert(path != nullptr && "uv::fs_scandir requires non-null path");
    return status_to_error(::uv_fs_scandir(&loop, &req, path, flags, cb));
}

ALWAYS_INLINE error fs_scandir_next(uv_fs_t& req, uv_dirent_t& ent) noexcept {
    return error(::uv_fs_scandir_next(&req, &ent));
}

ALWAYS_INLINE error fs_opendir(uv_loop_t& loop,
                               uv_fs_t& req,
                               const char* path,
                               uv_fs_cb cb) noexcept {
    assert(path != nullptr && "uv::fs_opendir requires non-null path");
    return status_to_error(::uv_fs_opendir(&loop, &req, path, cb));
}

ALWAYS_INLINE error fs_readdir(uv_loop_t& loop, uv_fs_t& req, uv_dir_t& dir, uv_fs_cb cb) noexcept {
    return status_to_error(::uv_fs_readdir(&loop, &req, &dir, cb));
}

ALWAYS_INLINE error fs_closedir(uv_loop_t& loop,
                                uv_fs_t& req,
                                uv_dir_t& dir,
                                uv_fs_cb cb) noexcept {
    return status_to_error(::uv_fs_closedir(&loop, &req, &dir, cb));
}

ALWAYS_INLINE error fs_fstat(uv_loop_t& loop, uv_fs_t& req, uv_file file, uv_fs_cb cb) noexcept {
    return status_to_error(::uv_fs_fstat(&loop, &req, file, cb));
}

ALWAYS_INLINE error fs_lstat(uv_loop_t& loop,
                             uv_fs_t& req,
                             const char* path,
                             uv_fs_cb cb) noexcept {
    assert(path != nullptr && "uv::fs_lstat requires non-null path");
    return status_to_error(::uv_fs_lstat(&loop, &req, path, cb));
}

ALWAYS_INLINE error fs_rename(uv_loop_t& loop,
                              uv_fs_t& req,
                              const char* path,
                              const char* new_path,
                              uv_fs_cb cb) noexcept {
    assert(path != nullptr && new_path != nullptr &&
           "uv::fs_rename requires non-null source and destination paths");
    return status_to_error(::uv_fs_rename(&loop, &req, path, new_path, cb));
}

ALWAYS_INLINE error fs_fsync(uv_loop_t& loop, uv_fs_t& req, uv_file file, uv_fs_cb cb) noexcept {
    return status_to_error(::uv_fs_fsync(&loop, &req, file, cb));
}

ALWAYS_INLINE error fs_fdatasync(uv_loop_t& loop,
                                 uv_fs_t& req,
                                 uv_file file,
                                 uv_fs_cb cb) noexcept {
    return status_to_error(::uv_fs_fdatasync(&loop, &req, file, cb));
}

ALWAYS_INLINE error
    fs_ftruncate(uv_loop_t& loop, uv_fs_t& req, uv_file file, int64_t off, uv_fs_cb cb) noexcept {
    return status_to_error(::uv_fs_ftruncate(&loop, &req, file, off, cb));
}

ALWAYS_INLINE error fs_sendfile(uv_loop_t& loop,
                                uv_fs_t& req,
                                uv_file out_file,
                                uv_file in_file,
                                int64_t in_offset,
                                std::size_t length,
                                uv_fs_cb cb) noexcept {
    return status_to_error(::uv_fs_sendfile(&loop, &req, out_file, in_file, in_offset, length, cb));
}

ALWAYS_INLINE error
    fs_access(uv_loop_t& loop, uv_fs_t& req, const char* path, int mode, uv_fs_cb cb) noexcept {
    assert(path != nullptr && "uv::fs_access requires non-null path");
    return status_to_error(::uv_fs_access(&loop, &req, path, mode, cb));
}

ALWAYS_INLINE error
    fs_chmod(uv_loop_t& loop, uv_fs_t& req, const char* path, int mode, uv_fs_cb cb) noexcept {
    assert(path != nullptr && "uv::fs_chmod requires non-null path");
    return status_to_error(::uv_fs_chmod(&loop, &req, path, mode, cb));
}

ALWAYS_INLINE error fs_utime(uv_loop_t& loop,
                             uv_fs_t& req,
                             const char* path,
                             double atime,
                             double mtime,
                             uv_fs_cb cb) noexcept {
    assert(path != nullptr && "uv::fs_utime requires non-null path");
    return status_to_error(::uv_fs_utime(&loop, &req, path, atime, mtime, cb));
}

ALWAYS_INLINE error fs_futime(uv_loop_t& loop,
                              uv_fs_t& req,
                              uv_file file,
                              double atime,
                              double mtime,
                              uv_fs_cb cb) noexcept {
    return status_to_error(::uv_fs_futime(&loop, &req, file, atime, mtime, cb));
}

ALWAYS_INLINE error fs_lutime(uv_loop_t& loop,
                              uv_fs_t& req,
                              const char* path,
                              double atime,
                              double mtime,
                              uv_fs_cb cb) noexcept {
    assert(path != nullptr && "uv::fs_lutime requires non-null path");
    return status_to_error(::uv_fs_lutime(&loop, &req, path, atime, mtime, cb));
}

ALWAYS_INLINE error fs_link(uv_loop_t& loop,
                            uv_fs_t& req,
                            const char* path,
                            const char* new_path,
                            uv_fs_cb cb) noexcept {
    assert(path != nullptr && new_path != nullptr &&
           "uv::fs_link requires non-null source and destination paths");
    return status_to_error(::uv_fs_link(&loop, &req, path, new_path, cb));
}

#undef ALWAYS_INLINE

struct resolved_addr {
    sockaddr_storage storage{};
    int family = AF_UNSPEC;
};

inline result<resolved_addr> resolve_addr(std::string_view host, int port) {
    std::string host_storage(host);
    resolved_addr out{};

    auto& out6 = *reinterpret_cast<sockaddr_in6*>(&out.storage);
    auto err6 = uv::ip6_addr(host_storage.c_str(), port, out6);
    if(!err6) {
        out.family = AF_INET6;
        return out;
    }

    auto& out4 = *reinterpret_cast<sockaddr_in*>(&out.storage);
    auto err4 = uv::ip4_addr(host_storage.c_str(), port, out4);
    if(!err4) {
        out.family = AF_INET;
        return out;
    }

    return std::unexpected(error::invalid_argument);
}

}  // namespace uv

template <typename Derived, typename Handle>
class uv_handle {
protected:
    uv_handle() = default;
    ~uv_handle() = default;

public:
    using pointer = unique_handle<Derived>;

    bool initialized() const noexcept {
        auto* self = static_cast<const Derived*>(this);
        auto* h = reinterpret_cast<const uv_handle_t*>(&self->handle);
        return h->loop != nullptr && h->type != UV_UNKNOWN_HANDLE;
    }

    static pointer make() {
        auto self = pointer(new Derived());
        auto* h = reinterpret_cast<uv_handle_t*>(&self->handle);
        *h = {};
        h->data = self.get();
        return self;
    }

    static void destroy(Derived* self) noexcept {
        if(!self) {
            return;
        }

        if(!self->initialized()) {
            delete self;
            return;
        }

        auto& h = *reinterpret_cast<uv_handle_t*>(&self->handle);
        h.data = self;
        if(uv::is_closing(h)) {
            return;
        }

        uv::close(h, [](uv_handle_t* handle) {
            auto* self = static_cast<Derived*>(handle->data);
            delete self;
        });
    }
};

}  // namespace eventide
