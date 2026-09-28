---
name: test
description: Run kotatsu test suites. Optional args = suite (ctest | integration | all, default all) and cmake preset for the ctest suites (default `debug`). Runs in a forked context — test output stays out of the main conversation; only a digest returns.
context: fork
---

Run the requested suites (default: all).

- Everything: `pixi run test [preset]` — builds the preset and installs the npm packages, then runs ctest: zest's bootstrap stages, its runner check, then `unit_tests` and `system_tests`, each from the repo root with `--snapshot-dir=tests/snapshots` (both are required for snapshot tests; ctest handles them for you), and beside them the integration tests, one ctest test per module (`ipc_lsp_integration`) that runs the module's `integration/*.test.ts` on node's test runner. `ctest --preset <preset> -L unit` (or `system`, `bootstrap`, `integration`) runs one label.
- Integration tests alone: `pixi run integration-test [preset]` (`ctest -L integration`). ctest passes each driver's path in `KOTA_<DRIVER>` (e.g. `KOTA_LSP_STUB_SERVER`); a test whose driver is unset or missing fails, it is never skipped.
- Coverage (Linux and macOS): `pixi run coverage` builds the `coverage` preset (clang source-based coverage), runs every stage with each process writing its profile into `build/coverage/profiles/`, and reports on `include/kota` and `src` in `build/coverage/report/`: `summary.md` (lines, functions and regions per module), `html/index.html` and `coverage.lcov`. Its arguments go to ctest (`pixi run coverage -L unit`). CI's `linux | clang | coverage` leg puts the table in the job summary and uploads the report as the `coverage-report` artifact. zest runs on async, deco and support, so part of their coverage is the runner's own; code only the compiler evaluates (constexpr) never runs and counts as not covered.

Filtering tests means invoking a binary directly — replicate what ctest does (repo root, snapshot dir) and add the filter:

```bash
./build/<preset>/unit_tests --snapshot-dir=tests/snapshots --test-filter=Suite.Case
./build/<preset>/system_tests --snapshot-dir=tests/snapshots --test-filter='async_io_*'
```

(`Suite.*` and bare positional patterns also work.) Rerun integration tests by running node the way ctest does (`ctest --preset <preset> -N -V -L integration` prints its command and environment) — from the repo root, through pixi (the host node is not the project's), with the driver's path set (`.exe` on Windows) — and add a name pattern:

```bash
KOTA_LSP_STUB_SERVER=build/<preset>/lsp_stub_server pixi run node --import ./tests/check_npm_packages.ts --test --test-timeout=20000 --test-name-pattern='^hover_' tests/ipc/lsp/integration/requests.test.ts
```

Rules:

- This skill runs and reports — nothing else. Never fix code or tests from here, never skip or weaken anything, never pass `--update-snapshots` (snapshot updates are a deliberate act in the main conversation).
- When running a binary directly, build the preset first (the build skill); a stale test binary silently tests old code, and so does a stale driver such as `lsp_stub_server`.

Report back, per suite: pass/fail and counts. For each failure: test name, `file:line`, the assertion message or snapshot-diff excerpt (trimmed), and the exact filter command to reproduce it.
