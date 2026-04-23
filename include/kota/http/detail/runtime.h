#pragma once

#include <memory>

#include "kota/http/detail/curl.h"

namespace kota::http {

class request;

}  // namespace kota::http

namespace kota::http::detail {

class inflight_request;
struct inflight_request_state;
using inflight_request_ref = std::shared_ptr<inflight_request_state>;

curl::easy_error ensure_curl_runtime() noexcept;

inflight_request_ref make_inflight_request_state(http::request request) noexcept;

void* inflight_request_opaque(const inflight_request_ref& request) noexcept;

inflight_request_ref retain_inflight_request(void* opaque) noexcept;

void mark_inflight_request_removed(const inflight_request_ref& request) noexcept;

void complete_inflight_request(const inflight_request_ref& request,
                               curl::easy_error result,
                               bool resume_inline) noexcept;

}  // namespace kota::http::detail
