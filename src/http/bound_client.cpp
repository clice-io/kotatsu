#include "kotatsu/http/detail/bound_client.h"

#include <string>
#include <utility>

namespace kotatsu::http {

bound_client::bound_client(client owner, async::event_loop& loop) noexcept :
    owner(std::move(owner)), dispatch_loop(&loop) {}

http::request bound_client::request(std::string method, std::string url) const noexcept {
    return http::request(owner, owner.key, *dispatch_loop, std::move(method), std::move(url));
}

http::request bound_client::get(std::string url) const noexcept {
    return request(std::string(http::method::get), std::move(url));
}

http::request bound_client::post(std::string url) const noexcept {
    return request(std::string(http::method::post), std::move(url));
}

http::request bound_client::put(std::string url) const noexcept {
    return request(std::string(http::method::put), std::move(url));
}

http::request bound_client::patch(std::string url) const noexcept {
    return request(std::string(http::method::patch), std::move(url));
}

http::request bound_client::del(std::string url) const noexcept {
    return request(std::string(http::method::del), std::move(url));
}

http::request bound_client::head(std::string url) const noexcept {
    return request(std::string(http::method::head), std::move(url));
}

}  // namespace kotatsu::http
