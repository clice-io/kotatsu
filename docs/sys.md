# sys

`kotatsu::sys` gives synchronous access to the current process and the
system: environment, program lookup, child processes, threads, command lines,
user directories, console output, stack traces, memory and CPU. It needs no
event loop, and a program links only the parts it calls.

It is not for managing processes on an event loop: `kotatsu::async::process`
builds on `sys::spawn` for that (see [async](async.md)).

## Targets

|            |                                                                                                         |
| ---------- | ------------------------------------------------------------------------------------------------------- |
| Bazel      | `@kotatsu//:sys`                                                                                        |
| CMake      | `kotatsu::sys`, always built                                                                            |
| Headers    | [include/kotatsu/sys/](../include/kotatsu/sys/) (`detail/` is private)                                  |
| Depends on | [fs](fs.md), [support](support.md); threads; on Linux libdl; on Windows advapi32, ntdll, ole32, shell32 |

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

| Header                                              | Contents                                                                                                                                                     |
| --------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| [env.h](../include/kotatsu/sys/env.h)               | Environment variables, and PATH-like lists split and joined                                                                                                  |
| [process.h](../include/kotatsu/sys/process.h)       | This process: IDs, executable path and base, arguments, `find_program`, title, `exit_with_parent`, `ignore_sigpipe`, signals                                 |
| [spawn.h](../include/kotatsu/sys/spawn.h)           | `spawn` and `Child` (wait, kill, process groups), `Stdio`, `ExitStatus`, `capture`                                                                           |
| [thread.h](../include/kotatsu/sys/thread.h)         | `Thread`, started with the stack size the caller picks                                                                                                       |
| [cmd.h](../include/kotatsu/sys/cmd.h)               | Command lines of Windows or POSIX quoting split and joined, the host's by default, response files expanded                                                   |
| [windows.h](../include/kotatsu/sys/windows.h)       | NTSTATUS names, CreateProcess's program search                                                                                                               |
| [dirs.h](../include/kotatsu/sys/dirs.h)             | Home, temporary, cache, config and data directories, `~` expansion                                                                                           |
| [console.h](../include/kotatsu/sys/console.h)       | `write_console` and `write_console_some`, `set_nonblocking`, `is_console`, `is_pipe`                                                                         |
| [stacktrace.h](../include/kotatsu/sys/stacktrace.h) | `write_stacktrace`: the calling thread's frames, as addresses in modules, from a crash handler                                                               |
| [info.h](../include/kotatsu/sys/info.h)             | Process and system memory, the address space limit, CPU time, `parallelism` and `physical_cores`, OS, user name, local time, priorities and power throttling |

## Platform notes

- What portable code calls is declared on every host and fails with
  `not_supported` where it cannot work (the process title on Windows, the OOM
  score off Linux). Command lines of either quoting are split, joined and
  expanded on every host. What only Windows has lives in `sys::windows`, or
  is declared on Windows alone (`Child::open`); `windows_*` options are
  ignored elsewhere.
- `spawn` uses `fork` and `execve` on Linux, `posix_spawn` on macOS, and
  `CreateProcessW` on Windows, the child in a job object of its own and given
  only the standard handles unless `windows_inherit_handles`.
- `write_stacktrace` is async-signal-safe: it allocates nothing and takes no
  lock of the loader's. On Linux, on x86-64 and arm64 (`not_supported`
  elsewhere), it walks the stack by .eh_frame's CFI with an unwinder of its
  own, through signal frames, finding modules with `_dl_find_object` from
  glibc 2.35 on and in `/proc/self/maps` before; code without CFI ends the
  trace. On macOS it walks frame pointers, which leaves out the instruction
  that faulted. On Windows it uses `RtlVirtualUnwind`, which takes ntdll's
  locks in shared mode as the exception dispatcher does. macOS and Windows
  name modules from tables that dyld's callbacks and the loader's
  notifications fill from static initialization on. Windows' holds the
  first 1024 modules loaded in the process's life and 128 KiB of their
  paths, none taken back as a module is unloaded; a module past them goes
  unnamed. It writes no symbols.
- macOS cannot start a child that ends with its parent
  (`CreationOptions::exit_with_parent`); a child that links kotatsu calls
  `exit_with_parent()` itself instead, which works on every host.

## See also

[async](async.md), [fs](fs.md), [support](support.md), [tests/sys](../tests/sys/).
