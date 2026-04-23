#pragma once

#include <chrono>
#include <memory>
#include <utility>

#include "kota/http/detail/request_settings.h"
#include "kota/async/io/loop.h"

namespace kota::http::detail {

struct shared_resources;

}  // namespace kota::http::detail

namespace kota::http {

class bound_client;

class client : public detail::request_settings {
public:
    client();
    ~client();

    client(const client&) = default;
    client& operator=(const client&) = default;

    client(client&&) noexcept;
    client& operator=(client&&) noexcept;

    bound_client on(event_loop& loop = event_loop::current()) & noexcept;
    bound_client on(event_loop& loop = event_loop::current()) && noexcept;

private:
    friend class bound_client;
    std::shared_ptr<detail::shared_resources> shared;
};

}  // namespace kota::http
