#include "kota/http/detail/client.h"

#include <cstdlib>
#include <memory>
#include <utility>

#include "kota/http/detail/bound_client.h"
#include "kota/http/detail/runtime.h"

namespace kota::http {

namespace {

std::shared_ptr<detail::shared_resources> require_shared_resources() {
    auto resources = detail::make_shared_resources();
    if(!resources) {
        std::abort();
    }
    return resources;
}

}  // namespace

std::shared_ptr<detail::shared_resources> detail::make_shared_resources() {
    if(auto code = detail::ensure_curl_runtime(); !curl::ok(code)) {
        return {};
    }

    auto resources = std::make_shared<detail::shared_resources>();
    resources->share = curl::share_handle::create();
    if(!resources->share) {
        return {};
    }

    if(auto err =
           curl::share_setopt(resources->share.get(), CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
       !curl::ok(err)) {
        return {};
    }

    if(auto err = curl::share_setopt(resources->share.get(), CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
       !curl::ok(err)) {
        return {};
    }

    if(auto err =
           curl::share_setopt(resources->share.get(), CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
       !curl::ok(err)) {
        return {};
    }

    return resources;
}

client::client() : shared(require_shared_resources()) {}

client::~client() = default;

client::client(client&&) noexcept = default;

client& client::operator=(client&&) noexcept = default;

bound_client client::on(event_loop& loop) & noexcept {
    return bound_client(*this, loop);
}

bound_client client::on(event_loop& loop) && noexcept {
    return bound_client(std::move(*this), loop);
}

}  // namespace kota::http
