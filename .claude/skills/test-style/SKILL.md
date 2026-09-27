---
name: test-style
description: How kotatsu's tests are organized and written — levels (unit, system, integration), the module-first layout, dependency and trust rules, naming, zest checks, determinism. Read BEFORE writing, moving or reviewing any test.
---

# kotatsu Test Style

## Levels

A test's level is decided by what it touches, not by the module it tests.

| Level       | May touch                                                                                              | Lives in                                  |
| ----------- | ------------------------------------------------------------------------------------------------------ | ----------------------------------------- |
| unit        | the process's memory; an event loop, its timers and in-memory transports count as memory               | `tests/<module>/unit/` → `unit_tests`     |
| system      | the operating system: files, processes, sockets, pipes, signals, threads and thread pools, environment | `tests/<module>/system/` → `system_tests` |
| integration | several programs talking over real protocols, black box                                                | `tests/<module>/integration/` → ctest     |

- The moment a test opens a file, spawns a process, binds a socket, installs a signal handler or starts a thread, it is a system test. zest's own I/O does not count: printing, and reading or writing snapshots through its snapshot macros, are fine in a unit test.
- Compile-time facts that must hold are `STATIC_EXPECT` in a unit case.
- Nothing reaches the network beyond loopback. Tests that need the internet are not part of any suite.
- zest's own runner check (`tests/zest/integration/`) is integration-level, but it is the one integration test in C++ and CMake rather than TypeScript: it runs from CMake as a bootstrap stage (see Trust), so it needs nothing beyond the build.

## Layout

Tests are grouped by module, then by level. `tests/<module>/` mirrors `include/kota/<module>/`; the modules are `support`, `meta`, `codec` with one directory per backend (`codec/json`, `codec/toml`, `codec/fbs`, `codec/bincode`, `codec/dyn`, `codec/debug`), `deco`, `async`, `ipc` with `ipc/lsp`, `http` and `zest`. `tests/examples/` holds the tests of `examples/`: unit tests of what an example builds on, and the example programs run end to end.

```
tests/<module>/
  unit/<path mirroring the headers>/...
  system/...                  same shape
  integration/*.test.ts       integration tests, TypeScript; nothing else but drivers/
  integration/drivers/*.cpp   the programs they spawn, one per file
  harness/*.h, harness/*.ts   helpers for this module's tests and the modules above,
                              integration tests' helpers included
  CMakeLists.txt              kota_add_module_tests(LIBS <the module's libraries>)
                              kota_add_integration_tests(LIBS ... PROGRAMS ...) if it
                              has integration/; PROGRAMS are targets built elsewhere
tests/fixtures/               types shared by several modules' tests
tests/snapshots/<suite>/      snapshot files
```

