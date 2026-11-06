# meta

`kotatsu::meta` is the compile-time reflection layer: the fields of an
aggregate, the enumerators of an enum and the names of types and members, read
from what the compiler knows, with no registration macro or code generation. It
also sorts types into a `type_kind`, compares values deeply (`meta::eq`), and
finds and composes the annotations written on fields and types.

meta encodes nothing and knows no attribute: each module defines its own and
the specs they fold into, as the [codec](codec.md) does for its attributes
(`rename`, `presence`, `tag`, ...) and cli for its options. The macros are in
`kotatsu/codec/macro.h`: `KANNOTATE` annotates a field, `KANNOTATE_TYPE` a
type, and `KANNOTATION` names a bundle of attributes either reuses.

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

static_assert(meta::field_count<Point> == 2);
static_assert(meta::field_names<Point>[1] == "z");

int main() {
    Point p{.x = 1, .z = 2.0};
    for(std::size_t i = 0; i < meta::field_count<Point>; ++i) {
        std::println("{} at offset {}", meta::field_names<Point>[i], meta::field_offsets<Point>[i]);
    }
    meta::apply_fields(p, [](int& x, double& z) { x += static_cast<int>(z); });
    std::println("{}", meta::enum_name(Color::Blue));                   // Blue
    std::println("{}", meta::enum_name(static_cast<Color>(42), "?"));   // ?
    std::println("{}", meta::enum_value<Color>("Green") == Color::Green);  // true
}
```

Adapted from `tests/meta/unit/struct_tests.cpp` and `tests/meta/unit/enum_tests.cpp`.

## Overview

| Header                                               | Contents                                                                                                                                                                                                                                |
| ---------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [struct.h](../include/kotatsu/meta/struct.h)         | Aggregate reflection: the `reflectable_class` concept, `field_count`, `field_names` and `field_offsets` (variables), `field_type<T, I>`, `field_of<I>`, `apply_fields`, and `field_offset` of a member pointer                          |
| [enum.h](../include/kotatsu/meta/enum.h)             | Enum reflection: `enum_values<E>` and `enum_names<E>` (variables), `enum_index`, `enum_name(value)`, `enum_value<E>(name)`                                                                                                              |
| [name.h](../include/kotatsu/meta/name.h)             | Names from `std::source_location`: `type_name<T>`, `qualified_type_name<T>`, `member_name<&S::m>` (variables)                                                                                                                           |
| [type_kind.h](../include/kotatsu/meta/type_kind.h)   | `type_kind` and `kind_of<T>()`, the shape codec dispatches on, with concepts such as `int_like` and `str_like`                                                                                                                          |
| [annotation.h](../include/kotatsu/meta/annotation.h) | Annotations: `make_annotation` over the attributes a module defines, bundles, a field's annotation found by its name and composed over its type's (`field_attrs_t<T, I>`), and the specs they fold into (`spec_attr_t`, `spec_refs_of`) |
| [compare.h](../include/kotatsu/meta/compare.h)       | `eq`, `ne`, `lt`, `le`, `gt`, `ge`: transparent functors that also compare aggregates field by field, variants and ranges                                                                                                               |

## Platform notes

- Names are parsed out of the compiler's function signature, so a type's
  spelling (and a variant's default tag names) can differ by compiler and platform.
- Enum reflection probes the underlying values -128..127 (0..127 and the top
  128 values for an unsigned type); an enumerator outside them is not reflected.
- Aggregate reflection handles structs of up to 72 fields, reaching them through
  one structured binding of them all: a binding pack (`auto& [...fields]`) on
  clang 21+ and GCC 16+, which take it in C++23 as an extension, and a
  specialization per field count elsewhere (MSVC, older clang and GCC).
- `kind_of` sorts integers by size: `long` is `int32` on Windows, `int64` elsewhere.

## See also

[codec](codec.md), [support](support.md), [cli](cli.md), [zest](zest.md)
(its checks compare through `meta::eq`), [tests/meta](../tests/meta/).
