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
  - `task_group` — spawn a dynamic fan-out of tasks that start immediately, then join.
- Cooperative cancellation model:
  - `cancellation_token` / `cancellation_source` for thread-safe external triggering.
  - `with_token(task, tokens...)` races a task against one or more tokens.
  - `co_await cancel()` explicitly transitions a task to cancelled.
  - `.catch_cancel()` converts cancellation into an explicit `outcome` channel.
  - `.or_fail()` short-circuits error propagation without resuming at the await site.
- Single-threaded libuv-backed `event_loop`; `run(tasks...)` helper; thread-safe `relay` for hopping onto a loop from another thread.
- Network and IPC I/O:
  - stream base abstraction
  - pipes, TCP sockets, TCP acceptors, console / TTY streams
  - UDP sockets with multicast and per-packet send/recv
- Child process API (`process::spawn`) with stdio piping, async wait/kill, and resource-usage reporting.
- Async filesystem API covering the full libuv fs surface (stat / mkdir / scandir / chmod / link / rename / sendfile / utime / mkstemp / …).
- Libuv watcher wrappers: timer, idle, prepare, check, signal, plus a `sleep` helper.

> **Filesystem-change notifications** are no longer provided. The former `fs_event`
> watcher has been removed — the underlying platform backends proved hard to use and
> unreliable across OSes. For change detection, build a periodic poll on top of the
> existing primitives: a `kota::timer` firing at your desired interval combined with
> `fs::stat` (comparing mtime / size) gives predictable, portable "did this path
> change?" semantics without the platform-specific pitfalls.

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
- Attributes for the codec layer (`spec.h`, `attrs.h`, `annotation.h`):
  - field values: `rename`, `alias`, `description`, `skip`, `flatten`, `defaulted`, `skip_if` (a built-in `skip_when` condition or a predicate type), and `idx`, which is metadata only: it reaches `field_info`, but no backend lays fields out by it
  - struct/variant values: `rename_all`, `deny_unknown_fields`, and variant tagging (`tagged` / `tag` / `content` / `tag_names`) in external, internal or adjacent form
  - behaviors: `as<Target>` (travel as another type), `with<Adapter>` (a per-field representation), `enum_string<Policy>`, `skip_if<Pred>`; a predicate taking only the value applies when encoding, one taking `(value, is_serialize)` decides both ways
  - declared with `KOTATSU_ANNOTATE(...)` on a field or `KOTATSU_ANNOTATION(name, ...)` for a reusable tag (both in `kota/codec/macro.h`), or with `annotate<Tag>::type<T>` / `annotation<T, Attrs...>` (which wrap, inherit or inherit-and-use T as its kind requires); the strings live in one constexpr spec per annotation, so they never enter mangled names
- Type representations (`repr.h`): specializing `repr<T>` or `repr<T, Format>` says what a type travels as, declaratively (`to` / `from`) or imperatively (`serialize` / `deserialize` over the visitor); a repr declaring `meta::dynamic` covers shapes known only at run time (not on FlatBuffers). Reprs chain, and a format-scoped one wins for its backend.
- Compile-time schema (`schema.h`, `type_info.h`): `virtual_schema<T, Config>` lists a struct's slots (`field_slot<RawType, BehaviorAttrs>`) and `field_info` table with flattening and skipping resolved up front; `type_info_of<T, Config>()` gives runtime descriptors (struct, enum, tuple, variant, array, map, optional) that follow reprs and attributes (`resolved_repr_t`) exactly as the codec does.

### `codec` (`include/kota/codec/*`)

