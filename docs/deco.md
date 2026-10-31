# deco

`kotatsu::deco` describes a command line as a struct: each field declared with
a deco macro (`DecoFlag`, `DecoKV`, `DecoInput`, ...) is an option, and
[meta](meta.md)'s reflection makes the option table at compile time.
`cli::Command` parses an argv into the struct and runs a handler,
`cli::SubCommander` routes subcommands, and usage and errors are rendered from
the same declarations.

Underneath is `kotatsu::option`, an option table modelled on LLVM's `OptTable`,
usable alone. deco reads argv only; config files are [codec](codec.md)'s.

## Targets

|            |                                                                                                                                                  |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| Bazel      | `@kotatsu//:deco`; the option layer alone `@kotatsu//:option`                                                                                    |
| CMake      | `kotatsu::deco`; the option layer `kotatsu::deco::option`, also named `kotatsu::option`; both under option `KOTATSU_ENABLE_DECO` (on by default) |
| Headers    | [include/kotatsu/deco/](../include/kotatsu/deco/)                                                                                                |
| Depends on | [meta](meta.md), [support](support.md); no third-party library                                                                                   |

## Example

```cpp
#include <print>
#include <string>

#include "kotatsu/deco/deco.h"

namespace deco = kotatsu::deco;

struct CloneOptions {
    deco::decl::HelpOption help;
    DecoInput(meta_var = "REPO"; help = "Repository URL";)
    <std::string> repo;
    DecoKV(names = {"-b", "--branch"}; meta_var = "BRANCH"; required = false;)
    <std::string> branch;
};

int main(int argc, char** argv) {
    auto args = deco::util::argvify(argc, argv);
    auto command = deco::cli::command<CloneOptions>("clone [OPTIONS] <REPO>");
    command.match_all([](CloneOptions options) {
        std::println("{} at {}", *options.repo, options.branch.value_or("HEAD"));
    });
    return command(args);  // 0 after -h; parse_error_exit_code (2) on a bad argv
}
```

Adapted from `tests/deco/unit/facade/runtime_git_tests.cpp`.

## Overview

| Header                                                           | What it holds                                                                                                                           |
| ---------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| [deco.h](../include/kotatsu/deco/deco.h)                         | The whole facade and the macros, in one include                                                                                         |
| [macro.h](../include/kotatsu/deco/macro.h)                       | The declaration macros alone (`DECO_CFG*`, `DecoFlag`, `DecoKV`, `DecoInput`, `DecoPack`, `DecoComma`, `DecoMulti`, the `*Alias` forms) |
| [facade/decl.h](../include/kotatsu/deco/facade/decl.h)           | What a field is declared with (`names`, `help`, `required`, `category`, ...), `decl::Category`, `decl::HelpOption`, `decl::SubCommand`  |
| [facade/runtime.h](../include/kotatsu/deco/facade/runtime.h)     | `cli::Command`, `cli::SubCommander`, `cli::parse`, `cli::Invocation`, `cli::parse_error_exit_code`, `util::argvify`                     |
| [facade/text.h](../include/kotatsu/deco/facade/text.h)           | Usage and diagnostic rendering: `cli::text`'s `CompatibleRenderer` and `ModernRenderer`                                                 |
| [facade/config.h](../include/kotatsu/deco/facade/config.h)       | The renderers' styles and the global `config::Config`                                                                                   |
| [facade/serialize.h](../include/kotatsu/deco/facade/serialize.h) | `ser::to_argv`: an options struct written back as an argv                                                                               |

### Option layer

`kotatsu::option` parses an argv against a table of `Option` entries of
LLVM's kinds (flag, joined, separate, comma-joined, multi-arg, remaining
args, ...), with aliases, grouped short options (`-abc`) and visibility masks.

| Header                                                     | What it holds                                                              |
| ---------------------------------------------------------- | -------------------------------------------------------------------------- |
| [option.h](../include/kotatsu/deco/option.h)               | The three headers below                                                    |
| [option/arg.h](../include/kotatsu/deco/option/arg.h)       | `Kind`, `RenderStyle`, `ParsedArg`, `ParseError`, the prefix sets `pfx_*`  |
| [option/option.h](../include/kotatsu/deco/option/option.h) | `Option`, a constexpr table entry, and `OptionRef`                         |
| [option/table.h](../include/kotatsu/deco/option/table.h)   | `OptTable` (`parse`, `find_option`, `render`), `ParseOptions`, `ParseIter` |

## See also

[meta](meta.md) · [support](support.md) · [zest](zest.md) (its runner's flags
are a deco struct) · `tests/deco/` for every option kind and the rendered help.
