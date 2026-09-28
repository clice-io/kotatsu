---
name: build
description: Build kotatsu. Optional arg = cmake preset (default `debug`; also coverage, relwithdebinfo, asan, ubsan, asan-ubsan, tsan, no-exceptions, no-exceptions-no-rtti). Runs in a forked context — compile output and mechanical fixes stay out of the main conversation; only the outcome returns.
context: fork
---

Build the requested cmake preset (default `debug`, the day-to-day tree).

- `pixi run build [preset]` — configures (idempotent) and builds `build/<preset>/` with the pixi toolchain (reproducible; never use the system compiler). Presets are defined in `CMakePresets.json`; dependency sources are shared across presets via `.cache/cpm`.
- Deleting a stale tree and rebuilding is always safe — `rm -rf build/<preset>` and rerun; presets make configuration reproducible.
- The auxiliary xmake build is CI-only (`xmake.yml`); to reproduce a failure from it locally, run the workflow's inlined `xmake config`/`xmake --all`/`xmake test` commands verbatim.

On failure:

- Mechanical breakage (missing include, renamed symbol, stale call site after an agreed-on change): fix it, rebuild, and list every file you touched in the report.
- Design-level errors (the fix requires a decision): do not guess — report the error with `file:line` and the relevant excerpt.

Report back: preset, success or failure, files changed (if any), and for failures a digested error list — never the raw compiler spew.
