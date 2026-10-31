# http

`kotatsu::http` is an HTTP client on [libcurl](https://curl.se/libcurl/) and
the [async](async.md) runtime. An `http::client` holds the settings every
request copies; `client.on(loop)` binds a copy to an event loop as a
`bound_client`, whose `get`, `post`, `put`, `patch`, `del`, `head` and
`request` build an `http::request`; `co_await request.send()` gives an
`outcome<http::response, http::error>`. A `bound_client` belongs to its loop:
await its requests in tasks on that loop, where one `http::manager` drives
them all. Cancelling a task cancels its request.

It is a client only: there is no HTTP server. For JSON-RPC and LSP over pipes
or sockets, use [ipc](ipc.md).

## Targets

|            |                                                                                                                                                                                            |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Bazel      | none: HTTP builds with CMake only                                                                                                                                                          |
| CMake      | `kotatsu::http`, with `KOTATSU_ENABLE_HTTP=ON` (default `OFF`), which turns on `KOTATSU_ENABLE_ASYNC`                                                                                      |
| Headers    | [include/kotatsu/http/](../include/kotatsu/http/); [http.h](../include/kotatsu/http/http.h) includes the client, request and response                                                      |
| Depends on | [async](async.md); libcurl, which CMake fetches and builds as a static, HTTP-only library; [codec](codec.md)'s JSON backend for `request::json` when `KOTATSU_CODEC_ENABLE_SIMDJSON` is on |

## Example

```cpp
#include <print>

#include "kotatsu/async/async.h"
#include "kotatsu/http/http.h"

namespace async = kotatsu::async;
namespace http = kotatsu::http;

async::task<void, http::error> fetch(async::event_loop& loop) {
    http::client client;
    auto reply = co_await client.on(loop).get("https://example.com").send().or_fail();
    std::println("{} {}: {} bytes", reply.status, reply.url, reply.bytes().size());
}

int main() {
    async::event_loop loop;
    auto root = fetch(loop);
    loop.schedule(root);
    loop.run();
    if(auto result = root.result(); result.has_error()) {
        std::println("request failed: {}", result.error().message());
        return 1;
    }
}
```

Adapted from `examples/http/github_avatar_download.cpp`.

## Overview

| Header                                                                  | Contents                                                                                                                                    |
| ----------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| [client.h](../include/kotatsu/http/detail/client.h)                     | `client`: settings shared by its requests, and per loop one cookie jar, DNS cache and TLS session cache; `on(loop)`                         |
| [bound_client.h](../include/kotatsu/http/detail/bound_client.h)         | `bound_client`: `get`, `post`, `put`, `patch`, `del`, `head`, `request(method, url)`                                                        |
| [request_settings.h](../include/kotatsu/http/detail/request_settings.h) | What a client sets and a request may override: headers, cookies, user agent, proxy, timeout, redirects, TLS, raw `curl_option`              |
| [request.h](../include/kotatsu/http/detail/request.h)                   | `request`: `query`, `bearer_auth`, `basic_auth`, `body`, `json_text`, `form`, `json(value)`, `send()`                                       |
| [response.h](../include/kotatsu/http/detail/response.h)                 | `response`: status, effective url, headers, body as bytes or text, `header_value`                                                           |
| [common.h](../include/kotatsu/http/detail/common.h)                     | `error` and `error_kind` (`curl`, `invalid_request`, `json_encode`, `aborted`), `method::` names, `proxy`, `redirect_policy`, `tls_options` |
| [manager.h](../include/kotatsu/http/detail/manager.h)                   | `manager`: one per event loop, made by its first request, gone with the loop; `unregister_loop` (not included by `http.h`)                  |
| [curl.h](../include/kotatsu/http/detail/curl.h)                         | `kotatsu::curl`: thin wrappers over libcurl's easy, multi and share handles                                                                 |

## Platform notes

- libcurl uses Schannel for TLS on Windows and OpenSSL elsewhere, which CMake
  must find.
- On Windows `kotatsu::http` defines `NOMINMAX` and `WIN32_LEAN_AND_MEAN` for
  its dependents too.

## See also

[async](async.md), [codec](codec.md), [ipc](ipc.md), [building](building.md), `examples/http/`, `tests/http/system/detail/`.
