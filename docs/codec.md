# codec

`kotatsu::codec` reads and writes C++ values from their type alone:
reflection supplies the fields; attributes, `meta::repr` and a config control
how they are written. Every backend but blob reads the same attributes, as far
as its format allows: the binary ones write no field names, so a rename does
nothing there, and bincode writes every field, so `skip_if` omits none. Encoding and
decoding return `std::expected<..., rich_error>`, whose error carries the path
to the failing value (`a.b[3]`) and, where the backend knows one, its line.

The attributes and `repr` are [meta](meta.md)'s; messages and RPC over these
codecs are [ipc](ipc.md)'s.

## Targets

|            |                                                                                                                                                                                               |
| ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Bazel      | `@kotatsu//:codec` (the protocol, dyn, bincode, blob, debug); `@kotatsu//:codec_json`, `@kotatsu//:codec_toml`, `@kotatsu//:codec_flatbuffers`                                                |
| CMake      | `kotatsu::codec`, always built, with every enabled backend; `kotatsu::codec::json`, `kotatsu::codec::toml`, `kotatsu::codec::flatbuffers`, each with its option (see [building](building.md)) |
| Headers    | [include/kotatsu/codec/](../include/kotatsu/codec/), header-only                                                                                                                              |
| Depends on | [meta](meta.md), [support](support.md); simdjson, toml++ and FlatBuffers for their backends                                                                                                   |

## Example

```cpp
#include <string>
#include <vector>

#include "kotatsu/codec/json/json.h"

namespace json = kotatsu::codec::json;

struct Project {
    KANNOTATE(rename = "compileCommands")
    <std::string> compile_commands;
    std::vector<int> ports;
};

int main() {
    auto text = json::to_string(Project{.compile_commands = "build", .ports = {80}});
    // *text == R"({"compileCommands":"build","ports":[80]})"

    auto back = json::from_string<Project>(*text);
    // back->compile_commands == "build"

    auto bad = json::from_string<Project>(R"({"compileCommands":"b","ports":[1,"x"]})");
    // !bad, and bad.error().format_path() == "ports[1]"
}
```

Adapted from `tests/codec/unit/macro_tests.cpp` and `tests/codec/json/unit/decode_tests.cpp`.

## Overview

| Header                                                                                                               | Contents                                                                                                                                  |
| -------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| [macro.h](../include/kotatsu/codec/macro.h)                                                                          | `KANNOTATE(...)` on a field, `KANNOTATION(name, ...)` for a reusable tag, `KANNOTATE_TYPE(struct X, ...)` above a type                    |
| [visit/encode.h](../include/kotatsu/codec/visit/encode.h), [visit/decode.h](../include/kotatsu/codec/visit/decode.h) | The protocol: `encode_value` / `decode_value`, overridden per visitor by `serialize_visit` / `deserialize_visit`                          |
| [visit/config.h](../include/kotatsu/codec/visit/config.h)                                                            | `default_config<UserConfig>`, which fills in the knobs a config leaves out (`enum_repr`, `nan_repr`, `deny_unknown_fields`, ...)          |
| [visit/context.h](../include/kotatsu/codec/visit/context.h)                                                          | `rich_error`, and `scoped_context<T>` for context a visitor or repr reads                                                                 |
| [json/json.h](../include/kotatsu/codec/json/json.h)                                                                  | JSON over simdjson: `to_string`, `from_string`, `prettify`; a `codec::RawValue` is spliced in verbatim                                    |
| [json/schema.h](../include/kotatsu/codec/json/schema.h)                                                              | The JSON Schema of a type: `schema<T>()`, `schema_string<T>()`                                                                            |
| [toml/toml.h](../include/kotatsu/codec/toml/toml.h)                                                                  | TOML over toml++: `to_string` / `from_string`, `to_toml` / `from_toml` on a `toml::Table`                                                 |
| [dyn/dyn.h](../include/kotatsu/codec/dyn/dyn.h)                                                                      | `dyn::Value`, an ordered DOM with `Cursor` navigation; `to_dyn` / `from_dyn`                                                              |
| [bincode/bincode.h](../include/kotatsu/codec/bincode/bincode.h)                                                      | `to_bytes` / `from_bytes`, not self-describing: decode with the type and config that encoded                                              |
| [blob/blob.h](../include/kotatsu/codec/blob/blob.h)                                                                  | Zero-copy blobs read in place as the struct: `to_bytes`, `view`, `view_verified`, `from_bytes`, `measure`; no config, no schema evolution |
| [fbs/fbs.h](../include/kotatsu/codec/fbs/fbs.h)                                                                      | FlatBuffers laid out from the type: `to_bytes` / `from_bytes`, and zero-copy views over a verified buffer                                 |
| [debug/debug.h](../include/kotatsu/codec/debug/debug.h)                                                              | `to_string` in Rust's Debug style, for logs; encode-only                                                                                  |

## Platform notes

- Blobs need a little-endian host, and their bytes must start at
  `blob::alignment` (8), which the vector `to_bytes` returns satisfies.
- `fbs::table_view` needs the buffer at the alignment `to_bytes` gave it;
  `fbs::from_bytes` decodes it at any address.

## See also

[meta](meta.md) (attributes, `repr`), [ipc](ipc.md), [support](support.md),
[tests/codec](../tests/codec/).
