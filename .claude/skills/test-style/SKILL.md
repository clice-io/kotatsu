---
name: test-style
description: How kotatsu's tests are organized and written — levels (static, unit, system, integration, fuzz), the module-first layout, dependency and trust rules, naming, zest checks, determinism. Read BEFORE writing, moving or reviewing any test.
---

# kotatsu Test Style

## Levels

A test's level is decided by what it touches, not by the module it tests.

| Level       | May touch                                                                                              | Lives in                                  |
| ----------- | ------------------------------------------------------------------------------------------------------ | ----------------------------------------- |
| static      | the compiler only: code that must not compile                                                          | `tests/<module>/static/`                  |
| unit        | the process's memory; an event loop and its timers count as memory                                     | `tests/<module>/unit/` → `unit_tests`     |
| system      | the operating system: files, processes, sockets, pipes, signals, threads and thread pools, environment | `tests/<module>/system/` → `system_tests` |
| fuzz        | untrusted input, coverage-guided                                                                       | `tests/<module>/fuzz/`                    |
| integration | several programs talking over real protocols, black box                                                | `tests/integration/`                      |

- A unit test may run an event loop, timers and in-memory transports; the moment it opens a file, spawns a process, binds a socket or starts a thread, it is a system test.
- Compile-time facts that must hold are `STATIC_EXPECT` in a unit case; code that must be rejected is a static case, matched against our own `static_assert` message.
- Nothing in any level reaches the network beyond loopback. Tests that need the internet, and long fuzz runs, are run by hand and are not part of any suite.

## Layout

Tests are grouped by module, then by level. `tests/<module>/` mirrors `include/kota/<module>/`; the modules are `support`, `meta`, `codec` with one directory per backend (`codec/json`, `codec/toml`, `codec/fbs`, `codec/bincode`, `codec/dyn`, `codec/debug`), `deco`, `async`, `ipc` with `ipc/lsp`, `http` and `zest`.

```
tests/<module>/
  unit/<path mirroring the headers>/<stem>[_<aspect>]_tests.cpp
  system/...                  same shape
  harness/*.h                 helpers for this module's tests and the modules above
  CMakeLists.txt              kota_add_module_tests(LIBS <the module's libraries>)
tests/fixtures/               types shared by several modules' tests
tests/snapshots/<suite>/      snapshot files
tests/integration/            cross-module tests with their own toolchain
```

- Build options are checked once, where `tests/CMakeLists.txt` (or the parent module's `CMakeLists.txt`) adds the module's directory. `xmake.lua` mirrors the module list.
- One file tests one header, or one aspect of a large one (`<stem>_<aspect>_tests.cpp`).

## Dependencies

Modules depend on each other in this order: support → meta → codec → deco; support → async → ipc → ipc/lsp; async and codec/json → http; zest on top of all of them.

- A module's tests include only their own module and the modules below it, including those modules' harnesses: `#include "async/harness/loop_fixture.h"`, rooted at `tests/`.
- Tests use the public API. Never include a header from `src/` or anything from `examples/`.
- Behaviour defined once in the library is tested once. Backends of one protocol share one suite through the module's harness; a backend's own files test only what is specific to that backend.

## Trust

zest decides pass or fail with meta's comparisons, prints operands with the debug codec, parses its options with deco and drives its worker processes with kota::async. ctest therefore runs in stages, each needing the one before:

1. `zest_bootstrap_unit`, `zest_bootstrap_system`: the tests of what zest relies on, in this process, without the worker pool. Their filters are in `tests/CMakeLists.txt`.
2. `zest_runner`: the worker pool end to end (`tests/zest/integration/`).
3. `unit_tests`, `system_tests`: everything.

A test in a bootstrap suite must not judge itself with what it tests: meta's comparison tests use unary checks (`EXPECT(eq(a, b))`) or parenthesized plain bools (`EXPECT((a.x == 1))`), never a split comparison. A new suite covering something zest relies on joins a bootstrap filter.

## Naming

- Files: `<stem>[_<aspect>]_tests.cpp`, `.cpp` only.
- Suites: `<module>_<path>_<stem>[_<aspect>]`, with the module path joined by `_`, the path under the level directory, and the stem without `_tests` or a leading repeat of the module's last part. For example, `tests/codec/toml/unit/toml_variant_tests.cpp` is `codec_toml_variant`, and `tests/async/unit/runtime/when/cancel_tests.cpp` is `async_runtime_when_cancel`. One suite per file, unique per binary; the suite name is also its snapshot directory.
- Cases: `snake_case`, shaped `<subject>_<behaviour>`. No suite prefix, no numbering. A case about an error ends in `_fails`. Write `roundtrip`.
- Fixture types are PascalCase.

## Namespaces

- A test file puts its fixtures and suite in an anonymous namespace inside the namespace it tests: `namespace kota::ipc { namespace { ... } }`.
- Harness headers use `namespace kota::test`.
- Test types never live in a library namespace outside an anonymous one. No `using namespace` at namespace scope except `std::literals`.

## Checks

- One expression per check. `EXPECT(a == b)` reports both operands; `EXPECT(x)` and `EXPECT(!x)` report `x`. Split `a && b` into two checks.
- `ASSERT` before anything that relies on the check: dereferencing an optional, expected or pointer, indexing, `.value()`. In a coroutine, `CO_ASSERT`.
- An expected error is checked for what it is, not only that it happened: `ASSERT(!result); EXPECT(result.error().kind == ...)`.
- Predicates: `contains`, `starts_with`, `ends_with` for text and ranges, `type_eq<A, B>()` for types.
- `ZEST_CONTEXT` in helpers and loops, naming the input a failing check was about.
- Large expected text (pretty output, schemas, diagnostics) is a snapshot. Updating snapshots is a deliberate act.
- `EXPECT((a == b))` compares with the type's own operator; use it only for a type meta cannot compare.
- Every case checks something. Checks inside a callback or coroutine are backed by a check, after the run, that they ran.

## Determinism

- Unit tests never order events by sleeping; they use events, latches or the loop's own ordering. A timer is fine as the subject of a test.
- System tests bind port 0 and read the port back; create temporary directories under the system temp directory with an owner that removes them on every path; never use fixed file names.
- `serial = true` and `skip = true` carry a one-line comment giving the reason.
- A randomized test uses a fixed seed and prints it when it fails.

## Running

`pixi run test [preset]` runs every stage. To run one binary by hand, e.g. with a filter, run it from the repo root with the snapshot directory:

```bash
./build/debug/unit_tests --snapshot-dir=tests/snapshots --test-filter='codec_json_*'
```
