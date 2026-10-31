# ipc

`kotatsu::ipc` carries JSON-RPC 2.0's messages between processes, with the LSP
base protocol's `Content-Length` framing, over stdio, TCP or any pair of
streams. They are encoded as JSON, which any JSON-RPC peer reads, or as
bincode, a binary encoding only a kotatsu peer reads.
`Peer<Codec>` is one end of a connection on the event loop: it dispatches what
it reads to handlers and sends requests of its own, with cancellation and
timeouts. The `lsp/` headers add the LSP 3.18 model, URIs and positions. It is
not for HTTP ([http](http.md)), nor does it define how values are encoded
([codec](codec.md)).

## Targets

|            |                                                                                                                                                                                                 |
| ---------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Bazel      | `@kotatsu//:ipc_codec` (framing, codecs, `BlockingClient`; no event loop) and `@kotatsu//:ipc_codec_json`; `@kotatsu//:ipc` (transports, peers) and `@kotatsu//:ipc_json`; `@kotatsu//:ipc_lsp` |
| CMake      | `kotatsu::ipc::codec`, always built; `kotatsu::ipc` and `kotatsu::ipc::lsp`, with `KOTATSU_ENABLE_ASYNC`; the JSON parts with `KOTATSU_CODEC_ENABLE_SIMDJSON`                                   |
| Headers    | [include/kotatsu/ipc/](../include/kotatsu/ipc/), the LSP part in [lsp/](../include/kotatsu/ipc/lsp/)                                                                                            |
| Depends on | [codec](codec.md), [support](support.md); the peers and the LSP part also [async](async.md)                                                                                                     |

## Example

```cpp
#include <cstdint>
#include <utility>

#include "kotatsu/ipc/peer/json.h"

namespace async = kotatsu::async;
namespace ipc = kotatsu::ipc;

struct AddParams { std::int64_t a = 0, b = 0; };
struct AddResult { std::int64_t sum = 0; };

int main() {
    async::event_loop loop;
    auto transport = ipc::StreamTransport::open_stdio(loop);
    if(!transport) {
        return 1;
    }
    ipc::JSONPeer peer(loop, std::move(*transport));
    peer.on_request("example/add",
                    [](ipc::JSONPeer::RequestContext&, const AddParams& params)
                        -> ipc::RequestResult<AddParams, AddResult> {
                        co_return AddResult{.sum = params.a + params.b};
                    });
    loop.schedule(peer.run());
    return loop.run();
}
```

Adapted from `examples/ipc/stdio_server.cpp`.

## Overview

| Header                                                                                                                                                    | Contents                                                                                                                                                      |
| --------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [protocol.h](../include/kotatsu/ipc/protocol.h)                                                                                                           | The JSON-RPC model: `RequestTraits`, `NotificationTraits`, `RequestID`, `ErrorCode`, `Error`                                                                  |
| [framing.h](../include/kotatsu/ipc/framing.h)                                                                                                             | The base protocol's framing: `frame_header()`, `FrameParser`, the payload size limit                                                                          |
| [codec.h](../include/kotatsu/ipc/codec.h), [codec/json.h](../include/kotatsu/ipc/codec/json.h), [codec/bincode.h](../include/kotatsu/ipc/codec/bincode.h) | `JSONCodec` (LSP's member names) and `BincodeCodec`, the messages they read (`IncomingMessage`), `Result<T>`                                                  |
| [transport.h](../include/kotatsu/ipc/transport.h), [recording_transport.h](../include/kotatsu/ipc/recording_transport.h)                                  | `Transport`; `StreamTransport` with `open_stdio()` and `connect_tcp()`; `RecordingTransport`, which records what it reads to JSONL for replay                 |
| [peer.h](../include/kotatsu/ipc/peer.h), [peer/json.h](../include/kotatsu/ipc/peer/json.h), [peer/bincode.h](../include/kotatsu/ipc/peer/bincode.h)       | `Peer<Codec>`, `JSONPeer`, `BincodePeer`: handlers, `RequestContext`, `request_options` (cancellation, timeout), `ViolationPolicy`, `ended()`, `wait_ended()` |
| [blocking_client.h](../include/kotatsu/ipc/blocking_client.h)                                                                                             | `BlockingClient<Codec>`: asks a peer and blocks for the answer, without an event loop                                                                         |
| [logger.h](../include/kotatsu/ipc/logger.h)                                                                                                               | `LogLevel` and `LogCallback`, for `Peer::set_logger()`                                                                                                        |
| [lsp/protocol.h](../include/kotatsu/ipc/lsp/protocol.h)                                                                                                   | The LSP 3.18 model, traits for every request and notification; generated by `scripts/lsp/codegen.ts` (`pixi run lsp-codegen`, checked in CI), not edited      |
| [lsp/uri.h](../include/kotatsu/ipc/lsp/uri.h)                                                                                                             | `lsp::URI`: parsing, percent-encoding, `file://` URIs to and from paths                                                                                       |
| [lsp/position.h](../include/kotatsu/ipc/lsp/position.h), [lsp/text.h](../include/kotatsu/ipc/lsp/text.h)                                                  | Byte offsets to and from `Position` and `Range`, in UTF-8, UTF-16 or UTF-32                                                                                   |
| [lsp/progress.h](../include/kotatsu/ipc/lsp/progress.h)                                                                                                   | `ProgressReporter`, for `$/progress` work-done reports                                                                                                        |

## Platform notes

- `BlockingClient` takes a pipe `HANDLE` (no `FILE_FLAG_OVERLAPPED`) on Windows, a connected stream socket (no `O_NONBLOCK`) elsewhere.
- `open_stdio()` needs stdin to be a pipe, a console or a socket; stdout may also be a file or a device, except on Windows.

## See also

[async](async.md), [codec](codec.md), [meta](meta.md), [support](support.md),
[examples/ipc](../examples/ipc/), [tests/ipc](../tests/ipc/).
