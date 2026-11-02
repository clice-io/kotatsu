# codec

`kotatsu::codec` reads and writes C++ values from their type alone:
reflection supplies the fields; attributes, `codec::repr` and a format control
how they are written. Every backend but blob reads the same attributes, as far
as its format allows: the binary ones write no field names, so a rename does
nothing there, and bincode writes every field, so `presence` omits none. Encoding and
decoding return `std::expected<..., rich_error>`, whose error carries the path
to the failing value (`a.b[3]`) and, where the backend knows one, its line.

The attributes are written in [meta](meta.md)'s annotations; messages and RPC
over these codecs are [ipc](ipc.md)'s.

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
    KANNOTATE(&compile_commands, rename = "compileCommands")
    std::string compile_commands;
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

| Header                                                                                       | Contents                                                                                                                                                                                                                                       |
| -------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [macro.h](../include/kotatsu/codec/macro.h)                                                  | `KANNOTATE(&field, ...)` above a field, `KANNOTATE_FIELD(&T::field, ...)` outside its class, `KANNOTATION(name, ...)` for a reusable bundle, `KANNOTATE_TYPE(struct X, ...)` above a type                                                      |
| [attribute.h](../include/kotatsu/codec/attribute.h)                                          | The attributes: `rename`, `alias`, `skip`, `flatten`, `presence`, `as`, `with`; `rename_all`, `presence_all`, `deny_unknown_fields`; `tagged`, `tag`, `content`, `tag_names`; `description`, `minimum`, `maximum`, `choices`, `schema_default` |
| [encode.h](../include/kotatsu/codec/encode.h), [decode.h](../include/kotatsu/codec/decode.h) | The protocol: `encode_value` / `decode_value`, overridden per visitor by `serialize_visit` / `deserialize_visit`                                                                                                                               |
| [format.h](../include/kotatsu/codec/format.h)                                                | `codec::format`, the root a backend's format extends (`rename_all`, `presence_all`, `deny_unknown_fields`, `human_readable`), `extend<F>` to derive one; `encode_options` / `decode_options` for one call                                      |
| [repr.h](../include/kotatsu/codec/repr.h)                                                    | `repr<T, F>`, the type a value travels as (`to` / `from`) or how a visitor writes it (`encode` / `decode`); `enum_names`, `raw_value`                                                                                                          |
| [type_info.h](../include/kotatsu/codec/type_info.h)                                          | `type_info::of<T, F>()`, what a type is at run time; `field<T, I, F>`, a field as `visit_field` receives it                                                                                                                                    |
| [context.h](../include/kotatsu/codec/context.h)                                              | `rich_error`, and `scoped_context<T>` for context a visitor or repr reads                                                                                                                                                                      |
| [json/json.h](../include/kotatsu/codec/json/json.h)                                          | JSON over simdjson: `to_string`, `from_string`, `prettify`; a `codec::raw_value` is spliced in verbatim                                                                                                                                        |
| [json/schema.h](../include/kotatsu/codec/json/schema.h)                                      | The JSON Schema of a type under a JSON format, with the defaults it states: `schema<T, F>()`, `schema_string<T, F>()`, given `schema_options`                                                                                                  |
| [toml/toml.h](../include/kotatsu/codec/toml/toml.h)                                          | TOML over toml++: `to_string` / `from_string`, `to_toml` / `from_toml` on a `toml::Table`                                                                                                                                                      |
| [dyn/dyn.h](../include/kotatsu/codec/dyn/dyn.h)                                              | `dyn::Value`, an ordered DOM with `Cursor` navigation; `to_dyn` / `from_dyn`                                                                                                                                                                   |
| [bincode/bincode.h](../include/kotatsu/codec/bincode/bincode.h)                              | `to_bytes` / `from_bytes`, not self-describing: decode with the type and format that encoded                                                                                                                                                   |
| [blob/blob.h](../include/kotatsu/codec/blob/blob.h)                                          | Zero-copy blobs read in place as the struct: `to_bytes`, `view`, `view_verified`, `from_bytes`, `measure`; no format, no schema evolution                                                                                                      |
| [fbs/fbs.h](../include/kotatsu/codec/fbs/fbs.h)                                              | FlatBuffers laid out from the type: `to_bytes` / `from_bytes`, and zero-copy views over a verified buffer                                                                                                                                      |
| [debug/debug.h](../include/kotatsu/codec/debug/debug.h)                                      | `to_string` in Rust's Debug style, for logs; encode-only                                                                                                                                                                                       |

## Customizing

- A field: `KANNOTATE(&field, ...)` with the attributes above.
- A type: `KANNOTATE_TYPE(struct X, ...)` for how its own fields are named and
  marked present, or how a variant is tagged; `codec::repr<T>` for what it
  travels as, `repr<T, F>` under the format F and those extending it.
- A backend's documents: a format extending the backend's own, given to the
  entry point, `json::to_string<lsp_format>(value)` with
  `struct lsp_format : codec::extend<json::format> { using rename_all = ...; };`.
- One call: `encode_options` / `decode_options`, such as
  `{.deny_unknown_fields = true}`.
- A visitor: `serialize_visit<Vis, T>` / `deserialize_visit<Vis, T>`; the
  `codec::field` its `visit_field` receives reads the attributes a module of
  its own defines through `extension<Spec>()`.

## Platform notes

- Blobs need a little-endian host, and their bytes must start at
  `blob::alignment` (8), which the vector `to_bytes` returns satisfies.
- `fbs::table_view` needs the buffer at the alignment `to_bytes` gave it;
  `fbs::from_bytes` decodes it at any address.

## See also

[meta](meta.md) (annotations), [ipc](ipc.md), [support](support.md),
[tests/codec](../tests/codec/).
