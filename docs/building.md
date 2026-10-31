# Building

kotatsu builds with Bazel and with CMake. Both have a target per module, with further targets for the codec backends and for parts of deco, ipc and zest. kotatsu needs a C++23 compiler, and the code that includes its headers is compiled as C++23 too; [the README](../README.md#platforms) lists the toolchains CI tests.

## Targets

| Module  | Bazel (`@kotatsu//...`)                                          | CMake                                                               | Third-party                   |
| ------- | ---------------------------------------------------------------- | ------------------------------------------------------------------- | ----------------------------- |
| support | `:support`                                                       | `kotatsu::support`                                                  |                               |
| meta    | `:meta`                                                          | `kotatsu::meta`                                                     |                               |
| fs      | `:fs`                                                            | `kotatsu::fs`                                                       |                               |
| sys     | `:sys`                                                           | `kotatsu::sys`                                                      |                               |
| codec   | `:codec`, `:codec_json`, `:codec_toml`, `:codec_flatbuffers`     | `kotatsu::codec`, `kotatsu::codec::json`, `::toml`, `::flatbuffers` | simdjson, toml++, FlatBuffers |
| deco    | `:option`, `:deco`                                               | `kotatsu::deco::option`, `kotatsu::deco`                            |                               |
| async   | `:async`                                                         | `kotatsu::async`                                                    | libuv                         |
| ipc     | `:ipc_codec`, `:ipc_codec_json`, `:ipc`, `:ipc_json`, `:ipc_lsp` | `kotatsu::ipc::codec`, `kotatsu::ipc`, `kotatsu::ipc::lsp`          |                               |
| http    | none; HTTP builds with CMake only                                | `kotatsu::http`                                                     | libcurl                       |
| zest    | `:zest`, `:zest_async`                                           | `kotatsu::zest`, `kotatsu::zest::async`                             | cpptrace                      |

Bazel splits the JSON parts of ipc into targets of their own (`:ipc_codec_json`, `:ipc_json`); CMake builds them into `kotatsu::ipc::codec` and `kotatsu::ipc` when simdjson is enabled. The module pages say what each target holds.

## Bazel

Every commit of `main` that passes the Bazel build is published to the [clice Bazel registry](https://bazel.clice.io) as version `0.1.0.<n>`, with the third-party libraries kotatsu uses; [metadata.json](https://bazel.clice.io/modules/kotatsu/metadata.json) lists the versions. Your build lists the registry before the Bazel Central Registry:

```
common --registry=https://bazel.clice.io/
common --registry=https://bcr.bazel.build/
```

```starlark
bazel_dep(name = "kotatsu", version = "0.1.0.<n>")
```

Your build brings the C++ toolchain, `--cxxopt=-std=c++23`, and on macOS `--macos_minimum_os=15.0`, below which the SDK hides libc++ features kotatsu uses. A build without exceptions or RTTI sets the flags below; the simdjson one resolves when your `MODULE.bazel` depends on simdjson too (`bazel_dep(name = "simdjson", version = "4.2.4.clice.1")`).

| Flag                                                                | Effect                            |
| ------------------------------------------------------------------- | --------------------------------- |
| `--@kotatsu//bazel:exceptions=false --@simdjson//:exceptions=false` | builds kotatsu without exceptions |
| `--@kotatsu//bazel:rtti=false`                                      | builds kotatsu without RTTI       |

## CMake

kotatsu is consumed from source, with `FetchContent` or `add_subdirectory`; it fetches its third-party libraries the same way, at pinned versions. libcurl's TLS is the system's: Schannel on Windows, and OpenSSL elsewhere, whose development files must be installed.

```cmake
set(CMAKE_CXX_STANDARD 23)

include(FetchContent)
FetchContent_Declare(kotatsu
    GIT_REPOSITORY https://github.com/clice-io/kotatsu.git
    GIT_TAG <commit>)
set(KOTATSU_CODEC_ENABLE_SIMDJSON ON)
set(KOTATSU_ENABLE_HTTP ON)
FetchContent_MakeAvailable(kotatsu)

target_link_libraries(app PRIVATE kotatsu::http kotatsu::ipc)
```

| Option                             | Default | Effect                                                               |
| ---------------------------------- | ------- | -------------------------------------------------------------------- |
| `KOTATSU_ENABLE_ASYNC`             | `ON`    | `kotatsu::async`, `kotatsu::ipc`, `kotatsu::ipc::lsp`; fetches libuv |
| `KOTATSU_ENABLE_DECO`              | `ON`    | `kotatsu::deco` and `kotatsu::deco::option`                          |
| `KOTATSU_CODEC_ENABLE_SIMDJSON`    | `OFF`   | `kotatsu::codec::json`, and the JSON parts of ipc                    |
| `KOTATSU_CODEC_ENABLE_TOML`        | `OFF`   | `kotatsu::codec::toml`                                               |
| `KOTATSU_CODEC_ENABLE_FLATBUFFERS` | `OFF`   | `kotatsu::codec::flatbuffers`                                        |
| `KOTATSU_ENABLE_HTTP`              | `OFF`   | `kotatsu::http`; fetches libcurl, turns on async                     |
| `KOTATSU_ENABLE_ZEST`              | `OFF`   | `kotatsu::zest`, `kotatsu::zest::async`; turns on deco and async     |
| `KOTATSU_ENABLE_EXCEPTIONS`        | `ON`    | builds with exceptions                                               |
| `KOTATSU_ENABLE_RTTI`              | `ON`    | builds with RTTI                                                     |

`kotatsu::codec` carries every backend that is enabled. The exception and RTTI flags travel with kotatsu's targets to the code that links them, with the definitions `KOTATSU_ENABLE_EXCEPTIONS` and `KOTATSU_ENABLE_RTTI` (`0` or `1`).

## Developing kotatsu

The development environment comes from [pixi](https://pixi.sh). The CMake build's presets are in `CMakePresets.json` (`debug`, `relwithdebinfo`, `asan`, `ubsan`, `asan-ubsan`, `tsan`, `coverage`, `no-exceptions`, `no-exceptions-no-rtti`); each builds into `build/<preset>/`.

```shell
pixi run build [preset]              # configure and build, debug by default
pixi run test [preset]               # every test through ctest
pixi run integration-test [preset]   # the TypeScript integration tests alone
pixi run coverage                    # line coverage per module
pixi run format                      # format every source file
pixi run lsp-codegen                 # regenerate the LSP protocol headers
pixi run -e bazel bazel test //...   # the Bazel build and its tests
```

The Bazel build takes the configurations of `.bazelrc`: `--config=no-exceptions`, `--config=no-rtti`, `--config=asan`, `--config=ubsan` and `--config=tsan`.

The zest tests build into two binaries, `unit_tests` and `system_tests`. To run one by hand, for example with `--test-filter`, run it from the repository root with `--snapshot-dir=tests/snapshots`:

```shell
./build/debug/unit_tests --test-filter='fs_*' --snapshot-dir=tests/snapshots
```

Tests live in `tests/<module>/`: `unit/` in memory, `system/` against the OS, `integration/` real programs driven over their protocols from TypeScript.
