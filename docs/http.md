# http

`kotatsu::http` is an HTTP client on [libcurl](https://curl.se/libcurl/)
whose requests are awaited in [async](async.md) tasks; cancelling a task
cancels its request. An `http::client` holds settings its requests share, and
`client.on(loop)` makes requests on that event loop;
`co_await request.send()` gives an `outcome<http::response, http::error>`.

It is a client only: there is no HTTP server. For JSON-RPC and LSP over pipes
or sockets, use [ipc](ipc.md).

## Targets

|            |                                                                                                                                       |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| Bazel      | none; HTTP builds with CMake only                                                                                                     |
| CMake      | `kotatsu::http`, with `KOTATSU_ENABLE_HTTP` (off by default), which turns on async                                                    |
| Headers    | [include/kotatsu/http/](../include/kotatsu/http/); [http.h](../include/kotatsu/http/http.h) includes the client, request and response |
| Depends on | [async](async.md); libcurl; [codec](codec.md)'s JSON backend for `request::json` when it is enabled                                   |

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

| Header                                                                         | Contents                                                                                                                                    |
| ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------- |
| [detail/client.h](../include/kotatsu/http/detail/client.h)                     | `client`: settings shared by its requests, and per loop one cookie jar, DNS cache and TLS session cache; `on(loop)`                         |
| [detail/bound_client.h](../include/kotatsu/http/detail/bound_client.h)         | `bound_client`: `get`, `post`, `put`, `patch`, `del`, `head`, `request(method, url)`                                                        |
| [detail/request_settings.h](../include/kotatsu/http/detail/request_settings.h) | What a client sets and a request may override: headers, cookies, user agent, proxy, timeout, redirects, TLS, raw `curl_option`              |
| [detail/request.h](../include/kotatsu/http/detail/request.h)                   | `request`: `query`, `bearer_auth`, `basic_auth`, `body`, `json_text`, `form`, `json(value)`, `send()`                                       |
| [detail/response.h](../include/kotatsu/http/detail/response.h)                 | `response`: status, effective url, headers, body as bytes or text, `header_value`                                                           |
| [detail/common.h](../include/kotatsu/http/detail/common.h)                     | `error` and `error_kind` (`curl`, `invalid_request`, `json_encode`, `aborted`), `method::` names, `proxy`, `redirect_policy`, `tls_options` |

## Platform notes

- libcurl uses Schannel for TLS on Windows, and OpenSSL elsewhere, whose
  development files must be installed.
- On Windows `kotatsu::http` defines `NOMINMAX` and `WIN32_LEAN_AND_MEAN` for
  its dependents too.

## See also

[async](async.md), [codec](codec.md), [ipc](ipc.md), [building](building.md),
[examples/http](../examples/http/), [tests/http](../tests/http/).