- The tests of header `<module>/a/b.h` are `unit/a/b_tests.cpp`. A large header's tests split by aspect, into `unit/a/b_<aspect>_tests.cpp` or into a directory `unit/a/b/<aspect>_tests.cpp`.
- codec's protocol (`visit/`, `meta::repr`, attrs, config) is tested once, by a kit every backend runs: `codec/harness/visit/kit.h` holds the primitives and `Caps`, and each area (`values.h`, `attrs.h`, `repr.h`, `variants.h`, `probing.h`) registers its cases for a backend. A backend adapts itself in `codec/<backend>/harness/backend.h` and runs each area in `codec/<backend>/unit/visit/<area>_tests.cpp`, one `ZEST_CASE_GROUP` per file. Fixtures only the codec's tests use live in `codec/harness/fixtures/`.
- Build options are checked once, where `tests/CMakeLists.txt` (or the parent module's `CMakeLists.txt`) adds the module's directory. `xmake.lua` mirrors the module list.

## Dependencies

What each module's headers include, and so what its tests may use:

- `support`; `meta` on support; `deco` on meta.
- `codec`: its core (`visit/`, `macro.h`) on meta; the backends `debug`, `dyn`, `toml` and `fbs` on the core; `bincode` and `json` on the core and `dyn`.
- `async` on support; `ipc` on async and codec (`json`, `dyn`, `bincode`); `ipc/lsp` on ipc; `http` on async and codec `json`.
- `zest` on all of the above that it checks, prints, parses or runs with. Every test includes zest; that is the one exception.

Rules:

- A module's tests include only what their module depends on, plus the harnesses of those modules: `#include "async/harness/loop_fixture.h"`, rooted at `tests/`. Shared fixtures in `tests/fixtures/` follow the same rule for the lowest module that uses them. The build does not enforce this; review does.
- Tests use the public API: `include/kota/`, never a header from `src/`, nor anything from `examples/` outside `tests/examples/`.
- Behaviour defined once in the library is tested once. Backends of one protocol share one suite through the module's harness; a backend's own files test only what is specific to that backend.

## Integration tests

- An integration test is a TypeScript file on node's test runner (`node:test`, `node:assert/strict`), run by node directly (type stripping, no build step) and type-checked by `pixi run typecheck`. Its npm packages are the root `package.json`'s devDependencies (`vscode-jsonrpc`, `vscode-languageserver-protocol`, `vscode-uri`, `vscode-languageserver-textdocument`, `fast-check`), installed by `pixi run npm-ci`.
- A driver is a C++ program under test, `integration/drivers/<name>.cpp`, linked against the module's libraries by `kota_add_integration_tests`; an example it runs is named in `PROGRAMS`. ctest passes each one's path in `KOTA_<NAME>`; a test whose driver is unset or missing fails, it never skips.
- All of a module's `integration/*.test.ts` are one ctest test, `<module>_integration` (label `integration`); it needs nothing from zest and runs beside the other stages.
- ipc's harness (`ipc/harness/driver.ts`) spawns a driver and talks to it over its stdio: through a vscode-jsonrpc connection, on the raw wire (`raw.ts`, and `session.ts`, which pairs responses with requests and keeps the strays), or in JSON lines (`jsonl.ts`) for a driver that is no JSON-RPC peer. `Driver.spawn` takes the test's context: a driver the test did not finish with is killed when the test ends, and how it ended printed with its stderr.
- A driver logs through `test::stderr_logger()` (`ipc/harness/stderr_logger.h`), one `[<level>] <message>` line each.
- Every test ends with `expectExit(code)`, which checks the exit code and fails on a sanitizer report in the driver's stderr, and on a warn or error line the case did not declare with `expectLog(pattern)`: a message the driver drops with a warning fails the case that sent it.
- Cases are named like zest cases: `snake_case`, shaped `<subject>_<behaviour>`, `_fails` for an error. A file's comment says what its cases have in common.
- No sleeps: wait for the message or the exit that says it happened. The runner's timeout (`--test-timeout` in `kota_add_integration_tests`) bounds each test, driver included.
- A randomized test runs on fast-check through `fuzz()` (`ipc/harness/fuzz.ts`): a fixed seed, reported with the shrunk counterexample; `KOTA_FUZZ_SEED` and `KOTA_FUZZ_RUNS` override the seed and the 100 runs for long runs by hand. A run that stalls fails through `within()` before the test times out, and a run that fails kills its driver and reports its stderr.
- LSP's types are drawn from the pinned metaModel (`ipc/lsp/harness/protocol_values.ts`), which also says whether protocol.h reads a value. Where kotatsu reads LSP otherwise, `ipc/lsp/harness/known_deviations.ts` has an entry marked "bug" or "design" that says how; the fix of a bug deletes its entry, and the tests then check it.
- Behaviour the library owes but does not have yet is a case written for the correct behaviour, skipped with the reason, in words rather than a plan's numbering (`{ skip: "an error response drops its data" }`); the fix removes the skip.

## Trust

zest decides pass or fail with meta's comparisons, prints operands with the debug codec, matches test filters with support's glob patterns, parses its options with deco and drives its worker processes with kota::async. ctest therefore runs in stages, each needing the one before:

1. `zest_bootstrap_unit`, `zest_bootstrap_system`: the tests of what zest relies on, in this process, without the worker pool. Their filters are in `tests/CMakeLists.txt`.
2. `zest_runner`: the worker pool end to end.
3. `unit_tests`, `system_tests`: everything.

A test in a bootstrap suite must not judge itself with what it tests: meta's comparison tests use unary checks (`EXPECT(eq(a, b))`) or parenthesized plain bools (`EXPECT((a.x == 1))`), never a split comparison. A new suite covering something zest relies on joins a bootstrap filter; a renamed one updates it.

## Naming

- Files: `<stem>[_<aspect>]_tests.cpp`, `.cpp` only.
- Suites: the file's path under `tests/`, without the level directory and the `_tests` suffix, its components joined by `_`, and a word dropped when it repeats the word before it. For example, `tests/codec/toml/unit/toml_variant_tests.cpp` is `codec_toml_variant`, `tests/meta/unit/schema/schema_attrs_tests.cpp` is `meta_schema_attrs`, and `tests/async/unit/runtime/when/cancel_tests.cpp` is `async_runtime_when_cancel`.
- One suite per file. Suite names are unique across both binaries, since the suite name is also its snapshot directory: when one header has both unit and system tests, the system file names its aspect (`io/loop_threads_tests.cpp` beside the unit `io/loop_tests.cpp`).
- Cases: `snake_case`, shaped `<subject>_<behaviour>`. No suite prefix, no numbering. A case about an error ends in `_fails`, a thrown exception included. Spell it `roundtrip`, not `round_trip`.
- Fixture types are PascalCase.

## Namespaces

- A test file puts its fixtures and suite in an anonymous namespace inside the namespace it tests: `namespace kota::ipc { namespace { ... } }`.
- Harness headers use `namespace kota::test`.
- Specializations of library templates (traits, `meta::repr`) are declared in the library's namespace, as C++ requires; the types they name stay in the test's anonymous namespace.
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
- System tests order events the same way wherever they can: wait for the event, a relay or a semaphore that says the other side is ready. A wait is left only to show that something does not happen, with a comment saying so.
- System tests bind port 0 and read the port back. A case that needs a port twice (a second bind to share it, or to find it in use) binds port 0 first and reuses the port it read back.
- System tests create temporary directories under the system temp directory with an owner that removes them on every path, and never use fixed file names. Windows named pipes live outside the file system; they take a random name instead (e.g. the temporary directory's name).
- `serial = true` and `skip = true` carry a one-line comment giving the reason. A case that can only tell at run time that it cannot run (no IPv6 loopback, no pseudo-terminal) calls `zest::skip()` and returns; a comment on the check, or on the helper it asks, gives the reason.
- A randomized test uses a fixed seed and prints it when it fails.

## Running

`pixi run test [preset]` runs every stage. To run one binary by hand, e.g. with a filter, run it from the repo root with the snapshot directory:

```bash
./build/debug/unit_tests --snapshot-dir=tests/snapshots --test-filter='codec_json_*'
```

`pixi run integration-test [preset]` runs only the integration tests. To run some by hand, do what ctest does (`ctest -N -V -L integration` prints its command), from the repo root with the driver's path set (`lsp_stub_server.exe` on Windows), and filter by name:

```bash
KOTA_LSP_STUB_SERVER=build/debug/lsp_stub_server pixi run node --import ./tests/check_npm_packages.ts --test --test-timeout=20000 --test-name-pattern='^hover_' tests/ipc/lsp/integration/requests.test.ts
```
