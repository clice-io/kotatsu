#pragma once

#include <memory>

#include "kota/http/detail/request_settings.h"
#include "kota/async/io/loop.h"

namespace kota::http::detail {

/// What a client and its copies have in common. Each event loop keeps one
/// curl share (cookie jar, DNS cache, TLS sessions) for the requests of
/// each key, for as long as the key lives: a client, its copies and the
/// requests made from them hold it.
struct share_key {};

}  // namespace kota::http::detail

namespace kota::http {

class bound_client;

/// Settings for requests, and the state they share. The settings are
/// copied into each request, which may override them.
///
/// A client and its copies are one: on each event loop, the requests they
/// send share one cookie jar, DNS cache and TLS session cache, which the
/// loop keeps while any of them lives. A client used on two loops keeps two
/// of each, as curl cannot share cookies between threads, and a loop runs
/// on one thread at a time. A client, like its copies, may be used from any
/// thread; see bound_client for where requests run.
class client : public detail::request_settings {
public:
    client();
    ~client();

    client(const client&) = default;
    client& operator=(const client&) = default;

    client(client&&) noexcept;
    client& operator=(client&&) noexcept;

    /// A copy of this client bound to `loop`, whose requests run there.
    bound_client on(event_loop& loop = event_loop::current()) & noexcept;
    bound_client on(event_loop& loop = event_loop::current()) && noexcept;

private:
    friend class bound_client;
    std::shared_ptr<const detail::share_key> key;
};

}  // namespace kota::http
