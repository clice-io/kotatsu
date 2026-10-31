# zest

`kotatsu::zest` is the test framework kotatsu's own tests use. Each case
(`ZEST_CASE`) runs on a fresh instance of its suite (`ZEST_SUITE`).
`ZEXPECT` and `ZASSERT` print both operands of a failed comparison, comparing
through [meta](meta.md), so reflectable types need no `operator==`. Cases run
spread over worker processes: a crash, a failed `ZASSERT` or a hang fails that
case alone. It suits projects already on kotatsu, as it brings
[deco](deco.md), [async](async.md) and cpptrace along.

## Targets

|            |                                                                                                                                                                                                          |
| ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Bazel      | `@kotatsu//:zest`; `@kotatsu//:zest_async` for `zest/async.h`                                                                                                                                            |
| CMake      | `kotatsu::zest` (option `KOTATSU_ENABLE_ZEST`, off by default, turned on by `KOTATSU_ENABLE_TEST`); `kotatsu::zest::async` for `zest/async.h`                                                            |
| Headers    | [include/kotatsu/zest/](../include/kotatsu/zest/)                                                                                                                                                        |
| Depends on | [deco](deco.md), [meta](meta.md), [codec](codec.md)'s debug codec, [support](support.md); privately [async](async.md), [fs](fs.md), [sys](sys.md); [cpptrace](https://github.com/jeremy-rifkin/cpptrace) |

## Example

```cpp
#include <expected>
#include <string>

#include "kotatsu/zest/zest.h"

std::expected<int, std::string> parse(std::string text) {
    if(text == "42") {
        return 42;
    }
    return std::unexpected(text);
}

ZEST_SUITE(parse_tests) {
ZEST_CASE(reads_the_answer) {
    auto ok = parse("42");
    ZASSERT(ok);  // on failure prints the error, and ends this test
    ZEXPECT(*ok == 42);
    ZEXPECT(!parse("x"));
    ZEXPECT(kotatsu::zest::contains(std::string("haystack"), "st"));
}
};
```

Adapted from `tests/zest/unit/check_tests.cpp`; `main` is
`return kotatsu::zest::run_cli(argc, argv);` (`tests/main.cpp`).

## Overview

| Header                                                             | What it holds                                                                                                                              |
| ------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------ |
| [zest.h](../include/kotatsu/zest/zest.h)                           | Everything below but `async.h`, in one include                                                                                             |
| [macro.h](../include/kotatsu/zest/macro.h)                         | The macros alone: `ZEST_SUITE`, `ZEST_SUITE_ATTRS`, `ZEST_CASE`, `ZEST_CASE_GROUP`, `ZEXPECT`, `ZASSERT`, `ZSTATIC_EXPECT`, `ZEST_CONTEXT` |
| [assert/check.h](../include/kotatsu/zest/assert/check.h)           | `Match`, `Context`, and `FatalHook`, the cleanup a failed `ZASSERT` still runs                                                             |
| [assert/predicates.h](../include/kotatsu/zest/assert/predicates.h) | `contains`, `starts_with`, `ends_with`, `type_eq`, `throws`                                                                                |
| [assert/trace.h](../include/kotatsu/zest/assert/trace.h)           | Stack traces: `print_trace`, `trace_exception`                                                                                             |
| [snapshot/snapshot.h](../include/kotatsu/zest/snapshot/snapshot.h) | `snapshot(value[, name])` and `snapshot_glob`, compared with files under `--snapshot-dir`                                                  |
| [runner/registry.h](../include/kotatsu/zest/runner/registry.h)     | `TestAttrs` (`skip`, `focus`, `serial`, `crashes`), `skip()`                                                                               |
| [runner/suite.h](../include/kotatsu/zest/runner/suite.h)           | `TestSuiteDef`, the base `ZEST_SUITE` gives a suite, and `CaseRegistrar`                                                                   |
| [runner/run.h](../include/kotatsu/zest/runner/run.h)               | `run_cli`, `run_tests`, and `Options`, the runner's flags as a deco struct                                                                 |
| [async.h](../include/kotatsu/zest/async.h)                         | `LoopFixture`: an event loop and `run(tasks...)` with a watchdog, for tests of async code                                                  |

Runner flags: `--test-filter=SUITE[.TEST]` (globs, or a positional pattern),
`--jobs`, `--timeout` (seconds), `--no-isolation` (in-process, for debuggers),
`--verbose`, `--list-tests`, `--snapshot-dir` (snapshot checks fail without
it), `--update-snapshots`, `--cleanup-snapshots`.

## Platform notes

- Registration survives `/OPT:REF`, `--gc-sections` and LTO; only clang's
  `-fvirtual-function-elimination` drops cases.
- Workers end with a killed runner on Linux and Windows; on macOS, once their
  stdin closes.
- Without exceptions, `throws`, `trace_exception` and `describe_exception`
  are not declared.

## See also

[deco](deco.md) · [meta](meta.md) · [async](async.md) · `tests/zest/` for more
