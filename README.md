# kotatsu

![C++ Standard](https://img.shields.io/badge/C++-23-blue.svg)
[![GitHub license](https://img.shields.io/github/license/clice-io/kotatsu)](https://github.com/clice-io/kotatsu/blob/main/LICENSE)
[![Actions status](https://github.com/clice-io/kotatsu/actions/workflows/main.yml/badge.svg)](https://github.com/clice-io/kotatsu/actions/workflows/main.yml)

kotatsu is a C++23 async runtime with reflection-driven serialization, JSON-RPC and LSP, and cross-platform file system and process APIs. The runtime runs coroutine tasks with structured cancellation over sockets, pipes, child processes and timers. Around it are compile-time reflection and the codecs built on it (JSON, TOML, bincode, FlatBuffers), an HTTP client, a command-line parser and a test framework.

## Modules

| Module                     | What it is                                                                                                    |
| -------------------------- | ------------------------------------------------------------------------------------------------------------- |
| [async](docs/async.md)     | Coroutine tasks and cancellation, an event loop, sockets, pipes, child processes, timers                      |
| [codec](docs/codec.md)     | Serialization driven by reflection: JSON, TOML, bincode, FlatBuffers and zero-copy blobs                      |
| [deco](docs/deco.md)       | Command-line options declared as structs, on an option parser modelled on LLVM's                              |
| [fs](docs/fs.md)           | Paths, files and directories on UTF-8 strings, synchronous                                                    |
| [http](docs/http.md)       | An HTTP client on libcurl and the async runtime                                                               |
| [ipc](docs/ipc.md)         | JSON-RPC peers over any transport, and the generated LSP 3.18 model                                           |
| [meta](docs/meta.md)       | Compile-time reflection of structs and enums, and the attributes the codecs read                              |
| [support](docs/support.md) | Containers, `outcome`, UTF-8 and UTF-16, and the traits the other modules share                               |
| [sys](docs/sys.md)         | Environment, process spawning and system information, synchronous                                             |
| [zest](docs/zest.md)       | A test framework that runs cases in worker processes, with checks that show both sides of a failed comparison |

Everything lives in the `kotatsu` namespace and under `include/kotatsu/`. The [documentation](docs/README.md) has a page per module, with its targets, an example and platform notes.

## Using kotatsu

kotatsu needs a C++23 compiler; the code that includes its headers is compiled as C++23 too.

### Bazel

Every commit of `main` that passes the Bazel build is published to the [clice Bazel registry](https://bazel.clice.io) as version `0.1.0.<n>`; [metadata.json](https://bazel.clice.io/modules/kotatsu/metadata.json) lists them, newest last. List the registry before the Bazel Central Registry in `.bazelrc`:

```
common --registry=https://bazel.clice.io/
common --registry=https://bcr.bazel.build/
common --cxxopt=-std=c++23 --host_cxxopt=-std=c++23
```

Then add the module to `MODULE.bazel` and depend on its targets (`@kotatsu//:async`, `@kotatsu//:codec_json`, ...) in a `BUILD` file:

```starlark
bazel_dep(name = "kotatsu", version = "0.1.0.<n>")
```

### CMake

Add the repository with `FetchContent` (or `add_subdirectory`), turn on the optional parts, and link the targets, named `kotatsu::<module>`:

```cmake
set(CMAKE_CXX_STANDARD 23)

include(FetchContent)
FetchContent_Declare(kotatsu
    GIT_REPOSITORY https://github.com/clice-io/kotatsu.git
    GIT_TAG <commit>)
set(KOTATSU_CODEC_ENABLE_SIMDJSON ON)
FetchContent_MakeAvailable(kotatsu)

target_link_libraries(app PRIVATE kotatsu::async kotatsu::codec)
```

[Building](docs/building.md) lists the targets and the options a dependent sets, and how to build without exceptions or RTTI.

## Platforms

CI builds and tests every commit on:

| Platform           | Toolchains                                                                                         |
| ------------------ | -------------------------------------------------------------------------------------------------- |
| Linux (x64, ARM64) | GCC 14, Clang 20                                                                                   |
| macOS 15 (ARM64)   | Apple Clang, Clang 22                                                                              |
| Windows (x64)      | MSVC, clang-cl, MinGW GCC 14, and Clang with libc++ ([xclang](https://github.com/clice-io/xclang)) |

CI also builds without exceptions and without RTTI, and runs the tests under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer. The Bazel build is tested with xclang on the same platforms.

## Development

kotatsu's development environment comes from [pixi](https://pixi.sh):

```shell
pixi run build   # configure and build the debug preset
pixi run test    # unit, system and integration tests
pixi run format  # format every source file
```

## License

kotatsu is licensed under the [Apache License 2.0](LICENSE).
