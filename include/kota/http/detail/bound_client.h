#pragma once

#include <string>
#include <utility>

#include "kota/http/detail/client.h"
#include "kota/http/detail/request.h"
#include "kota/http/detail/response.h"
#include "kota/async/io/loop.h"
#include "kota/async/runtime/task.h"

namespace kota::http {

class bound_client {
public:
    bound_client(client owner, event_loop& loop) noexcept;

    http::request request(std::string method, std::string url) const noexcept;
    http::request get(std::string url) const noexcept;
    http::request post(std::string url) const noexcept;
    http::request put(std::string url) const noexcept;
    http::request patch(std::string url) const noexcept;
    http::request del(std::string url) const noexcept;
    http::request head(std::string url) const noexcept;

    event_loop& loop() const noexcept {
        return *dispatch_loop;
    }

private:
    client owner;
    event_loop* dispatch_loop = nullptr;
};

}  // namespace kota::http
