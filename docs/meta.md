# meta

`kotatsu::meta` is the compile-time reflection layer: the fields of an
aggregate, the enumerators of an enum and the names of types and members, read
from what the compiler knows, with no registration macro or code generation. It
also sorts types into a `type_kind`, compares values deeply (`meta::eq`), and
defines the attributes, `repr` and type descriptors the codec module consumes.

meta encodes nothing. Attributes and reprs take effect when a [codec](codec.md)
backend reads or writes the type; `spec.h` documents each attribute, and the
codec page shows one in use. The macros are in `kotatsu/codec/macro.h`:
`KANNOTATE` annotates a field, `KANNOTATE_TYPE` a type, and `KANNOTATION`
names a bundle of attributes either reuses.

## Targets

|            |                                                                |
| ---------- | -------------------------------------------------------------- |
| Bazel      | `@kotatsu//:meta`                                              |
| CMake      | `kotatsu::meta`, always built                                  |
| Headers    | [include/kotatsu/meta/](../include/kotatsu/meta/), header-only |
| Depends on | [support](support.md)                                          |

## Example

```cpp
#include <print>

#include "kotatsu/meta/enum.h"
#include "kotatsu/meta/struct.h"

namespace meta = kotatsu::meta;

struct Point {
    int x;
    double z;
};

enum class Color { Red, Green, Blue };

static_assert(meta::field_count<Point>() == 2);
static_assert(meta::field_name<1, Point>() == "z");

int main() {
    Point p{.x = 1, .z = 2.0};
    meta::for_each(p, [](auto field) {
        std::println("{} at offset {}", field.name(), field.offset());
    });
    std::println("{}", meta::enum_name(Color::Blue));                   // Blue
    std::println("{}", meta::enum_name(static_cast<Color>(42), "?"));   // ?
    std::println("{}", meta::enum_value<Color>("Green") == Color::Green);  // true
}
```

Adapted from `tests/meta/unit/struct_tests.cpp` and `tests/meta/unit/enum_tests.cpp`.

## Overview

| Header                                                                                                                                     | Contents                                                                                                                                                                                                                                                                                                     |
| ------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| [struct.h](../include/kotatsu/meta/struct.h)                                                                                               | Aggregate reflection: `field_count`, `field_names`, `field_refs`, `field_offset`, `for_each`, the `reflectable_class` concept                                                                                                                                                                                |
| [enum.h](../include/kotatsu/meta/enum.h)                                                                                                   | Enum reflection: `enum_name(value)`, `enum_value<E>(name)`, the members in `reflection<E>`                                                                                                                                                                                                                   |
| [name.h](../include/kotatsu/meta/name.h)                                                                                                   | Names from `std::source_location`: `type_name<T>()`, `enum_name<V>()`, `pointer_name`, `member_name`                                                                                                                                                                                                         |
| [spelling.h](../include/kotatsu/meta/spelling.h)                                                                                           | Enumerator names through a rename policy: `map_enum_to_string`, `map_string_to_enum`, `enum_strings`                                                                                                                                                                                                         |
| [type_kind.h](../include/kotatsu/meta/type_kind.h)                                                                                         | `type_kind` and `kind_of<T>()`, the shape codec dispatches on, with concepts such as `int_like` and `str_like`                                                                                                                                                                                               |
| [compare.h](../include/kotatsu/meta/compare.h)                                                                                             | `eq`, `ne`, `lt`, `le`, `gt`, `ge`: transparent functors that also compare aggregates field by field, variants and ranges                                                                                                                                                                                    |
| [spec.h](../include/kotatsu/meta/spec.h), [attrs.h](../include/kotatsu/meta/attrs.h), [annotation.h](../include/kotatsu/meta/annotation.h) | The attributes codec acts on (`rename`, `skip`, `flatten`, `rename_all`, `tag`, ...), each documented in spec.h; a field's annotation, found by its name (`field_attrs_t<T, I>`) and composed over its type's; and the specs other modules add (cli's `ArgSpec`), found with `extension_of` or `extension()` |
| [repr.h](../include/kotatsu/meta/repr.h)                                                                                                   | `repr<T>` and `repr<T, Format>`: specialize one to say what a type is encoded as                                                                                                                                                                                                                             |
| [type_info.h](../include/kotatsu/meta/type_info.h), [schema.h](../include/kotatsu/meta/schema.h)                                           | `type_info_of<T, Config>()` descriptors and `virtual_schema<T, Config>`, resolved as the codec resolves them                                                                                                                                                                                                 |

## Platform notes

- Names are parsed out of the compiler's function signature, so a type's
  spelling (and a variant's default tag names) can differ by compiler and platform.
- Enum reflection probes the underlying values -128..127 (0..127 and the top
  128 values for an unsigned type); an enumerator outside them is not reflected.
- Aggregate reflection handles structs of up to 72 fields.
- `kind_of` sorts integers by size: `long` is `int32` on Windows, `int64` elsewhere.

## See also

[codec](codec.md), [support](support.md), [cli](cli.md), [zest](zest.md)
(its checks compare through `meta::eq`), [tests/meta](../tests/meta/).
