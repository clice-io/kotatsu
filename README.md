# kotatsu

`kotatsu` is a C++23 toolkit extracted from the `clice` ecosystem.
It started as a coroutine wrapper around [libuv](https://github.com/libuv/libuv), and now also includes compile-time reflection, an attribute-driven codec framework, a typed IPC layer with generated LSP protocol bindings, an HTTP client on libcurl, a lightweight test framework, an LLVM-compatible option parsing library, a declarative CLI layer built on top, and a shared support layer of containers, traits, and string utilities.

All public APIs live under the `kota::` namespace, public headers under `include/kota/`, and CMake options use the `KOTA_` prefix.

## Feature Coverage

### `async` runtime (`include/kota/async/*`)

- Typed coroutine tasks `task<T, E, C>` with explicit value, error, and cancellation channels, surfaced through `outcome<T, E, C>`.
- Task composition with sibling cancellation:
  - `when_all(...)` — wait for all children; first error cancels the rest.
  - `when_any(...)` — race children; the winner cancels the rest.
  - `task_group` — spawn a dynamic fan-out of tasks that start immediately, then join; a failure cancels the rest, a child that ends cancelled just ends.
  - `with_task_group(body)` — a `task_group` in a frame of its own, which ends once every child has.
- Cooperative cancellation model:
  - `cancellation_token` / `cancellation_source` for cancelling from outside the tasks, on their loop's thread (post from other threads through a `relay`).
  - `with_token(task, tokens...)` races a task against one or more tokens; `with_timeout(task, timeout)` against a deadline.
  - `token.on_cancel(callback)` runs a callback when the source cancels, until its registration goes.
  - `co_await cancel()` explicitly transitions a task to cancelled.
  - `.catch_cancel()` converts cancellation into an explicit `outcome` channel.
  - `.or_fail()` short-circuits error propagation without resuming at the await site.
- Single-threaded libuv-backed `event_loop`; `run(tasks...)` helper; thread-safe `relay` for hopping onto a loop from another thread.
- Network and IPC I/O:
  - stream base abstraction, with `read_line()` and `read_to_end()`
  - pipes, TCP sockets, TCP acceptors, console / TTY streams
  - UDP sockets with multicast and per-packet send/recv
- Child process API (`process::spawn`) on `sys::spawn` rather than libuv's: stdio piping, environment changes, a group of its own (`process_group`) that `kill_tree` ends as a whole, children killed with their parent (`exit_with_parent`; Linux and Windows), async wait/kill, exit statuses that tell how the child ended and the CPU time it used; the exit is watched through a pidfd on Linux (SIGCHLD before 5.3), kqueue on macOS and the process handle on Windows, and a child whose process was dropped is still reaped. `process::capture` runs a child to its exit and returns its output, killing it, or its group, if cancelled. On Windows `process::open(pid, access)` waits the same way for a process another started, and reads its exit status, its handle (`native_handle()`) holding the further rights `access` asks for, such as an injector's to write its memory; POSIX tells a process no exit status of one it did not start.
- Async filesystem API, `kota::fs::async`, covering the full libuv fs surface on libuv's thread pool (stat / mkdir / scandir / chmod / link / rename / sendfile / utime / mkstemp / …); `fs` below does the same synchronously, without libuv.
- Libuv watcher wrappers: timer, idle, prepare, check, signal, plus a `sleep` helper.

> **Filesystem-change notifications** are no longer provided. The former `fs_event`
> watcher has been removed — the underlying platform backends proved hard to use and
> unreliable across OSes. For change detection, build a periodic poll on top of the
> existing primitives: a `kota::timer` firing at your desired interval combined with
> `fs::status` (comparing mtime / size) gives predictable, portable "did this path
> change?" semantics without the platform-specific pitfalls.

- Blocking-work offload via `queue(fn, loop)` onto the libuv thread pool.
- Coroutine-friendly sync primitives: mutex, semaphore, event, and condition variable; `scoped_lock()` / `scoped_acquire()` give guards that let go when a cancel ends the task.
- `co_invoke(fn, args...)` keeps a capturing lambda coroutine and its arguments alive for its task.
- Error vocabulary: `error` (libuv status wrapper with named codes) and `result<T>`, an `outcome<T, error>`.

### `meta` (`include/kota/meta/*`)

- Aggregate reflection (`struct.h`): field count, field names, field references, field metadata iteration, field offsets (by index and by member pointer), and a `reflectable_class<T>` concept.
- Enum reflection (`enum.h`): bidirectional `enum_name<E>(value)` / `enum_value<E>(name)` lookups.
- Compile-time identifier extraction (`name.h`) driven by `std::source_location`: `type_name<T>()`, `pointer_name<>()`, `member_name<>()`.
- Type classification (`type_kind.h`): a `type_kind` enum plus companion concepts such as `int_like`, `uint_like`, `str_like`, `bytes_like`, `tuple_like`.
- Runtime type metadata (`type_info.h`): typed descriptors (`struct_type_info`, `enum_type_info`, `tuple_type_info`, `variant_type_info`, `array_type_info`, `map_type_info`, `optional_type_info`) accessible through `type_info_of<T, Config>()`.
- Reflection-powered comparison (`compare.h`): transparent `eq` / `ne` / `lt` / `le` / `gt` / `ge` functors that recursively handle aggregates, variants, optionals, and ranges.
- Attributes for the codec layer (`spec.h`, `attrs.h`, `annotation.h`):
  - field values: `rename`, `alias`, `description`, `skip`, `flatten`, `defaulted`, `skip_if` (a built-in `skip_when` condition or a predicate type), and `idx`, which is metadata only: it reaches `field_info`, but no backend lays fields out by it
  - struct/variant values: `rename_all`, `deny_unknown_fields`, and variant tagging (`tagged` / `tag` / `content` / `tag_names`) in external, internal or adjacent form
  - behaviors: `as<Target>` (travel as another type), `with<Adapter>` (a per-field representation), `enum_string<Policy>`, `skip_if<Pred>`; a predicate taking only the value applies when encoding, one taking `(value, is_serialize)` decides both ways
  - declared with `KOTATSU_ANNOTATE(...)` on a field or `KOTATSU_ANNOTATION(name, ...)` for a reusable tag (both in `kota/codec/macro.h`), or with `annotate<Tag>::type<T>` / `annotation<T, Attrs...>` (which wrap, inherit or inherit-and-use T as its kind requires); the strings live in one constexpr spec per annotation, so they never enter mangled names
- Type representations (`repr.h`): specializing `repr<T>` or `repr<T, Format>` says what a type travels as, declaratively (`to` / `from`) or imperatively (`serialize` / `deserialize` over the visitor); a repr declaring `meta::dynamic` covers shapes known only at run time (not on FlatBuffers). Reprs chain, and a format-scoped one wins for its backend.
- Compile-time schema (`schema.h`, `type_info.h`): `virtual_schema<T, Config>` lists a struct's slots (`field_slot<RawType, BehaviorAttrs>`) and `field_info` table with flattening and skipping resolved up front; `type_info_of<T, Config>()` gives runtime descriptors (struct, enum, tuple, variant, array, map, optional) that follow reprs and attributes (`resolved_repr_t`) exactly as the codec does.

### `codec` (`include/kota/codec/*`)

- One visitor protocol shared by every backend but blob (`codec/visit/`): `encode_value` / `decode_value` dispatch on a backend override (`serialize_visit` / `deserialize_visit`, specialized per visitor, as ipc does for its protocol types), then annotations and `meta::repr`, then `meta::type_kind`. A backend is a visitor; text backends read data-driven (by key, with speculative `try_read` for untagged variants), binary backends positionally.
- Attributes and config apply the same on every backend of the protocol: renames and aliases, skipping, flattening, defaults and required fields, `as` / `with` / `enum_string`, tagged variants (a tag wins over the variant type's own repr) and untagged ones picked by probing the input. `default_config<UserConfig>` supplies `enum_repr`, `nan_repr`, `deny_unknown_fields`, `detailed_error`, and takes `field_rename` / `enum_rename` policies; `human_readable = false` turns tagging off on a text backend (a binary backend cannot turn it on).
- Errors: every encode and decode entry point returns `std::expected<…, rich_error>` (a FlatBuffers view that fails verification is an invalid view instead, and a blob's `view_verified` reads bytes already checked); a `rich_error` carries the message, the path from the root to the failing value (`a.b[3]`), and a source location where the backend knows one.
- Backends:
  - JSON (`codec/json/`): simdjson-based `to_string` / `from_string`, `prettify`, `RawValue` for pass-through JSON, and JSON Schema generation (`schema<T>()`) that agrees with what the encoder writes.
  - TOML (`codec/toml/`): toml++-based `to_toml` / `from_toml` and `to_string` / `from_string`; values that are not tables are boxed under a root key.
  - dyn (`codec/dyn/`): `dyn::Value`, an ordered DOM (null, bool, signed and unsigned integers, double, string, `Array`, `Object`) with `Cursor` navigation; `to_dyn` / `from_dyn`. JSON, TOML and bincode read and write it as a document of their own (bincode behind a kind byte); JSON Schema generation and the LSP model use it.
  - Bincode (`codec/bincode/`): compact little-endian `to_bytes` / `from_bytes`, not self-describing: decode with the type and config that encoded.
  - Blob (`codec/blob/`): zero-copy blobs read in place as the struct itself. A record holding strings or sequences is a struct template over its sequence type: over `blob::Owned` (`std::string`, `std::vector`) a writer fills it, and `blob::view_t<T>`, the same template over `blob::Vec` (an offset and a count), reads it where it lies. `to_bytes`; `view`, which checks untrusted bytes once against the layout (every part where the writer puts it and inside the bytes, every bool 0 or 1, nothing after the last part), and `view_verified`, which does not; `from_bytes` to copy the value out; `measure` for the bytes each field takes. The layout is a function of the type alone (no config, annotations or reprs), and has no schema evolution: a fingerprint of the layout rejects a blob of another. `to_bytes` zeroes padding, so a value has one blob.
  - FlatBuffers (`codec/fbs/`): `to_bytes` / `from_bytes` with the layout computed from the type (no `.fbs` files), a decoder that verifies every access, and zero-copy views over a buffer verified once: `table_view` (`view[&T::field]`, the member located at compile time), `bound_view` (every member read once), `array_view` (a random-access range, contiguous over integer, `float`, `double`, `char`, `std::byte` and inline-struct elements, so `std::span` builds from it), `map_view`, `variant_view`, `tuple_view`.
  - debug (`codec/debug/`): Rust-Debug-style `to_string` for logs and test output; encode-only.

### `ipc` (`include/kota/ipc/*`)

- JSON-RPC 2.0 protocol model with typed request / notification traits, structured errors (`Error { code, message, data }`), and the full set of spec error codes (including LSP-aligned `RequestCancelled`).
- Transport abstraction for framed message IO: `Transport` interface, `StreamTransport` over stdio (`open_stdio`), a TCP connection (`connect_tcp`) or any pair of async streams, and a `RecordingTransport` decorator that captures traffic to JSONL for replay testing.
- The LSP base protocol's framing in `framing.h`: `frame_header()` writes the `Content-Length` header a payload goes after, and `FrameParser` reads frames from input split anywhere. `StreamTransport` writes each payload as it is after its header, and writes the messages queued together in one write. A frame larger than the transport's limit (64 MiB by default) is skipped, not buffered, and reading goes on; the peer fails only what the frame's first bytes say it concerns: a request is answered with `MessageTooLarge` (-32010), a response fails the request it answers, a notification is dropped, and a message of no kind it can tell fails every pending request.
- The framing, the protocol model and the codecs (`codec.h`, `codec/bincode.h`, `codec/json.h`) build without the event loop, as the CMake target `kota::ipc::codec` (Bazel: `:ipc_codec`, `:ipc_codec_json`): a program that reads and writes messages itself, such as catter's hook library, links neither libuv nor, on Windows, the DLLs it imports.
- Codec-parametric typed peer runtime (`Peer<Codec>`) supporting request dispatch, notifications, and nested RPC; predefined peers for JSON (with LSP camelCase policy) and Bincode codecs, `JSONPeer` (`peer/json.h`) and `BincodePeer` (`peer/bincode.h`). A value is written into its message's buffer as it is encoded, rather than encoded apart and copied in, and params and results are decoded where they lie in the message read.
- Externally-driven execution model: callers own the event loop, schedule the peer's run loop, and drive shutdown explicitly.
- `ipc::Result<T>`, an `outcome<T, Error>`, with protocol validation aligned with the JSON-RPC spec:
  - malformed payloads map to `ParseError` with null id
  - structurally invalid messages map to `InvalidRequest`, answered under the request's id when it can be read, and with null id otherwise
  - parameter decode failures map to `InvalidParams`
  - a handler that throws is answered with `InternalError`
- Cancellation integration with the `async` runtime:
  - inbound `$/cancelRequest` cancels the matching in-flight handler and reports `RequestCancelled`
  - outbound requests accept an optional cancellation token and/or timeout; cancelling a still-pending request sends `$/cancelRequest` to the peer
  - `RequestContext` exposes the inbound handler's cancellation token for easy propagation into nested outbound calls
- Optional structured logging hook (log level + callback).

### `ipc/lsp` (`include/kota/ipc/lsp/*`)

- C++ protocol model generated from the pinned LSP 3.18 meta-model by `scripts/lsp/codegen.ts`: aggregates with inherited properties inlined, same-shaped variant alternatives told apart by their string literal members, and `LSPAny` as `codec::dyn::Value`. Regenerate with `pixi run lsp-codegen`; CI checks that the committed headers are current.
- LSP request / notification traits layered on top of `kota::ipc::protocol`.
- `URI` parsing / manipulation with percent-encoding helpers and `from_file_path` factories.
- Byte-offset ↔ LSP `{line, character}` conversion across UTF-8 / UTF-16 / UTF-32 position encodings (`position.h`): over line starts the caller keeps in any random-access range of unsigned integers, with what it knows of which lines are ASCII; over a text alone, for one conversion; or over the size and line starts of an ASCII text it keeps no copy of.
- `ProgressReporter` helper for `$/progress` work-done notifications.

### `http` (`include/kota/http/*`)

- An HTTP client on [libcurl](https://curl.se/libcurl/) and the `async` runtime: `http::client` holds settings (headers, cookies, user agent, proxy, timeout, redirect policy, TLS options, raw `curl_option`s) that every request copies and may override; `client.on(loop)` binds a copy to an event loop as a `bound_client`, whose `get` / `post` / `put` / `patch` / `del` / `head` / `request(method, url)` build a `request`; `co_await request.send()` gives an `outcome<response, http::error>`.
- Request builders: `query`, `header` / `default_header`, `bearer_auth` / `basic_auth`, `body`, `json_text`, `form`, and `json(value)` through the JSON codec. A request that cannot be sent as built (no url, a body on GET or HEAD, a header that CR or LF would break, ...) fails with `error_kind::invalid_request` before curl sees it; curl's failures carry its code (`error_kind::curl`).
- `response`: the status, the effective url, the last response's headers in order, and the body as bytes or text.
- One `http::manager` per event loop drives its requests on one curl multi handle, with the libuv polls and timer curl asks for. Requests follow the lifecycle of `async`'s resources: cancelling a task cancels its request; the loop's destruction takes its manager, and requests still in flight then end when their tasks are cancelled; a request sent while the loop is being destroyed, like one in flight when `manager::unregister_loop` drops the manager, fails with `error_kind::aborted`.
- Threads: a `bound_client` belongs to the loop it names, and its requests are awaited in tasks on that loop. A client and its copies share one cookie jar, DNS cache and TLS session cache per event loop; used on two loops, a client keeps two of each, since curl cannot share cookies between threads.

### `option` (`include/kota/option/*`)

- LLVM-compatible option parsing model (`OptTable`, `Option`, `ParsedArgument`).
- Supported option kinds: flag, joined (`-O2`), separate (`--output file`), joined-or-separate, joined-and-separate, comma-joined, fixed-arity multi-arg, remaining / trailing argument packs, plus input / group / unknown variants.
- Alias unaliasing (e.g. `-O2` → `-O` + `2`), grouped short options, visibility-based filtering with per-visibility help text.
- Callback-based parse APIs for both per-argument and whole-argv flows.

### `deco` (`include/kota/deco/*`)

- **Dec**larative **o**ption library: describe options as reflected structs rather than imperative tables.
- Compile-time option-table generation driven by `meta` reflection on top of `kota::option`, wired via `DECO_CFG` / `DECO_DECLARE_OPTION_*` macros.
- Field-level attributes: `required`, help text, meta-var, spellings / aliases, category (exclusive / required groups), argument style and arity, `after_parsed` callbacks.
- Sub-command routing, nested config scopes, and built-in usage / help rendering.
- Handlers may return an `int` exit code, which `Command` and `SubCommander` return; an argv that does not parse returns `parse_error_exit_code` (2) unless the error handler returns a code of its own. `decl::HelpOption` gives an options struct the standard `-h` / `--help`, and `SubCommander::enable_help()` lists the subcommands for either.

### `zest` test framework (`include/kota/zest/*`)

- Minimal unit test framework used throughout this repository.
- `ZEST_SUITE` / `ZEST_CASE` / `ZEST_CASE_GROUP` registration with compile-time case attributes (skip / focus / serial / crashes); each case runs on a fresh instance of its suite, whose constructor and destructor set up and tear down. Cases share no suite instance and no order: they run spread over worker processes, several at once.
- Registration survives a linker's garbage collection (`/OPT:REF`, `--gc-sections`, LTO, but for clang's `-fvirtual-function-elimination`): a variable `ZEST_SUITE` puts before the suite keeps the suite and its cases in the program, so a suite may live in any namespace.
- Two checks, `ZEXPECT(expr)` and `ZASSERT(expr)`, plus the compile-time `ZSTATIC_EXPECT(expr)`, split a top-level comparison and show both operands when it fails: `ZEXPECT(parse(text) == expected)`. Comparisons go through `meta::eq` / `lt` / …, so reflectable types compare and print without `operator==`; `ZASSERT(result)` on a `std::expected` shows its error.
- A failed `ZASSERT` does not return: it ends the test's process without unwinding, so it works the same in a coroutine, a helper returning a value or another thread. A `zest::FatalHook` cleans up what would outlive the process, such as a temporary directory or a child process.
- The predicates `contains`, `starts_with`, `ends_with`, `type_eq`, `throws` / `throws<E>` and the snapshots `snapshot(value[, name])` / `snapshot_glob(dir, pattern, transform)` report their inputs (`ZEXPECT(!contains(log, "error"))`, `ZEXPECT(zest::snapshot(render(document)))`), and `ZEST_CONTEXT("…", args…)` adds a line to every check failing inside its scope.
- Default CLI runner: filter by `suite[.test]` with wildcards (`--test-filter=…`) and `--verbose`. Tests run on a pool of worker processes (`--jobs=N`), so a crash, a failed `ZASSERT` or a hang past `--timeout` fails that test alone and a fresh worker takes over; a case declared `crashes = true` passes only by crashing its worker. `--no-isolation` runs the tests in-process for debuggers: crash tests are skipped, and a crash or a failed `ZASSERT` ends the run.
- Failure reporting uses `std::source_location` to point at the failing expression.
- `kota::zest::LoopFixture` (`kota/zest/async.h`, target `kota::zest::async`) for tests of `async` code: it owns an event loop, and its `run(tasks...)` runs the tasks until every one has finished and returns what each ended with, failing the test when they outlast its `watchdog`.

### `fs` (`include/kota/fs/*`)

Synchronous paths and files on UTF-8 strings, depending on `support` alone (no libuv, no async), so that a library injected into other processes can use it.

- Strings out and in: what produces a path or text writes it into a buffer the caller gives, of any type (`std::string`, `small_string<N>`, `llvm::SmallString<N>`) through `buffer_ref`, so that a loop of calls can reuse one buffer and allocate nothing; a list of strings goes in as a `string_list_ref` of whatever the caller holds.
- Lexical paths (`path.h`): roots, `components` and `ancestors` as lazy views, and Rust's `PathBuf` operations in place on a buffer (`push`, `pop`, `set_file_name`, `set_extension`, `normalize`), `relative`, `strip_prefix`, in the POSIX or the Windows style on any host. Windows roots follow drives, UNC shares, device and verbatim (`\\?\`) prefixes; `..` never climbs past a root.
- Status (`status.h`): `status` / `symlink_status` with file IDs, change time, link counts and permissions; on Windows by name where the system allows, junctions and symlinks reported as links; `set_times` and `set_permissions`.
- Directories and links (`directory.h`): `read_dir`, a range of entries read as they are asked for, whose statuses Windows takes from the listing, `create_directories`, `remove` and `rename` with POSIX semantics on Windows, `remove_all` that never follows a link, symlinks (`create_symlink`, and `read_symlink`, which reads Windows junctions too) and hard links.
- Files (`file.h`, `mapping.h`): `File`, opened as Rust's `OpenOptions` say (read, write, append, truncate, create, create new), with `read_to_end`, `write_some`, `seek`, `status`, `canonical` (the name the OS gives the open file), `resize`, `sync`, sparse files, advisory locks (`lock`, `lock_shared`, `try_lock`), which on Windows keep no one from the file's bytes and go with `close`, and `into_descriptor` for the C runtime; `read_file`, which reads rather than maps, `write_file_atomic`, `create_temp_file` and `create_temp_directory`; `Mapping`, a file's bytes mapped read only.
- Volumes (`volume.h`): `volume`, the space, type and remoteness of the file system a path lives on (9p, NFS and SMB are remote; FUSE is told by its type).
- Resolution (`resolve.h`): `canonical`, `weakly_canonical` for paths that may not exist yet, `absolute`, `current_dir`.
- The Windows boundary (`windows.h`): UTF-8 paths as the wide, `\\?\`-prefixed paths the file functions take (`to_native`, `from_native`), and Win32 errors as `std::error_code`; Cygwin and MSYS2 mount tables (`cygwin.h`).

### `support` (`include/kota/support/*`)

- Callable and function-signature traits (`function_traits.h`): `is_function_pointer_v`, `is_functor_v`, and `function_traits<Fn>` exposing `return_type`, `args_type`, `args_count`, plus `member_traits<M C::*>`.
- Containers:
  - `cow_string` — copy-on-write string that can borrow or own its storage
  - `small_vector<T, N>` / `hybrid_vector<T>` — SBO vectors with per-element-size tuned size types
  - `small_string<N>` — SBO string, shares layout with `small_vector<char>`
- Compile-time string utilities: `string_ref`.
- Text encodings: UTF-8 checks (`utf8.h`) and WTF-8 ↔ UTF-16 conversion (`utf16.h`), which keeps a Windows name's lone surrogates.
- Type-erased string references (`buffer_ref.h`, `string_list_ref.h`): `buffer_ref`, any contiguous resizable buffer of characters for a function to write into, and `string_list_ref`, any contiguous range of string-likes for a function to read.
- `small_wstring<N>`, a `small_vector<wchar_t, N>` whose `c_str()` gives the Windows API its NUL-terminated wide strings.
- Naming-convention conversion (`naming.h`): identity, lower-snake, lower-camel, upper-camel, upper-snake.
- `outcome<T, E, C>` (`outcome.h`): a value, an error or a cancellation, which `async`'s tasks, `ipc`'s codecs and `http`'s requests return.
- Type-level utilities: `type_list<Ts...>`, `tuple_traits`, `type_traits`, `expected_try`, `comptime` helpers, and miscellaneous `memory` / `ranges` / `functional` adapters.

### `sys` (`include/kota/sys/*`)

- The process and its system without libuv or `async`, in a static library of one object per area: a program links only what it calls, and on Windows imports only those objects' DLLs. catter's hook library, injected into every process a build starts, takes the environment and program lookup without the rest.
- Platform-specific concepts follow one rule: what portable code calls is declared on every host and fails with `not_supported` where it cannot work (the process title, the OOM score); what only Windows-specific code calls lives in `sys::windows` (`windows.h`), or, a member of a portable type (`Child::open`), is declared on Windows alone.
- Text out goes into a buffer the caller gives (`buffer_ref`) and lists of strings come in as `string_list_ref`, as in `fs`.
- `env.h`: `get_env` / `set_env` / `unset_env` / `environment` (name and value pairs) on the OS's own block (Windows' `GetEnvironmentVariableW`, never the CRT's copy), `env_name_equal`, and `split_paths` (a lazy view) / `join_paths` for PATH-like lists in either style.
- `process.h`: `pid`, `parent_pid`, `process_alive` (a zombie, or a process this one may not signal or open, included), `executable_path` (under the name its file has now), `started_path` (the path it was started by, links not resolved, for a program that acts as whichever it is named after), `self_executable` (a path that starts this very image, for spawning a copy that speaks its protocol: `/proc/self/exe` on Linux, which survives the file's replacement), `arguments` (read from the system, without main's argv; on Windows from `GetCommandLineW`, in WTF-8, by the CRT's rules and without wildcard expansion), `find_program` (execvp's search on POSIX; on Windows PATH and PATHEXT directory by directory, Cygwin symlinks followed; the first file it cannot run named when it fails), the process title (`set_process_title`, Linux and macOS) and name (`set_process_name`, the main thread's on Linux), and `signal_name`.
- `spawn.h`: `spawn` starts a child without an event loop, as Rust's `Command` describes it (`program`, `args` after argv[0], `arg0`, `Stdio::pipe()` by its slot), refusing a string with a NUL, which the child would get cut short, and `Child` waits for it, kills it or its group, and reports its exit with CPU time (POSIX: fork and execve on Linux, `posix_spawn` on macOS, descriptors without close-on-exec inherited, signals at their defaults or as exec leaves them with `keep_signals`, an ignored SIGCHLD put back to its default so that no exit status is lost; Windows: `CreateProcessW` with the three standard handles alone inherited (every inheritable one with `windows_inherit_handles`) and a job object of the child's own from birth, a command line taken as given, a hook while the child is suspended, batch files run through cmd with their arguments escaped as Rust's `Command` escapes them since BatBadBut); `capture` runs one to its end and reads its stdout and stderr into the caller's buffers; on Windows `Child::open(pid, access)` opens a process another started, to wait for, read the exit of and kill as a child, and to reach through `native_handle()` with the further rights `access` asks for.
- `windows.h`: Windows command lines (`quote_argument` / `join_command_line` / `split_command_line`, a lazy view that reads by the C runtime's rules or LLVM's: `cl::TokenizeWindowsCommandLine`, by which clang reads a compilation database and a clang-cl response file, and its `Full` variant, by which an LLVM tool reads its own command line) and `ntstatus_name` (`0xC0000135` is `STATUS_DLL_NOT_FOUND`) on every host; CreateProcess's own program search (`create_process_directories`, `find_create_process_program`) on Windows.
- `dirs.h`: `home_dir`, `expand_tilde` (`~` and, on POSIX, `~user` from the password database), `temp_dir`, and the user's `cache_dir` / `config_dir` / `data_dir` (XDG, `~/Library`, known folders).
- `console.h`: `write_console`, UTF-8 to a Windows console as UTF-16 and bytes anywhere else, allocating nothing and never throwing, and `is_console` / `is_pipe` (a socket included), false for a descriptor not open.
- `info.h`: per-process memory and CPU (`process_stats`, with the peak), the memory a process can take under every level of its cgroups (`memory`, with macOS's compressor and Windows' commit charge counted), `parallelism` under its affinity and cgroup CPU quotas, `cpu_cores`, `os`, `hostname`, `uptime`, `Priority` of a process or of the calling thread (`set_thread_priority`: Windows' thread priorities, macOS's quality of service, Linux's per-thread nice value), and Linux's OOM score adjustment.
- What can fail returns `std::expected<T, std::error_code>`, or a `std::error_code` when there is no value; Windows codes are mapped to `std::errc` where one fits.

## Using kotatsu from Bazel

kotatsu is a Bazel module (Bazel 9). The targets are one per module:
`@kotatsu//:support`, `:fs`, `:meta`, `:codec` with the backends `:codec_json`,
`:codec_toml` and `:codec_flatbuffers`, `:option`, `:deco`, `:async`,
`:ipc_codec` with `:ipc_codec_json` (ipc's framing and codecs, without the
event loop), `:ipc` with `:ipc_json` (JSON-RPC) and `:ipc_lsp`, and `:zest`
with `:zest_async`; HTTP builds with CMake only for now.

```starlark
bazel_dep(name = "kotatsu", version = "0.1.0.<n>")
```

Every commit of `main` that passes the Bazel build is a version of the
module on the [clice Bazel registry](https://bazel.clice.io), `0.1.0.<n>`
for the n-th commit (`git rev-list --count`), as are its third-party
libraries (simdjson, toml++, flatbuffers, libuv, cpptrace). The depending
build lists that registry before the Bazel Central Registry:

```
common --registry=https://bazel.clice.io/
common --registry=https://bcr.bazel.build/
```

The depending build brings the C++23 toolchain (`--cxxopt=-std=c++23`). A
build without exceptions or RTTI sets `--@kotatsu//bazel:exceptions=false`
(and `--@simdjson//:exceptions=false`) or `--@kotatsu//bazel:rtti=false`,
which kotatsu's headers see through `KOTA_ENABLE_EXCEPTIONS` and
`KOTA_ENABLE_RTTI`.

## Repository Layout

```text
include/kota/
  async/       # Coroutine runtime, event loop, I/O, sync primitives, cancellation
  codec/       # Attribute-driven serde framework + JSON / Bincode / TOML / FlatBuffers backends, and zero-copy blobs
  deco/        # Declarative CLI layer on top of option + meta
  fs/          # Synchronous paths, file status, directories and files
  http/        # HTTP client on libcurl and the async runtime
  ipc/         # JSON-RPC peer, transport, codecs
    lsp/       # Generated LSP protocol model + URI / position / progress helpers
  meta/        # Compile-time reflection, attribute markers, schema IR, runtime type info
  option/      # LLVM-compatible option parsing layer
  support/     # Containers, traits, compile-time strings, naming helpers
  sys/         # Environment, command line, directories, memory and CPU, without libuv
  zest/        # Unit test framework

src/
  async/       # Async runtime implementations
  codec/       # Codec backend implementations (content / FlatBuffers)
  deco/        # Deco runtime and text rendering
  fs/          # File system implementations, POSIX and Windows
  http/        # HTTP client implementation
  ipc/         # IPC peer and transport implementations
    lsp/       # URI / position implementations
  option/      # Option parser implementation
  meta/        # Meta target wiring (header-only public APIs)
  sys/         # Process and system information, one file per area
  zest/        # Test runner implementation

bazel/         # Bazel: build settings, the rule running the tests' commands

tests/
  <module>/    # Tests per module, mirroring include/kota/<module>/:
               #   unit/ in memory, system/ touching the OS, harness/ helpers,
               #   integration/ programs over real protocols (TypeScript),
               #   <name>/ a check built against one target alone
  examples/    # Tests of the examples, and the examples run end to end
  fixtures/    # Types shared by several modules' tests
  snapshots/   # Snapshot files, one directory per suite

examples/
  async_basics/    # Introductory async runtime walkthroughs
  build_system/    # Dependency-graph build-system demo
  dump_dot/        # DOT-graph dumping example
  http/            # HTTP client demos (they reach the internet)
  ipc/             # IPC stdio, scripted, and multi-process examples

scripts/
  coverage.ts      # Test coverage per module (`pixi run coverage`)
  lsp/             # LSP meta-model tooling (TypeScript, run by Node)
    metaModel.json # The pinned meta-model, sha256-checked
    metamodel.ts   # Its types, and the schema codegen and the tests read
    codegen.ts     # Meta-model -> C++ protocol header generator
```
