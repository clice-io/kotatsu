#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "deco/harness/argv.h"
#include "kota/deco/option.h"

namespace kota::test {

/// What parsing an argv with an option table yields: each argument, and each error with how
/// many arguments came before it. The arguments view `argv`, which the parse owns; move it,
/// never copy it.
struct TableParse {
    std::vector<std::string> argv;
    std::vector<option::ParsedArg> args;

    struct Error {
        std::size_t after;
        option::ParseError error;
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
const inline option::OptTable& kinds_table() {
    using option::Kind;
    using option::Option;
    constexpr static auto infos = std::array{
        Option::input(InputId),
        Option::unknown(UnknownId),
        Option::unaliased_one(option::pfx_dash, "-f", FlagId, Kind::Flag, 0),
        Option::unaliased_one(option::pfx_dash, "-jx", JxId, Kind::Flag, 0),
        Option::unaliased_one(option::pfx_dash, "-j", JoinedId, Kind::Joined, 1),
        Option::unaliased_one(option::pfx_dash, "-s", SeparateId, Kind::Separate, 1),
        Option::unaliased_one(option::pfx_dash, "-Wl,", CommaJoinedId, Kind::CommaJoined, 1),
        Option::unaliased_one(option::pfx_double, "--pair", MultiArgId, Kind::MultiArg, 2),
        Option::unaliased_one(option::pfx_dash,
                              "-o",
                              JoinedOrSeparateId,
                              Kind::JoinedOrSeparate,
                              1),
        Option::unaliased_one(option::pfx_dash,
                              "-x",
                              JoinedAndSeparateId,
                              Kind::JoinedAndSeparate,
                              2),
        Option::unaliased_one(option::pfx_double, "--rest", RemainingId, Kind::RemainingArgs, 0),
        Option::unaliased_one(option::pfx_double,
                              "--tail",
                              RemainingJoinedId,
                              Kind::RemainingArgsJoined,
                              0),
        Option::unaliased_one(option::pfx_double, "--help", HelpId, Kind::Flag, 0),
        Option::unaliased_one(option::pfx_dash, "-h", HelpAliasId, Kind::Flag, 0).alias_of(HelpId),
        Option::unaliased_one(option::pfx_double, "--emit=", EmitId, Kind::Joined, 1),
        Option::unaliased_one(option::pfx_double, "--emit-llvm", EmitLlvmId, Kind::Flag, 0)
            .alias_of(EmitId, "llvm\0"),
        Option::unaliased_one(option::pfx_double, "--emit-default", EmitDefaultId, Kind::Flag, 0)
            .alias_of(EmitId),
        Option::unaliased_one(option::pfx_double, "--trap=", TrapId, Kind::CommaJoined, 1),
        Option::unaliased_one(option::pfx_double, "--trap-defaults", TrapDefaultsId, Kind::Flag, 0)
            .alias_of(TrapId, "all\0undefined\0"),
        Option::unaliased_one(option::pfx_slash_dash, "/Fo", SlashId, Kind::Joined, 1),
        Option::unaliased_one(option::pfx_dash,
                              "-I",
                              RenderJoinedId,
                              Kind::Separate,
                              1,
                              "",
                              "",
                              0,
                              option::RenderJoined),
        Option::unaliased_one(option::pfx_dash,
                              "-D",
                              RenderAsInputId,
                              Kind::Separate,
                              1,
                              "",
                              "",
                              0,
                              option::RenderAsInput),
        Option::unaliased_one(option::pfx_dash,
                              "-L",
                              RenderSeparateId,
                              Kind::Joined,
                              1,
                              "",
                              "",
                              0,
                              option::RenderSeparate),
    };
    const static option::OptTable table(infos);
    return table;
}

/// Parses `argv` with `table` to the end.
inline TableParse parse_table(const option::OptTable& table,
                              std::vector<std::string> argv,
                              option::ParseOptions options = {}) {
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
inline TableParse parse_table(const option::OptTable& table,
                              std::string_view line,
                              option::ParseOptions options = {}) {
    return parse_table(table, split(line), options);
}

}  // namespace kota::test
