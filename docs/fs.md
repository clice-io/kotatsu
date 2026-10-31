# fs

`kotatsu::fs` works with paths, files and directories synchronously, on UTF-8
strings (WTF-8 on Windows). It depends on [support](support.md) alone and needs
no event loop. For file I/O inside coroutines, use `kotatsu::async::fs` in
[async](async.md), which runs file operations on a thread pool.

Functions that return text write it into a buffer the caller gives
(`kotatsu::buffer_ref`); what can fail returns `std::expected<T, std::error_code>`
or a `std::error_code`.

## Targets

|            |                                               |
| ---------- | --------------------------------------------- |
| Bazel      | `@kotatsu//:fs`                               |
| CMake      | `kotatsu::fs`, always built                   |
| Headers    | [include/kotatsu/fs/](../include/kotatsu/fs/) |
| Depends on | [support](support.md)                         |

## Example

```cpp
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>

#include "kotatsu/fs/directory.h"
#include "kotatsu/fs/file.h"
#include "kotatsu/fs/path.h"

namespace fs = kotatsu::fs;

std::error_code save_and_list(std::string_view dir) {
    std::string file(dir);
    fs::path::push(file, "note.txt");
    if(auto error = fs::write_file_atomic(file, "hello")) {
        return error;
    }
    for(auto& entry: fs::walk(dir)) {  // depth first
        if(entry) {  // else a directory it could not read
            std::printf("%s\n", entry->path.c_str());
        }
    }
    return {};
}
```

Adapted from `tests/fs/system/file_tests.cpp` and `directory_walk_tests.cpp`.

## Overview

| Header                                           | Contents                                                                                             |
| ------------------------------------------------ | ---------------------------------------------------------------------------------------------------- |
| [path.h](../include/kotatsu/fs/path.h)           | Lexical paths in the POSIX or Windows style on any host: components, `push`, `normalize`, `relative` |
| [status.h](../include/kotatsu/fs/status.h)       | `status`, `symlink_status`, `exists`, `set_times`, `set_permissions`                                 |
| [directory.h](../include/kotatsu/fs/directory.h) | `Directory`, `walk`, `create_directories`, `remove_all`, `rename`, `rename_noreplace`, links         |
| [file.h](../include/kotatsu/fs/file.h)           | `File` and `OpenOptions`, locks; `read_file`, `write_file_atomic`, `copy_file`                       |
| [temp.h](../include/kotatsu/fs/temp.h)           | `TempFile` and `TempDirectory`, deleted when destroyed                                               |
| [mapping.h](../include/kotatsu/fs/mapping.h)     | `Mapping`, a file's bytes mapped read only                                                           |
| [volume.h](../include/kotatsu/fs/volume.h)       | `volume`: space, type and remoteness of a file system                                                |
| [resolve.h](../include/kotatsu/fs/resolve.h)     | `canonical`, `weakly_canonical`, `absolute`, `current_dir`                                           |
| [windows.h](../include/kotatsu/fs/windows.h)     | Windows only: `to_native` / `from_native` paths, Win32 errors as `std::error_code`                   |
| [cygwin.h](../include/kotatsu/fs/cygwin.h)       | Cygwin and MSYS2 POSIX paths and mount tables                                                        |

## Platform notes

- Windows: paths take the `\\?\` prefix inside, so long paths work; Win32
  errors compare equal to `std::errc` codes where one fits.
- Windows: `remove` and `rename` act as on POSIX where the file system allows
  (NTFS): the name disappears at once, even while another process holds the
  file open.
- Windows: junctions report as `FileType::Symlink`, and `walk` and
  `remove_all` never go through them.
- Locks are advisory; on Windows they keep no one from the file's bytes.

## See also

[support](support.md) (`buffer_ref`, `string_list_ref`), [sys](sys.md),
[async](async.md), [tests/fs](../tests/fs/).
