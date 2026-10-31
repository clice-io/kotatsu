# sys

`kotatsu::sys` gives synchronous access to the current process and the
system: environment, program lookup, child processes, user directories,
console output, memory and CPU. It needs no event loop, and a program links
only the parts it calls.

It is not for managing processes on an event loop: `kotatsu::async::process`
builds on `sys::spawn` for that (see [async](async.md)).

## Targets

|            |                                                                                         |
| ---------- | --------------------------------------------------------------------------------------- |
| Bazel      | `@kotatsu//:sys`                                                                        |
| CMake      | `kotatsu::sys`, always built                                                            |
| Headers    | [include/kotatsu/sys/](../include/kotatsu/sys/) (`detail/` is private)                  |
| Depends on | [fs](fs.md), [support](support.md); threads; on Windows advapi32, ntdll, ole32, shell32 |

## Example

```cpp
#include <cstdio>
#include <string>

#include "kotatsu/sys/spawn.h"

namespace sys = kotatsu::sys;

int main() {
    std::string out;
    std::string err;
    auto status = sys::capture(out, err, {
        .program = "sh",
        .args = {"-c", "printf out; printf err >&2; exit 3"},
    });
    if(!status) {
        std::fprintf(stderr, "sh: %s\n", status.error().message().c_str());
        return 1;
    }
    // "exit code 3: out, err"
    std::printf("%s: %s, %s\n", status->to_string().c_str(), out.c_str(), err.c_str());
}
```

Adapted from `tests/sys/system/spawn_tests.cpp`. The example runs `sh`, so it is
POSIX only; on Windows the test runs `cmd /c` instead.

## Overview

Failures are `std::expected<T, std::error_code>` (or a bare `std::error_code`);
text comes out into a caller's buffer (`buffer_ref`), as in [fs](fs.md).

| Header                                        | Contents                                                                           |
| --------------------------------------------- | ---------------------------------------------------------------------------------- |
| [env.h](../include/kotatsu/sys/env.h)         | Environment variables, and PATH-like lists split and joined                        |
| [process.h](../include/kotatsu/sys/process.h) | This process: IDs, executable path, arguments, `find_program`, title, signal names |
| [spawn.h](../include/kotatsu/sys/spawn.h)     | `spawn` and `Child` (wait, kill, process groups), `Stdio`, `ExitStatus`, `capture` |
| [windows.h](../include/kotatsu/sys/windows.h) | Windows command lines, NTSTATUS names, CreateProcess's program search              |
| [dirs.h](../include/kotatsu/sys/dirs.h)       | Home, temporary, cache, config and data directories, `~` expansion                 |
| [console.h](../include/kotatsu/sys/console.h) | `write_console`, `is_console`, `is_pipe`                                           |
| [info.h](../include/kotatsu/sys/info.h)       | Process and system memory, CPU time, `parallelism`, OS, priorities                 |

## Platform notes

- What portable code calls is declared on every host and fails with
  `not_supported` where it cannot work (the process title on Windows, the OOM
  score off Linux). What only Windows has lives in `sys::windows`, or is
  declared on Windows alone (`Child::open`); `windows_*` options are ignored
  elsewhere.
- `spawn` uses `fork` and `execve` on Linux, `posix_spawn` on macOS, and
  `CreateProcessW` on Windows, the child in a job object of its own and given
  only the standard handles unless `windows_inherit_handles`.

## See also

[async](async.md), [fs](fs.md), [support](support.md), [tests/sys](../tests/sys/).
