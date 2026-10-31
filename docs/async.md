# async

`kotatsu::async` is a coroutine runtime: tasks that end with a value, an error
or a cancellation (`task<T, E, C>`; awaiting one gives its value, or an
`outcome` when it has an error or cancel type), ways to combine and cancel
them, sync primitives, and I/O as tasks: streams, UDP, child processes, file
operations, timers and signals.

The event loop is single-threaded: tasks, and what they wait on, belong to the
thread that runs their loop. Other threads reach it through a `relay`;
blocking work goes to a thread pool through `queue()`. For synchronous
file and process work, use [fs](fs.md) and [sys](sys.md). There is no watcher
for file changes: poll with a `timer` and `kotatsu::fs::status()`, comparing
the `mtime` and `size` it reports.

## Targets

|            |                                                                                                                                    |
| ---------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| Bazel      | `@kotatsu//:async`                                                                                                                 |
| CMake      | `kotatsu::async`, with `KOTATSU_ENABLE_ASYNC` (on by default)                                                                      |
| Headers    | [include/kotatsu/async/](../include/kotatsu/async/); [async.h](../include/kotatsu/async/async.h) includes all but `runtime/walk.h` |
| Depends on | [sys](sys.md), [fs](fs.md), [support](support.md); libuv                                                                           |

## Example

```cpp
#include <chrono>
#include <print>

#include "kotatsu/async/async.h"

namespace async = kotatsu::async;
using namespace std::chrono_literals;

async::task<int> slow_add(int a, int b) {
    co_await async::sleep(10ms);
    co_return a + b;
}

async::task<int> combined() {
    // Both run at once; when_all waits for both.
    auto [x, y] = co_await async::when_all(slow_add(1, 2), slow_add(10, 20));
    co_return x + y;
}

int main() {
    // run() makes a loop, runs the task and gives what it ended with.
    auto [sum] = async::run(combined());
    std::println("{}", *sum);  // 33
}
```

Adapted from `examples/async_basics/async_basics.cpp`.

## Overview

| Header                                                                                                                 | Contents                                                                   |
| ---------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| [runtime/task.h](../include/kotatsu/async/runtime/task.h)                                                              | `task`, `cancel()`, `fail()`, `or_fail()`, `catch_cancel()`, `co_invoke()` |
| [runtime/when.h](../include/kotatsu/async/runtime/when.h)                                                              | `when_all` and `when_any`, over tasks or a range of them                   |
| [runtime/task_group.h](../include/kotatsu/async/runtime/task_group.h)                                                  | `task_group` and `with_task_group()`, for a dynamic set of children        |
| [runtime/cancellation.h](../include/kotatsu/async/runtime/cancellation.h)                                              | `cancellation_source`, `cancellation_token`, `with_token()`                |
| [runtime/sync.h](../include/kotatsu/async/runtime/sync.h)                                                              | `mutex`, `semaphore`, `event`, `condition_variable`, and scoped guards     |
| [runtime/debug.h](../include/kotatsu/async/runtime/debug.h), [runtime/walk.h](../include/kotatsu/async/runtime/walk.h) | `dump_dot()` and `async_visitor`, to inspect the task tree                 |
| [io/loop.h](../include/kotatsu/async/io/loop.h)                                                                        | `event_loop`, `run()`, `relay`, `yield()`                                  |
| [io/request.h](../include/kotatsu/async/io/request.h)                                                                  | `queue()`, to run a function on the thread pool                            |
| [io/watcher.h](../include/kotatsu/async/io/watcher.h)                                                                  | `timer`, `signal`, `idle`, `prepare`, `check`, `sleep()`, `with_timeout()` |
| [io/stream.h](../include/kotatsu/async/io/stream.h), [io/udp.h](../include/kotatsu/async/io/udp.h)                     | `stream` (`pipe`, `tcp`, `console`), `acceptor`, `udp`                     |
| [io/process.h](../include/kotatsu/async/io/process.h)                                                                  | `process`: `spawn()`, `capture()`, `wait()`, `kill()`, `kill_tree()`       |
| [io/fs.h](../include/kotatsu/async/io/fs.h)                                                                            | `kotatsu::async::fs`: file operations as tasks, on the thread pool         |
| [vocab/error.h](../include/kotatsu/async/vocab/error.h)                                                                | `error`, `result<T>`, `cancellation`                                       |

## Platform notes

- On Unix the first event loop ignores SIGPIPE, unless the program set its
  own action: a write to a peer that went away fails with `error::broken_pipe`.
- `pipe` is a named pipe on Windows and a Unix domain socket elsewhere.
- `process::open(pid)`, for a process another one started, is Windows only.
- Without exceptions (`KOTATSU_ENABLE_EXCEPTIONS` off), what would throw
  aborts, such as `when_any` over an empty range.

## See also

[sys](sys.md) (`sys::spawn` starts the children of `process`), [fs](fs.md),
[ipc](ipc.md), [http](http.md), [zest](zest.md) (`zest::LoopFixture` tests
async code), [support](support.md) (`outcome`),
[examples/async_basics](../examples/async_basics/), [tests/async](../tests/async/).
