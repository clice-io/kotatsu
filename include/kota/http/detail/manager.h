#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>

#include "kota/http/detail/common.h"
#include "kota/http/detail/curl.h"
#include "kota/async/io/loop.h"
#include "kota/async/vocab/owned.h"

namespace kota::http::detail {

struct share_key;
struct transfer;

}  // namespace kota::http::detail

namespace kota::http {

/// Drives the requests of one event loop: one curl multi handle, the libuv
/// timer and socket polls curl asks for, and the curl share of each client
/// that sent requests there. The first request sent on a loop makes its
/// manager, and the loop destroys it as it goes; requests still in flight
/// then stay with their tasks, and cancelling those ends them.
///
/// A manager lives on its loop's thread: call its functions there.
class manager {
public:
    manager(const manager&) = delete;
    manager& operator=(const manager&) = delete;

    manager(manager&&) = delete;
    manager& operator=(manager&&) = delete;

    ~manager();

    /// The manager of `loop`, made by the first call. Fails with
    /// error_kind::aborted once the loop is being destroyed, and with a curl
    /// error when curl cannot be set up.
    static std::expected<std::reference_wrapper<manager>, error> try_for_loop(event_loop& loop);

    /// try_for_loop(), which must succeed: a failure aborts the process.
    static manager& for_loop(event_loop& loop);

    /// Destroys the manager of `loop`, if it has one: its requests in flight
    /// end with error_kind::aborted on a later turn of the loop, and the next
    /// request makes a new manager, with new curl shares.
    static void unregister_loop(event_loop& loop);

    /// The requests curl is driving.
    std::size_t pending_requests() const noexcept;

    event_loop& loop() const noexcept {
        return *bound_loop;
    }

    /// The curl multi handle, for the options kotatsu does not set.
    CURLM* native_multi() const noexcept {
        return multi.get();
    }

private:
    friend struct detail::transfer;

    struct timer_watch;
    struct socket_watch;

    manager(event_loop& loop, curl::multi_handle multi) noexcept;

    /// The curl share of the requests of `key` on this loop, made by the
    /// first call.
    std::expected<std::shared_ptr<curl::share_handle>, error>
        share_for(const std::shared_ptr<const detail::share_key>& key);

    /// Hands `job` to curl and tracks it.
    curl::multi_error add(detail::transfer& job) noexcept;

    /// Stops tracking `job`, and takes it from curl if curl still drives it.
    void drop(detail::transfer& job) noexcept;

    /// Lets curl act on `events` of `socket`, or on its timeout for
    /// CURL_SOCKET_TIMEOUT, then queues the completion of every transfer it
    /// has finished.
    void drive(curl_socket_t socket, int events) noexcept;

    /// Polls `socket` for what curl waits on (`what`), or stops for
    /// CURL_POLL_REMOVE; `watch` is the socket's poll, null before the first.
    int watch_socket(curl_socket_t socket, int what, socket_watch* watch) noexcept;

    static int
        on_socket(CURL* easy, curl_socket_t socket, int what, void* self, void* watch) noexcept;

    static int on_timeout(CURLM* multi, long timeout_ms, void* self) noexcept;

    event_loop* bound_loop;
    /// The curl share of each client that sent requests here, found by its
    /// key's control block. Declared before the multi handle, so that the
    /// shares its transfers used outlive it.
    std::map<std::weak_ptr<const detail::share_key>,
             std::shared_ptr<curl::share_handle>,
             std::owner_less<>>
        jars;
    curl::multi_handle multi;
    kota::detail::unique_handle<timer_watch> timer;
    std::unordered_map<curl_socket_t, kota::detail::unique_handle<socket_watch>> sockets;
    /// The transfers it tracks, linked through transfer::prev and next.
    detail::transfer* transfers = nullptr;
};

}  // namespace kota::http
