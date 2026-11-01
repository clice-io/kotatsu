# cli

`kotatsu::cli` reads a command line into a plain struct: every field is an
option named after it, its type says what it takes, and the `cli::` entries of
a `KANNOTATE`, the macro [codec](codec.md) uses, say anything more. A
nested struct is a group of options, a `std::optional` of one is given all or
nothing, and a `std::variant` of structs is a set of subcommands. `cli::parse`
is a [codec](codec.md) decoder over the argv, so a type with a
[meta](meta.md) `repr` reads as what it is represented by, and `cli::to_argv`
is its encoder. The usage is rendered from the same struct.

Underneath is `kotatsu::cli`'s option table, modelled on LLVM's `OptTable` and
usable alone. cli reads argv only; config files are [codec](codec.md)'s.

## Targets

|            |                                                                                                                                     |
| ---------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| Bazel      | `@kotatsu//:cli`; the option table alone `@kotatsu//:cli_table`                                                                     |
| CMake      | `kotatsu::cli`, and the option table `kotatsu::cli::table`, with `KOTATSU_ENABLE_CLI` (on by default)                               |
| Headers    | [include/kotatsu/cli/](../include/kotatsu/cli/)                                                                                     |
| Depends on | [meta](meta.md), [codec](codec.md), [support](support.md), privately [sys](sys.md); the option table on [support](support.md) alone |

## Example

```cpp
#include <cstdint>
#include <optional>
#include <print>
#include <span>
#include <string>

#include "kotatsu/cli/cli.h"

namespace cli = kotatsu::cli;

KANNOTATE_TYPE(struct Clone, description = "Clone a repository");
struct Clone {
    cli::Help help;
    KANNOTATE(cli::positional = true, cli::meta_var = "<REPO>", description = "Repository URL")
    <std::string> repo;
    KANNOTATE(cli::names = {"-b", "--branch"}, cli::meta_var = "<BRANCH>")
    <std::optional<std::string>> branch;
    std::uint32_t depth = 0;
};

int main(int argc, char** argv) {
    auto options = cli::parse<Clone>(std::span(argv, argc).subspan(1));
    if(!options) {
        cli::report(options.error());
        return 2;
    }
    if(options->help) {
        cli::print_usage<Clone>("clone [OPTIONS] <REPO>");
        return 0;
    }
    std::println("{} at {}", options->repo, options->branch.value_or("HEAD"));
}
```

Adapted from `tests/cli/unit/parse/positional_tests.cpp` and `tests/cli/unit/usage_tests.cpp`.

## Overview

| Header                                          | Contents                                                                                                                                                            |
| ----------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [cli.h](../include/kotatsu/cli/cli.h)           | The headers below but the table, and `KANNOTATE`                                                                                                                    |
| [spec.h](../include/kotatsu/cli/spec.h)         | `ArgSpec`, what a field's `cli::` entries say (`names`, `positional`, `rest`, `count`, `required`, `hidden`, `exclusive`, ...), `Help`, `Alias`                     |
| [parse.h](../include/kotatsu/cli/parse.h)       | `parse<T>`, from a `string_list_ref` or any range of strings, and `parse_into`, onto options already held, each with a visit of the arguments it reads, and `Given` |
| [commands.h](../include/kotatsu/cli/commands.h) | `Commands<Tag>`, a field of subcommands files add with `add_command`, which calling runs                                                                            |
| [error.h](../include/kotatsu/cli/error.h)       | `Error`, its `Kind`, the argv span at fault, its subcommands and a suggestion, and `render`, under a `Style`                                                        |
| [report.h](../include/kotatsu/cli/report.h)     | `report` and `print_usage`, in the style for a terminal (`style_for`)                                                                                               |
| [usage.h](../include/kotatsu/cli/usage.h)       | `usage<T>`, the help text with defaults and ranges, and `usage_entries<T>`, its lines for a program to lay out itself                                               |
| [to_argv.h](../include/kotatsu/cli/to_argv.h)   | `to_argv`: options written back as the argv that parses to them                                                                                                     |

### Option table

The option table parses an argv against a table of `Option` entries of LLVM's
kinds (flag, joined, separate, comma-joined, multi-arg, remaining args, ...),
with aliases, grouped short options (`-abc`) and visibility masks.

| Header                                                  | Contents                                                                   |
| ------------------------------------------------------- | -------------------------------------------------------------------------- |
| [table.h](../include/kotatsu/cli/table.h)               | The three headers below                                                    |
| [table/arg.h](../include/kotatsu/cli/table/arg.h)       | `OptionKind`, `RenderStyle`, `ParsedArg`, the prefix sets `pfx_*`          |
| [table/option.h](../include/kotatsu/cli/table/option.h) | `Option`, a constexpr table entry, and `OptionRef`                         |
| [table/table.h](../include/kotatsu/cli/table/table.h)   | `OptTable` (`parse`, `find_option`, `render`), `ParseOptions`, `ParseIter` |

## Platform notes

cli parses the same on every platform. `report` and `print_usage` color what they write when
the descriptor is a terminal (a console on Windows) and `NO_COLOR` is unset, and write UTF-16
to a Windows console.

## See also

[meta](meta.md), [codec](codec.md), [support](support.md), [zest](zest.md)
(its runner's flags are a cli struct), [tests/cli](../tests/cli/).
