#pragma once

#include <string>

#include "kotatsu/http/detail/client.h"
#include "kotatsu/http/detail/request.h"
#include "kotatsu/async/io/loop.h"

namespace kotatsu::http {

/// A client bound to an event loop: a copy of the client's settings, as
/// they were when it was bound, and the loop its requests run on.
///
/// The requests it builds belong to that loop's thread: await their send()
/// only in tasks that run on the loop. Like the loop, a bound_client is
/// used from one thread at a time.
class bound_client {
public:
    bound_client(client owner, async::event_loop& loop) noexcept;

    /// A request with `method` sent as written, except that GET, HEAD and
    /// POST match in any case and are sent in upper case.
    http::request request(std::string method, std::string url) const noexcept;
    http::request get(std::string url) const noexcept;
    http::request post(std::string url) const noexcept;
    http::request put(std::string url) const noexcept;
    http::request patch(std::string url) const noexcept;
    http::request del(std::string url) const noexcept;
    http::request head(std::string url) const noexcept;

    async::event_loop& loop() const noexcept {
        return *dispatch_loop;
    }

private:
    client owner;
    async::event_loop* dispatch_loop;
};

}  // namespace kotatsu::http
