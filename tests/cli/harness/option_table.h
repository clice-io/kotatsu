#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cli/harness/argv.h"
#include "kotatsu/cli/table.h"

namespace kotatsu::test {

/// What parsing an argv with an option table yields: each argument, and each error with how
/// many arguments came before it. The arguments view `argv`, which the parse owns; move it,
/// never copy it.
struct TableParse {
    std::vector<std::string> argv;
    std::vector<cli::ParsedArg> args;

    struct Error {
        std::size_t after;
        cli::Error error;
    };

    std::vector<Error> errors;

    /// The values of argument `i`, as strings.
    std::vector<std::string> values(std::size_t i) const {
        return std::vector<std::string>(args[i].values.begin(), args[i].values.end());
    }
};

/// The ids of kinds_table(), each its option's 1-based index.
enum KindsId : std::uint32_t {
    InputId = 1,
    UnknownId,
    FlagId,               // -f
    JxId,                 // -jx, a flag spelled like -j and more
    JoinedId,             // -j
    SeparateId,           // -s
    CommaJoinedId,        // -Wl,
    MultiArgId,           // --pair, two values
    JoinedOrSeparateId,   // -o
    JoinedAndSeparateId,  // -x
    RemainingId,          // --rest
    RemainingJoinedId,    // --tail
    HelpId,               // --help
    HelpAliasId,          // -h, an alias of --help
    EmitId,               // --emit=, joined
    EmitLlvmId,           // --emit-llvm, an alias of --emit= that adds "llvm"
    EmitDefaultId,        // --emit-default, an alias of --emit= that adds nothing
    TrapId,               // --trap=, comma joined
    TrapDefaultsId,       // --trap-defaults, an alias of --trap= that adds "all", "undefined"
    SlashId,              // /Fo, joined, also written -Fo
    RenderJoinedId,       // -I, separate but rendered joined
    RenderAsInputId,      // -D, separate but rendered as its value alone
    RenderSeparateId,     // -L, joined but rendered separate
};

/// A table with an option of each kind that takes part in parsing, and aliases with and
/// without values of their own.
const inline cli::OptTable& kinds_table() {
    using cli::OptionKind;
    using cli::Option;
    constexpr static auto infos = std::array{
        Option::input(InputId),
        Option::unknown(UnknownId),
        Option::unaliased_one(cli::pfx_dash, "-f", FlagId, OptionKind::Flag, 0),
        Option::unaliased_one(cli::pfx_dash, "-jx", JxId, OptionKind::Flag, 0),
        Option::unaliased_one(cli::pfx_dash, "-j", JoinedId, OptionKind::Joined, 1),
        Option::unaliased_one(cli::pfx_dash, "-s", SeparateId, OptionKind::Separate, 1),
        Option::unaliased_one(cli::pfx_dash, "-Wl,", CommaJoinedId, OptionKind::CommaJoined, 1),
        Option::unaliased_one(cli::pfx_double, "--pair", MultiArgId, OptionKind::MultiArg, 2),
        Option::unaliased_one(cli::pfx_dash,
                              "-o",
                              JoinedOrSeparateId,
                              OptionKind::JoinedOrSeparate,
                              1),
        Option::unaliased_one(cli::pfx_dash,
                              "-x",
                              JoinedAndSeparateId,
                              OptionKind::JoinedAndSeparate,
                              2),
        Option::unaliased_one(cli::pfx_double, "--rest", RemainingId, OptionKind::RemainingArgs, 0),
        Option::unaliased_one(cli::pfx_double,
                              "--tail",
                              RemainingJoinedId,
                              OptionKind::RemainingArgsJoined,
                              0),
        Option::unaliased_one(cli::pfx_double, "--help", HelpId, OptionKind::Flag, 0),
        Option::unaliased_one(cli::pfx_dash, "-h", HelpAliasId, OptionKind::Flag, 0)
            .alias_of(HelpId),
        Option::unaliased_one(cli::pfx_double, "--emit=", EmitId, OptionKind::Joined, 1),
        Option::unaliased_one(cli::pfx_double, "--emit-llvm", EmitLlvmId, OptionKind::Flag, 0)
            .alias_of(EmitId, "llvm\0"),
        Option::unaliased_one(cli::pfx_double, "--emit-default", EmitDefaultId, OptionKind::Flag, 0)
            .alias_of(EmitId),
        Option::unaliased_one(cli::pfx_double, "--trap=", TrapId, OptionKind::CommaJoined, 1),
        Option::unaliased_one(cli::pfx_double,
                              "--trap-defaults",
                              TrapDefaultsId,
                              OptionKind::Flag,
                              0)
            .alias_of(TrapId, "all\0undefined\0"),
        Option::unaliased_one(cli::pfx_slash_dash, "/Fo", SlashId, OptionKind::Joined, 1),
        Option::unaliased_one(cli::pfx_dash,
                              "-I",
                              RenderJoinedId,
                              OptionKind::Separate,
                              1,
                              "",
                              "",
                              0,
                              cli::RenderJoined),
        Option::unaliased_one(cli::pfx_dash,
                              "-D",
                              RenderAsInputId,
                              OptionKind::Separate,
                              1,
                              "",
                              "",
                              0,
                              cli::RenderAsInput),
        Option::unaliased_one(cli::pfx_dash,
                              "-L",
                              RenderSeparateId,
                              OptionKind::Joined,
                              1,
                              "",
                              "",
                              0,
                              cli::RenderSeparate),
    };
    const static cli::OptTable table(infos);
    return table;
}

/// Parses `argv` with `table` to the end.
inline TableParse parse_table(const cli::OptTable& table,
                              std::vector<std::string> argv,
                              cli::ParseOptions options = {}) {
    TableParse parse{.argv = std::move(argv), .args = {}, .errors = {}};
    for(const auto& result: table.parse(parse.argv, options)) {
        if(result.has_value()) {
            parse.args.push_back(*result);
        } else {
            parse.errors.push_back({.after = parse.args.size(), .error = result.error()});
        }
    }
    return parse;
}

/// Parses the argv `line` writes, split at single spaces.
inline TableParse parse_table(const cli::OptTable& table,
                              std::string_view line,
                              cli::ParseOptions options = {}) {
    return parse_table(table, split(line), options);
}

}  // namespace kotatsu::test
