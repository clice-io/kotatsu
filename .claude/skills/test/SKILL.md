---
name: test
description: Run kotatsu test suites. Optional args = suite (ctest | integration | all, default all) and cmake preset for the ctest suites (default `debug`). Runs in a forked context — test output stays out of the main conversation; only a digest returns.
context: fork
---

Run the requested suites (default: all).

- Unit and system tests: `pixi run test [preset]` — builds the preset, then runs ctest: zest's bootstrap stages, its runner check, then `unit_tests` and `system_tests`, each from the repo root with `--snapshot-dir=tests/snapshots` (both are required for snapshot tests; ctest handles them for you). `ctest --preset <preset> -L unit` (or `system`, `bootstrap`) runs one label.
- Integration tests (LSP stub server, pytest): `pixi run integration-test` — drives the `lsp_stub_server` binary from the `debug` tree (override with `LSP_STUB_SERVER`). If the stub server is missing entirely, pytest skips the whole suite — a skipped suite is a failure to report, never a pass.

Filtering tests means invoking a binary directly — replicate what ctest does (repo root, snapshot dir) and add the filter:

```bash
./build/<preset>/unit_tests --snapshot-dir=tests/snapshots --test-filter=Suite.Case
./build/<preset>/system_tests --snapshot-dir=tests/snapshots --test-filter='async_io_*'
```

(`Suite.*` and bare positional patterns also work.) Rerun a single integration test with `pixi run pytest tests/integration/test_foo.py -v -k <name>` — always through pixi; the host `pytest`/deps are not the project's.

Rules:

- This skill runs and reports — nothing else. Never fix code or tests from here, never skip or weaken anything, never pass `--update-snapshots` (snapshot updates are a deliberate act in the main conversation).
- When running a binary directly, build the preset first (the build skill); a stale test binary silently tests old code, and so does a stale `lsp_stub_server`.

Report back, per suite: pass/fail and counts. For each failure: test name, `file:line`, the assertion message or snapshot-diff excerpt (trimmed), and the exact filter command to reproduce it.
