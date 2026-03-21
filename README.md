# eventide

`eventide` is a C++23 toolkit extracted from the `clice` ecosystem.
It started as a coroutine wrapper around [libuv](https://github.com/libuv/libuv), and now also includes compile-time reflection, serde utilities, a typed IPC layer, generated LSP protocol bindings, a lightweight test framework, an LLVM-compatible option parsing library, and a declarative option library built on it.

## Feature Coverage

### `eventide` async runtime (`include/eventide/*`)

- Coroutine task system (`task`, shared task/future utilities, cancellation).
- Event loop scheduling (`event_loop`, `run(...)` helper).
- Task composition (`when_all`, `when_any`).
- Network / IPC wrappers:
  - `stream` base API
  - `pipe`, `tcp`, `console`
  - `udp`
- Process API (`process::spawn`, stdio pipe wiring, async wait/kill).
- Async filesystem API (`fs::*`) and file watch API (`fs_event`).
- Libuv watcher wrappers (`timer`, `idle`, `prepare`, `check`, `signal`).
- Coroutine-friendly sync primitives (`mutex`, `semaphore`, `event`, `condition_variable`).

### `reflection` (`include/eventide/reflection/*`)

- Aggregate reflection:
  - field count
  - field names
  - field references and field metadata iteration
  - field offsets (by index and by member pointer)
- Enum reflection and enum name/value mapping.
- Compile-time type/pointer/member name extraction.
- Callable/function traits (`callable_args_t`, `callable_return_t`, etc.).

### `serde` (`include/eventide/serde/*`)

- Generic serialization/deserialization trait layer.
- Field annotation system:
  - `rename`, `alias`, `flatten`, `skip`, `skip_if`, `literal`, `enum_string`, etc.
- Backend integrations:
  - `serde::json::simd` (simdjson-based JSON serializer/deserializer)
- FlatBuffers helpers (`flatbuffers/binary/*`) and FlexBuffers helpers (`flatbuffers/flex/*`)

### `ipc` (`include/eventide/ipc/*`)

- JSON-RPC 2.0 protocol types and typed request/notification traits.
- Transport abstraction (`Transport`, `StreamTransport`) for framed message IO.
- Typed peer runtime (`Peer`) for request dispatch, notifications, and nested RPC.
- External event-loop execution model: callers own `event_loop`, schedule `peer.run()`, and drive shutdown explicitly.
- Error model: RPC APIs return `ipc::Result<T>` (`std::expected<T, RPCError>`), where `RPCError` includes `code`, `message`, and optional structured `data`.
- Protocol validation behavior:
  - malformed JSON maps to `ParseError (-32700)` with `id: null`
  - structurally invalid messages map to `InvalidRequest (-32600)` with `id: null`
  - parameter decode failures map to `InvalidParams (-32602)`
- Cancellation behavior:
  - inbound `$/cancelRequest` cancels matching in-flight handlers and returns `RequestCancelled (-32800)` (aligned with LSP `LSPErrorCodes::RequestCancelled`)
  - outbound request cancellation (token or timeout) sends `$/cancelRequest` to the remote peer when the request is still pending
  - timeout overloads report `RequestCancelled (-32800)` with message `"request timed out"`
  - `RequestContext` delegates via `operator->`; use `context->send_request(..., {.token = context.cancellation})` to propagate the inbound handler token to nested outbound requests

### `ipc/lsp` (`include/eventide/ipc/lsp/*`)

- Generated LSP protocol model (`include/eventide/ipc/lsp/protocol.h`).
- LSP URI and position helpers (`URI`, `PositionMapper`).
- LSP request/notification traits layered onto `eventide::ipc::protocol`.
- `ProgressReporter` for `$/progress` work-done notifications.

### `option` (`include/eventide/option/*`)

- LLVM-compatible option parsing model (`OptTable`, `Option`, `ParsedArgument`).
- Supports common option styles:
  - flag (`-v`)
  - joined (`-O2`)
  - separate (`--output file`)
  - comma-joined (`--list=a,b,c`)
  - fixed multi-arg (`--pair left right`)
  - remaining args / trailing packs
- Alias/group matching, visibility/flag filtering, and grouped short options.
- Callback-based parse APIs for one-arg and whole-argv flows.

### `deco` (`include/eventide/deco/*`)

- **Dec**larative **o**ption library
- Declarative CLI option definition macros (`DecoFlag`, `DecoKV`, `DecoInput`, `DecoPack`, etc.).
- Reflection-driven compile-time option table generation on top of `eventide::option`.
- Runtime parser + dispatch layer (`deco::cli::parse`, `Dispatcher`, `SubCommander`).
- Built-in usage/help descriptor rendering and category constraints:
  - required options
  - required/exclusive categories
  - nested config scopes

### `zest` test framework (`include/eventide/zest/*`)

- Minimal unit test framework used by this repository.
- Test suite / test case registration macros.
- Expect/assert helpers with formatted failure output and stack trace support.
- Test filtering via `--test-filter=...`.

## Repository Layout

```text
include/
  eventide/      # Public headers
    async/       # Async runtime APIs
    common/      # Shared utilities
    deco/        # Declarative CLI layer built on option + reflection
    ipc/         # IPC protocol, peer, and transport APIs
    ipc/lsp/     # LSP protocol model and utilities
    option/      # LLVM-compatible option parsing layer
    reflection/  # Compile-time reflection utilities
    serde/       # Generic serde + backend adapters
    zest/        # Internal test framework headers

src/
  async/         # Async runtime implementations
  ipc/           # IPC peer and transport implementations
  option/        # Option parser implementation
  deco/          # Deco target wiring (header-only APIs)
  serde/         # FlatBuffers/FlexBuffers serde implementation
  ipc/lsp/   # URI/position implementations
  reflection/    # Reflection target wiring (header-only public APIs)
  zest/          # Test runner implementation

tests/
  option/        # Option parser behavior tests
  deco/          # Declarative CLI/deco tests
  reflection/    # Reflection behavior tests
  eventide/      # Runtime/event-loop/IO/process/fs/sync tests
  ipc/           # IPC peer and transport tests
  serde/         # JSON/FlatBuffers serde tests
  ipc/lsp/   # LSP utility, progress, and jsonrpc-trait tests

examples/
  ipc/       # IPC stdio, scripted, and multi-process examples

scripts/
  lsp_codegen.py # LSP schema -> C++ protocol header generator
```
