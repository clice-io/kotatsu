#include "kotatsu/http/detail/client.h"

#include <memory>
#include <utility>

#include "kotatsu/http/detail/bound_client.h"

namespace kotatsu::http {

client::client() : key(std::make_shared<const detail::share_key>()) {}

client::~client() = default;

client::client(client&&) noexcept = default;

client& client::operator=(client&&) noexcept = default;

bound_client client::on(event_loop& loop) & noexcept {
    return bound_client(*this, loop);
}

bound_client client::on(event_loop& loop) && noexcept {
    return bound_client(std::move(*this), loop);
}

}  // namespace kotatsu::http
