# support

`support` is the vocabulary the other modules share: `outcome`, small
containers and strings, the convention for passing strings in and out, UTF-8
and UTF-16 text, and the traits and helpers the rest of kotatsu is built from.
Its names sit in the root namespace (`kotatsu::outcome`,
`kotatsu::small_vector`), except those in `kotatsu::naming`, `kotatsu::mem`
and `kotatsu::comptime`. It does no I/O: files and paths are [fs](fs.md),
processes and the environment [sys](sys.md).

## Targets

|            |                                                         |
| ---------- | ------------------------------------------------------- |
| Bazel      | `@kotatsu//:support`                                    |
| CMake      | `kotatsu::support`, always built                        |
| Headers    | [include/kotatsu/support/](../include/kotatsu/support/) |
| Depends on | the standard library alone                              |

## Example

```cpp
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "kotatsu/support/buffer_ref.h"
#include "kotatsu/support/small_string.h"
#include "kotatsu/support/string_list_ref.h"

void join(kotatsu::buffer_ref out, kotatsu::string_list_ref parts, std::string_view separator) {
    out.clear();
    for(std::size_t i = 0; i < parts.size(); ++i) {
        out.append(i == 0 ? std::string_view() : separator);
        out.append(parts[i]);
    }
}

int main() {
    std::vector<std::string> words{"src", "main.cpp"};
    std::string path;
    join(path, words, "/");  // "src/main.cpp"
    kotatsu::small_string<32> line;
    join(line, {"a", "b", "c"}, ", ");  // a braced list; the text stays inline
    std::println("{} | {}", path, std::string_view(line));
}
```

Adapted from `tests/support/unit/buffer_ref_tests.cpp` and
`tests/support/unit/string_list_ref_tests.cpp`.

## Overview

| Header                                                                                                                                                                                                                                                                                                                                               | Contents                                                                                                                                            |
| ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| [outcome.h](../include/kotatsu/support/outcome.h)                                                                                                                                                                                                                                                                                                    | `outcome<T, E, C>`: a value, an error or a cancellation, as async tasks, ipc codecs and http requests return                                        |
| [buffer_ref.h](../include/kotatsu/support/buffer_ref.h)                                                                                                                                                                                                                                                                                              | `buffer_ref`: any contiguous, resizable buffer of characters a function writes text into                                                            |
| [string_list_ref.h](../include/kotatsu/support/string_list_ref.h)                                                                                                                                                                                                                                                                                    | `string_list_ref`: any contiguous range of string-likes a function reads                                                                            |
| [small_vector.h](../include/kotatsu/support/small_vector.h)                                                                                                                                                                                                                                                                                          | `small_vector<T, N>` with an inline buffer, and `hybrid_vector<T>` to take one of any `N`                                                           |
| [small_string.h](../include/kotatsu/support/small_string.h)                                                                                                                                                                                                                                                                                          | `small_string<N>`, and `small_wstring<N>` for the wide strings of the Windows API                                                                   |
| [cow_string.h](../include/kotatsu/support/cow_string.h)                                                                                                                                                                                                                                                                                              | `cow_string`: borrows its text or owns a copy, for zero-copy decoding                                                                               |
| [string_ref.h](../include/kotatsu/support/string_ref.h)                                                                                                                                                                                                                                                                                              | `string_ref`: a `std::string_view` with split, trim, take/drop, consume and case-insensitive helpers                                                |
| [utf8.h](../include/kotatsu/support/utf8.h), [utf16.h](../include/kotatsu/support/utf16.h)                                                                                                                                                                                                                                                           | `is_utf8`, `decode_utf8`, `replace_invalid_utf8`; `to_utf16` / `from_utf16` between WTF-8 and UTF-16                                                |
| [naming.h](../include/kotatsu/support/naming.h)                                                                                                                                                                                                                                                                                                      | `kotatsu::naming`: identifier case conversion, `rename_policy`, `Casing`                                                                            |
| [glob_pattern.h](../include/kotatsu/support/glob_pattern.h)                                                                                                                                                                                                                                                                                          | `GlobPattern`: VS Code-style globs, compiled once by `create` and matched without allocating                                                        |
| [scope_exit.h](../include/kotatsu/support/scope_exit.h)                                                                                                                                                                                                                                                                                              | `ScopeExit`: calls a function as its scope is left, unless `release()`d                                                                             |
| [functional.h](../include/kotatsu/support/functional.h)                                                                                                                                                                                                                                                                                              | `function_ref` (borrowed) and `function` (owned, move-only) callables, `bind`, `bind_ref`                                                           |
| [config.h](../include/kotatsu/support/config.h), [expected_try.h](../include/kotatsu/support/expected_try.h)                                                                                                                                                                                                                                         | `KOTATSU_ENABLE_EXCEPTIONS`, `KOTATSU_THROW` and kin; `KOTATSU_EXPECTED_TRY` / `_V` to return a `std::expected`'s error                             |
| [function_traits.h](../include/kotatsu/support/function_traits.h), [type_list.h](../include/kotatsu/support/type_list.h), [tuple_traits.h](../include/kotatsu/support/tuple_traits.h), [type_traits.h](../include/kotatsu/support/type_traits.h), [ranges.h](../include/kotatsu/support/ranges.h), [numeric.h](../include/kotatsu/support/numeric.h) | Type-level helpers: function and callable traits, type lists, tuple queries, `is_specialization_of`, range kinds (sequence, set, map), `narrow_int` |

## Platform notes

- Without exceptions (`KOTATSU_ENABLE_EXCEPTIONS=0`), `KOTATSU_THROW` aborts,
  so `outcome::unwrap()` without a value aborts instead of throwing
  `bad_outcome_access`.
- The `wchar_t` overloads of `to_utf16` and `from_utf16` exist only where
  `wchar_t` is 16 bits, as on Windows.

## See also

[fs](fs.md), [sys](sys.md), [async](async.md), [meta](meta.md),
[building](building.md), [tests/support](../tests/support/).
