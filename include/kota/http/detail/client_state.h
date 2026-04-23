#pragma once

#include <functional>
#include <mutex>
#include <optional>

#include "kota/http/detail/curl.h"
#include "kota/http/detail/options.h"
#include "kota/async/io/loop.h"

namespace kota::http {

struct client_state {
    explicit client_state(client_options opts);
    ~client_state() noexcept;

    void bind(event_loop& loop) noexcept;

    bool is_bound() const noexcept;

    std::optional<std::reference_wrapper<event_loop>> loop() const noexcept;

    const client_options& options() const noexcept;

    void record_cookie(bool enabled) noexcept;

    bool bind_easy(CURL* easy, bool enable_record_cookie) const noexcept;

    event_loop* bound_loop = nullptr;
    client_options defaults{};
    curl::share_handle share{};

private:
    static void on_share_lock(CURL* handle,
                              curl_lock_data data,
                              curl_lock_access access,
                              void* userptr) noexcept;

    static void on_share_unlock(CURL* handle, curl_lock_data data, void* userptr) noexcept;

    std::mutex& mutex_for(curl_lock_data data) noexcept;

    mutable std::mutex cookie_mu{};
    mutable std::mutex dns_mu{};
    mutable std::mutex ssl_session_mu{};
    mutable std::mutex admin_mu{};
};

}  // namespace kota::http