- One visitor protocol shared by every backend (`codec/visit/`): `encode_value` / `decode_value` dispatch on a backend override (`serialize_visit` / `deserialize_visit`, specialized per visitor, as ipc does for its protocol types), then annotations and `meta::repr`, then `meta::type_kind`. A backend is a visitor; text backends read data-driven (by key, with speculative `try_read` for untagged variants), binary backends positionally.
- Attributes and config apply the same on every backend: renames and aliases, skipping, flattening, defaults and required fields, `as` / `with` / `enum_string`, tagged variants (a tag wins over the variant type's own repr) and untagged ones picked by probing the input. `default_config<UserConfig>` supplies `enum_repr`, `nan_repr`, `deny_unknown_fields`, `detailed_error`, and takes `field_rename` / `enum_rename` policies; `human_readable = false` turns tagging off on a text backend (a binary backend cannot turn it on).
- Errors: every encode and decode entry point returns `std::expected<…, rich_error>` (a FlatBuffers view that fails verification is an invalid view instead); a `rich_error` carries the message, the path from the root to the failing value (`a.b[3]`), and a source location where the backend knows one.
- Backends:
  - JSON (`codec/json/`): simdjson-based `to_string` / `from_string`, `prettify`, `RawValue` for pass-through JSON, and JSON Schema generation (`schema<T>()`) that agrees with what the encoder writes.
  - TOML (`codec/toml/`): toml++-based `to_toml` / `from_toml` and `to_string` / `from_string`; values that are not tables are boxed under a root key.
  - dyn (`codec/dyn/`): `dyn::Value`, an ordered DOM (null, bool, signed and unsigned integers, double, string, `Array`, `Object`) with `Cursor` navigation; `to_dyn` / `from_dyn`. JSON, TOML and bincode read and write it as a document of their own (bincode behind a kind byte); JSON Schema generation and the LSP model use it.
  - Bincode (`codec/bincode/`): compact little-endian `to_bytes` / `from_bytes`, not self-describing: decode with the type and config that encoded.
  - FlatBuffers (`codec/fbs/`): `to_bytes` / `from_bytes` with the layout computed from the type (no `.fbs` files), a decoder that verifies every access, and zero-copy views (`table_view`, `array_view`, `map_view`, `variant_view`, `tuple_view`) over a buffer verified once.
  - debug (`codec/debug/`): Rust-Debug-style `to_string` for logs and test output; encode-only.

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

- C++ protocol model generated from the pinned LSP 3.18 meta-model by `scripts/lsp/codegen.ts`: aggregates with inherited properties inlined, same-shaped variant alternatives told apart by their string literal members, and `LSPAny` as `codec::dyn::Value`. Regenerate with `pixi run lsp-codegen`; CI checks that the committed header is current.
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
- `ZEST_SUITE` / `ZEST_CASE` / `ZEST_CASE_GROUP` registration with compile-time case attributes (skip / focus / serial) and `setup` / `teardown` hooks.
- One check macro per failure mode — `EXPECT(expr)`, `ASSERT(expr)`, coroutine-aware `CO_ASSERT(expr)` and compile-time `STATIC_EXPECT(expr)` — that splits a top-level comparison and shows both operands when it fails: `EXPECT(parse(text) == expected)`. Comparisons go through `meta::eq` / `lt` / …, so reflectable types compare and print without `operator==`; `ASSERT(result)` on a `std::expected` shows its error.
- The predicates `contains`, `starts_with`, `ends_with` and `type_eq` report their inputs (`EXPECT(!contains(log, "error"))`), and `ZEST_CONTEXT("…", args…)` adds a line to every check failing inside its scope; `EXPECT_THROWS` / `EXPECT_NOTHROWS` check exceptions.
- Default CLI runner: filter by `suite[.test]` with wildcards (`--test-filter=…`) and `--verbose`. Tests run on a pool of worker processes (`--jobs=N`), so a crash or a hang past `--timeout` fails that test alone and a fresh worker takes over; `--no-isolation` runs everything in-process for debuggers.
- Failure reporting uses `std::source_location` to point at the failing expression.

### `support` (`include/kota/support/*`)

- Callable and function-signature traits (`function_traits.h`): `is_function_pointer_v`, `is_functor_v`, and `function_traits<Fn>` exposing `return_type`, `args_type`, `args_count`, plus `member_traits<M C::*>`.
- Containers:
  - `cow_string` — copy-on-write string that can borrow or own its storage
  - `small_vector<T, N>` / `hybrid_vector<T>` — SBO vectors with per-element-size tuned size types
  - `small_string<N>` — SBO string, shares layout with `small_vector<char>`
- Compile-time string utilities: `string_ref`.
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
  <module>/    # Tests per module, mirroring include/kota/<module>/:
               #   unit/ in memory, system/ touching the OS, harness/ helpers
  fixtures/    # Types shared by several modules' tests
  snapshots/   # Snapshot files, one directory per suite
  integration/ # Process-level IPC / LSP integration tests

examples/
  async_basics/    # Introductory async runtime walkthroughs
  build_system/    # Dependency-graph build-system demo
  dump_dot/        # DOT-graph dumping example
  ipc/             # IPC stdio, scripted, and multi-process examples

scripts/
  lsp/             # LSP meta-model tooling (TypeScript, run by Node)
    metamodel.ts   # Pinned meta-model: types, download, sha256-checked cache
    codegen.ts     # Meta-model -> C++ protocol header generator
```
