# kotatsu

A C++20/23 coroutine wrapper for libuv. Library modules live under
`include/kota/<module>/` (`async`, `codec`, `deco`, `http`, `ipc`, `meta`,
`support`, `zest`).

## Build system

**CMake is the primary build system.** `xmake.lua` is an auxiliary build
description for downstream xmake users, kept working by the small `xmake.yml`
CI workflow — never use xmake for local development.

- All configuration lives in `CMakePresets.json`; trees land in
  `build/<preset>/`. Build with `pixi run build [preset]` (default `debug`;
  also `asan`, `tsan`, `no-exceptions`, ...) — this uses the pixi toolchain,
  never the system compiler.
- The main CI matrix is `cmake.yml` (all toolchains, sanitizers,
  no-exceptions/no-rtti variants), driving the same presets.
- Bazel (`MODULE.bazel`) is the build clice consumes, kept in step with
  CMake: a target per module in `BUILD.bazel`, the tests in
  `tests/BUILD.bazel`, the third-party libraries and the toolchain (xclang)
  from the clice Bazel registry, bazel.clice.io. `pixi run -e bazel bazel
  test //...` builds and tests it with xclang (`--config=no-exceptions`,
  `--config=no-rtti`, `--config=asan`, ... in `.bazelrc`); `bazel.yml` runs
  it in CI. A source, test or dependency
  added to CMake is added there too.

## Testing

- Tests live in `tests/<module>/`, split by level into `unit_tests`,
  `system_tests` and TypeScript integration tests (the test-style skill).
  `pixi run test [preset]` runs them all through ctest, which handles the
  snapshot dir and the drivers' paths. Running a test binary by hand — e.g. to
  pass `--test-filter` — must be done from the repo root with
  `--snapshot-dir=tests/snapshots`, or snapshot tests fail spuriously.
- Integration tests alone: `pixi run integration-test [preset]`
  (`ctest -L integration`).

## Skills

Read the relevant skill before acting: `cpp-style` before writing C++,
`test-style` before writing or moving tests, `build`/`test` to build and run
suites, `pr` before committing or opening a PR, `format` (`pixi run format`)
before every commit, `codex` before delegating work to the codex CLI
(debugging, test writing, scoped implementation; reviews go to Opus
subagents).
