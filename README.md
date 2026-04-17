# kotatsu

`kotatsu` is a C++23 toolkit extracted from the `clice` ecosystem.
It started as a coroutine wrapper around [libuv](https://github.com/libuv/libuv), and now also includes compile-time reflection, an attribute-driven codec framework, a typed IPC layer with generated LSP protocol bindings, a lightweight test framework, an LLVM-compatible option parsing library, a declarative CLI layer built on top, and a shared support layer of containers, traits, and string utilities.

All public APIs live under the `kota::` namespace, public headers under `include/kota/`, and CMake/xmake options use the `KOTA_` prefix.

## Feature Coverage

### `async` runtime (`include/kota/async/*`)

- Typed coroutine tasks `task<T, E, C>` with explicit value, error, and cancellation channels, surfaced through `outcome<T, E, C>`.
- Task composition with sibling cancellation:
  - `when_all(...)` — wait for all children; first error cancels the rest.
  - `when_any(...)` — race children; the winner cancels the rest.
  - `async_scope` — spawn a dynamic fan-out of sibling tasks and join them.
- Cooperative cancellation model:
  - `cancellation_token` / `cancellation_source` for thread-safe external triggering.
  - `with_token(task, tokens...)` races a task against one or more tokens.
  - `co_await cancel()` explicitly transitions a task to cancelled.
  - `.catch_cancel()` converts cancellation into an explicit `outcome` channel.
  - `.or_fail()` short-circuits error propagation without resuming at the await site.
- Single-threaded libuv-backed `event_loop`; `run(tasks...)` helper; thread-safe `post()` / `relay` for hopping onto a loop from another thread.
- Network and IPC I/O:
  - stream base abstraction
  - pipes, TCP sockets, TCP acceptors, console / TTY streams
  - UDP sockets with multicast and per-packet send/recv
- Child process API (`process::spawn`) with stdio piping, async wait/kill, and resource-usage reporting.
- Async filesystem API covering the full libuv fs surface (stat / mkdir / scandir / chmod / link / rename / sendfile / utime / mkstemp / …) and filesystem-change notifications via `fs_event`.
- Libuv watcher wrappers: timer, idle, prepare, check, signal, plus a `sleep` helper.
- Blocking-work offload via `queue(fn, loop)` onto the libuv thread pool.
- Coroutine-friendly sync primitives: mutex, semaphore, event (with interrupt), and condition variable.
- Error vocabulary: `error` (libuv status wrapper with named codes), `result<T>`, and the general `outcome<T, E, C>`.

### `meta` (`include/kota/meta/*`)

- Aggregate reflection (`struct.h`): field count, field names, field references, field metadata iteration, field offsets (by index and by member pointer), and a `reflectable_class<T>` concept.
- Enum reflection (`enum.h`): bidirectional `enum_name<E>(value)` / `enum_value<E>(name)` lookups.
- Compile-time identifier extraction (`name.h`) driven by `std::source_location`: `type_name<T>()`, `pointer_name<>()`, `member_name<>()`.
- Type classification (`type_kind.h`): a `type_kind` enum plus companion concepts such as `int_like`, `uint_like`, `str_like`, `bytes_like`, `tuple_like`.
- Runtime type metadata (`type_info.h`): typed descriptors (`struct_type_info`, `enum_type_info`, `tuple_type_info`, `variant_type_info`, `array_type_info`, `map_type_info`, `optional_type_info`) accessible through `type_info_of<T, Config>()`.
- Reflection-powered comparison (`compare.h`): transparent `eq` / `ne` / `lt` / `le` / `gt` / `ge` functors that recursively handle aggregates, variants, optionals, and ranges.
- Attribute markers for the codec layer (`annotation.h`, `attrs.h`):
  - schema markers: `rename`, `alias`, `literal`, `skip`, `flatten`, `default_value`, `rename_all`, `deny_unknown_fields`, `tagged`, `hint`
  - behavior markers: `enum_string`, `skip_if`, `with<Adapter>`, `as<Target>`
  - attach via the `annotation<T, Attrs...>` wrapper (three kinds: wrap / inherit / inherit-use)
  - compile-time validation via `validate_field_schema<T>()`
- Compile-time schema IR (`schema.h`): `virtual_schema<T, Config>` produces `field_slot<RawType, WireType, BehaviorAttrs>` entries that codec backends consume, with flattening and skipping resolved up-front.

### `codec` (`include/kota/codec/*`)

- Attribute-driven schema framework:
  - User types participate by being reflectable aggregates and optionally wearing `meta::annotation<T, Attrs...>` for per-field or struct-level customization.
  - A compile-time field-lookup table and behavior-dispatch layer are built from `meta::virtual_schema<T, Config>`; backends consume field slots rather than re-deriving layout.
  - `codec::serialize(s, v)` / `codec::deserialize(d, v)` dispatch on annotations first, then on `meta::type_kind`, giving a single entry point whose behavior is controlled entirely by types and attributes.
- Generic trait contract: `serialize_traits<S, V>` / `deserialize_traits<D, V>` with `std::expected<…, error>` return. The `serializer_like` / `deserializer_like` concepts spell out the full visitor surface (null, bool, int, uint, float, char, str, bytes, optional, seq, tuple, map, struct, plus external / internal / adjacent variant tagging).
- Structured error model: a generic `serde_error<Kind>` template carrying a lazily allocated detail block (message, navigation path, source location), with per-backend kind enums (`json::error_kind`, `bincode::error_kind`, `toml::error_kind`, …).
- Backends:
  - JSON (`codec/json/`): a high-throughput streaming backend built on simdjson, plus a DOM-based backend built on yyjson for push/pull access, and a portable content-DOM variant.
  - Bincode (`codec/bincode/`): compact length-prefixed binary format, read and write.
  - TOML (`codec/toml/`): `tomlplusplus`-backed, read and write.
  - FlatBuffers (`codec/flatbuffers/`): binary serialization plus compile-time `.fbs` schema emission from annotated structs.

### `ipc` (`include/kota/ipc/*`)

- JSON-RPC 2.0 protocol model with typed request / notification traits, structured errors (`Error { code, message, data }`), and the full set of spec error codes (including LSP-aligned `RequestCancelled`).
- Transport abstraction for framed message IO: `Transport` interface, `StreamTransport` over stdio / TCP / arbitrary fds, and a `RecordingTransport` decorator that captures traffic to JSONL for replay testing.
- Codec-parametric typed peer runtime (`Peer<Codec>`) supporting request dispatch, notifications, and nested RPC; predefined peers for JSON (with LSP camelCase policy) and Bincode codecs.
- Externally-driven execution model: callers own the event loop, schedule the peer's run loop, and drive shutdown explicitly.
- `std::expected`-based result type (`ipc::Result<T>`) with protocol validation aligned with the JSON-RPC spec:
  - malformed payloads map to `ParseError` with null id
  - structurally invalid messages map to `InvalidRequest` with null id
  - parameter decode failures map to `InvalidParams`
- Cancellation integration with the `async` runtime:
  - inbound `$/cancelRequest` cancels the matching in-flight handler and reports `RequestCancelled`
  - outbound requests accept an optional cancellation token and/or timeout; cancelling a still-pending request sends `$/cancelRequest` to the peer
  - `RequestContext` exposes the inbound handler's cancellation token for easy propagation into nested outbound calls
- Optional structured logging hook (log level + callback).

### `ipc/lsp` (`include/kota/ipc/lsp/*`)

- C++ protocol model generated from the LSP TypeScript meta-model by `scripts/lsp_codegen.py` (named string types, enums, structs, and union variants).
- LSP request / notification traits layered on top of `kota::ipc::protocol`.
- `URI` parsing / manipulation with percent-encoding helpers and `from_file_path` factories.
- `PositionMapper` for byte-offset ↔ LSP `{line, character}` conversion across UTF-8 / UTF-16 / UTF-32 position encodings.
- `ProgressReporter` helper for `$/progress` work-done notifications.

### `option` (`include/kota/option/*`)

- LLVM-compatible option parsing model (`OptTable`, `Option`, `ParsedArgument`).
- Supported option kinds: flag, joined (`-O2`), separate (`--output file`), joined-or-separate, joined-and-separate, comma-joined, fixed-arity multi-arg, remaining / trailing argument packs, plus input / group / unknown variants.
- Alias unaliasing (e.g. `-O2` → `-O` + `2`), grouped short options, visibility-based filtering with per-visibility help text.
- Callback-based parse APIs for both per-argument and whole-argv flows.

### `deco` (`include/kota/deco/*`)

- **Dec**larative **o**ption library: describe options as reflected structs rather than imperative tables.
- Compile-time option-table generation driven by `meta` reflection on top of `kota::option`, wired via `DECO_CFG` / `DECO_DECLARE_OPTION_*` macros.
- Field-level attributes: `required`, help text, meta-var, spellings / aliases, category (exclusive / required groups), argument style and arity, `after_parsed` callbacks.
- Sub-command routing, nested config scopes, and built-in usage / help rendering.

### `zest` test framework (`include/kota/zest/*`)

- Minimal unit test framework used throughout this repository.
- `TEST_SUITE` / `TEST_CASE` registration with compile-time case attributes (skip / focus / serial) and `setup` / `teardown` hooks.
- `EXPECT_*` / `ASSERT_*` and coroutine-aware `CO_ASSERT_*` helpers; binary comparisons go through `meta::eq` / `lt` / … so any reflectable type is printable and comparable.
- Exception helpers: `CAUGHT(expr)` and `ZEST_EXPECT_THROWS(expr, Exception)`.
- Default CLI runner: filter by `suite[.test]` with wildcards (`--test-filter=…`), optional `--parallel` / `--parallel-workers=N`, and `--only-failed-output`.
- Failure reporting uses `std::source_location` to point at the failing expression.

### `support` (`include/kota/support/*`)

- Callable and function-signature traits (`function_traits.h`): `is_function_pointer_v`, `is_functor_v`, and `function_traits<Fn>` exposing `return_type`, `args_type`, `args_count`, plus `member_traits<M C::*>`.
- Containers:
  - `cow_string` — copy-on-write string that can borrow or own its storage
  - `small_vector<T, N>` / `hybrid_vector<T>` — SBO vectors with per-element-size tuned size types
  - `small_string<N>` — SBO string, shares layout with `small_vector<char>`
- Compile-time string utilities: `fixed_string<N>` (usable as an NTTP), `string_ref`.
- Naming-convention conversion (`naming.h`): identity, lower-snake, lower-camel, upper-camel, upper-snake.
- Type-level utilities: `type_list<Ts...>`, `tuple_traits`, `type_traits`, `expected_try`, `comptime` helpers, and miscellaneous `memory` / `ranges` / `functional` adapters.

## Repository Layout

```text
include/kota/
  async/       # Coroutine runtime, event loop, I/O, sync primitives, cancellation
  codec/       # Attribute-driven serde framework + JSON / Bincode / TOML / FlatBuffers backends
  deco/        # Declarative CLI layer on top of option + meta
  ipc/         # JSON-RPC peer, transport, codecs
    lsp/       # Generated LSP protocol model + URI / position / progress helpers
  meta/        # Compile-time reflection, attribute markers, schema IR, runtime type info
  option/      # LLVM-compatible option parsing layer
  support/     # Containers, traits, compile-time strings, naming helpers
  zest/        # Unit test framework

src/
  async/       # Async runtime implementations
  codec/       # Codec backend implementations (content / FlatBuffers)
  deco/        # Deco runtime and text rendering
  ipc/         # IPC peer and transport implementations
    lsp/       # URI / position implementations
  option/      # Option parser implementation
  meta/        # Meta target wiring (header-only public APIs)
  zest/        # Test runner implementation

tests/
  unit/        # Unit tests per module
  integration/ # Process-level IPC / LSP integration tests

examples/
  async_basics/    # Introductory async runtime walkthroughs
  build_system/    # Dependency-graph build-system demo
  dump_dot/        # DOT-graph dumping example
  ipc/             # IPC stdio, scripted, and multi-process examples

scripts/
  lsp_codegen.py   # LSP meta-model -> C++ protocol header generator
```
