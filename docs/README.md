# kotatsu documentation

Each page gives a module's purpose, its build targets, a short example and what differs between platforms. The headers' doc comments are the reference for every name; the pages link to them.

## Modules

| Module                | What it is                                                                                                    |
| --------------------- | ------------------------------------------------------------------------------------------------------------- |
| [support](support.md) | Containers, `outcome`, UTF-8 and UTF-16, and the traits the other modules share                               |
| [meta](meta.md)       | Compile-time reflection of structs and enums, and the attributes the codecs read                              |
| [codec](codec.md)     | Serialization driven by reflection: JSON, TOML, bincode, FlatBuffers and zero-copy blobs                      |
| [fs](fs.md)           | Paths, files and directories on UTF-8 strings, synchronous                                                    |
| [sys](sys.md)         | Environment, process spawning and system information, synchronous                                             |
| [cli](cli.md)         | Command lines read into plain structs as a codec backend, on an option parser modelled on LLVM's              |
| [async](async.md)     | Coroutine tasks and cancellation, an event loop, sockets, pipes, child processes, timers                      |
| [ipc](ipc.md)         | JSON-RPC peers over any transport, and the generated LSP 3.18 model                                           |
| [http](http.md)       | An HTTP client on libcurl and the async runtime                                                               |
| [zest](zest.md)       | A test framework that runs cases in worker processes, with checks that show both sides of a failed comparison |

[Building](building.md) covers depending on kotatsu from Bazel and CMake, the targets and options, and the development commands.

## Dependencies between modules

| Module  | Depends on                                             |
| ------- | ------------------------------------------------------ |
| support | the standard library                                   |
| meta    | support                                                |
| codec   | meta, support                                          |
| fs      | support                                                |
| sys     | fs, support                                            |
| cli     | meta, codec, support; privately sys                    |
| async   | fs, sys, support                                       |
| ipc     | codec, support; its peers and the LSP part also async  |
| http    | async; codec's JSON backend when it is enabled         |
| zest    | cli, meta, codec, support; privately async, fs and sys |

Only async and the modules built on it need an event loop; support, meta, codec, fs, sys, cli and ipc's codec target do not, so a program that must stay small, such as a library injected into other processes, can use them alone.

## Conventions

- Names live in the `kotatsu` namespace: support at its root, every other module in its own (`kotatsu::fs`, `kotatsu::async`, ...). Headers are `kotatsu/<module>/<header>.h`.
- What can fail returns its error: `std::expected<T, std::error_code>` in fs and sys, `std::expected<T, rich_error>` in codec, and an `outcome` from async tasks, ipc and http. Every module builds and is tested without exceptions and without RTTI.
- In support, fs and sys, functions that produce text write it into a buffer the caller gives (`buffer_ref`), and lists of strings come in as `string_list_ref`, so that a loop of calls can reuse one buffer.
- Strings are UTF-8; on Windows, paths and names are WTF-8, which keeps unpaired surrogates.
