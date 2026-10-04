#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "kota/deco/option.h"
#include "kota/zest/zest.h"

namespace kota::option {

namespace {

// Each id is its option's 1-based index in `infos`.
enum OptId : std::uint32_t {
    InputId = 1,
    UnknownId,
    GroupId,
    MemberId,
    AliasId,
    EmitId,
    EmitLlvmId,
    PairId,
    RawId,
    AsInputId,
};

constexpr std::uint32_t internal_visibility = 1U << 1;

constexpr auto infos = std::array{
    Option::input(InputId),
    Option::unknown(UnknownId),
    Option::unaliased_one(pfx_none, "group", GroupId, Kind::Group, 0, "", ""),
    Option::unaliased_one(pfx_dash_double,
                          "-m",
                          MemberId,
                          Kind::Flag,
                          0,
                          "member help",
                          "",
                          GroupId),
    Option::unaliased_one(pfx_dash, "-am", AliasId, Kind::Flag, 0, "", "").alias_of(MemberId),
    Option::unaliased_one(pfx_double, "--emit=", EmitId, Kind::Joined, 1, "", "<kind>"),
    Option::unaliased_one(pfx_double, "--emit-llvm", EmitLlvmId, Kind::Flag, 0, "", "")
        .alias_of(EmitId, "llvm\0"),
    Option::unaliased_one(pfx_dash, "-p", PairId, Kind::MultiArg, 2, "", ""),
    Option::unaliased_one(pfx_dash,
                          "-r",
                          RawId,
                          Kind::Flag,
                          0,
                          "",
                          "",
                          0,
                          RenderJoined | HelpHidden),
    Option::unaliased_one(pfx_dash,
                          "-i",
                          AsInputId,
                          Kind::Separate,
                          1,
                          "",
                          "",
                          0,
                          RenderAsInput,
                          internal_visibility),
};

/// A table of one option of `kind`, beside the input and unknown options.
struct OneOption {
    std::array<Option, 3> infos;
    OptTable table;

    OneOption(Kind kind, std::uint32_t flags = 0) :
        infos{
            Option::input(1),
            Option::unknown(2),
            Option::unaliased_one(pfx_dash, "-x", 3, kind, 1, "", "", 0, flags),
        },
        table(infos) {}

    OneOption(const OneOption&) = delete;
    auto operator=(const OneOption&) -> OneOption& = delete;

    OptionRef option() const {
        return *table.option(3);
    }
};

std::string printed(const OptionRef& option) {
    std::ostringstream out;
    option.print(out, false);
    return out.str();
}

ZEST_SUITE(deco_option) {

ZEST_CASE(name_drops_the_written_prefix) {
    ZEXPECT(infos[MemberId - 1].name() == "m");
    ZEXPECT(infos[EmitId - 1].name() == "emit=");
}

ZEST_CASE(name_of_an_option_without_prefixes_is_whole) {
    ZEXPECT(infos[GroupId - 1].has_no_prefix());
    ZEXPECT(infos[GroupId - 1].name() == "group");
}

ZEST_CASE(unknown_and_input_are_unprefixed) {
    const auto unknown = Option::unknown(7);
    ZEXPECT(unknown.id == 7U);
    ZEXPECT(unknown.kind == Kind::Unknown);
    ZEXPECT(unknown.name() == "<unknown>");

    const auto input = Option::input(8);
    ZEXPECT(input.id == 8U);
    ZEXPECT(input.kind == Kind::Input);
    ZEXPECT(input.name() == "<input>");
}

ZEST_CASE(unaliased_one_fills_in_defaults) {
    const auto option = Option::unaliased_one(pfx_dash, "-v", 3, Kind::Flag, 0);
    ZEXPECT(option.alias_id == 0U);
    ZEXPECT(option.alias_args == nullptr);
    ZEXPECT(option.group_id == 0U);
    ZEXPECT(option.flags == 0U);
    ZEXPECT(option.visibility == static_cast<std::uint32_t>(DefaultVis));
    ZEXPECT(std::string_view(option.help_text) == "no help text");
    ZEXPECT(std::string_view(option.meta_var) == "<nullptr>");
}

ZEST_CASE(alias_of_records_its_target) {
    const auto& alias = infos[EmitLlvmId - 1];
    ZEXPECT(alias.alias_id == EmitId);
    ZEXPECT(std::string_view(alias.alias_args) == "llvm");
}

ZEST_CASE(ref_reads_its_option) {
    const OptTable table(infos);
    ZASSERT(table.option(MemberId).has_value());
    const auto member = *table.option(MemberId);
    ZEXPECT(member.id() == MemberId);
    ZEXPECT(member.kind() == Kind::Flag);
    ZEXPECT(member.name() == "m");
    ZEXPECT(member.prefix() == "-");
    ZEXPECT(member.prefixed_name() == "-m");
    ZEXPECT(member.help_text() == "member help");

    ZASSERT(table.option(EmitId).has_value());
    ZEXPECT(table.option(EmitId)->meta_var() == "<kind>");
    ZASSERT(table.option(PairId).has_value());
    ZEXPECT(table.option(PairId)->num_args() == 2U);
}

ZEST_CASE(ref_of_an_unprefixed_option_has_an_empty_prefix) {
    const OptTable table(infos);
    ZASSERT(table.option(GroupId).has_value());
    ZEXPECT(table.option(GroupId)->prefix().empty());
}

ZEST_CASE(group_and_alias_resolve_through_the_table) {
    const OptTable table(infos);
    ZASSERT(table.option(MemberId).has_value());
    const auto member = *table.option(MemberId);
    ZASSERT(member.group().has_value());
    ZEXPECT(member.group()->id() == GroupId);
    ZEXPECT(!member.alias().has_value());

    ZASSERT(table.option(AliasId).has_value());
    const auto alias = *table.option(AliasId);
    ZASSERT(alias.alias().has_value());
    ZEXPECT(alias.alias()->id() == MemberId);
    ZEXPECT(!alias.group().has_value());
}

ZEST_CASE(unaliased_option_is_the_target) {
    const OptTable table(infos);
    ZASSERT(table.option(AliasId).has_value());
    const auto alias = *table.option(AliasId);
    ZEXPECT(alias.unaliased_option().id() == MemberId);
    ZEXPECT(alias.render_name() == "m");

    ZASSERT(table.option(MemberId).has_value());
    ZEXPECT(table.option(MemberId)->unaliased_option().id() == MemberId);
}

ZEST_CASE(alias_args_are_the_extra_values) {
    const OptTable table(infos);
    ZASSERT(table.option(EmitLlvmId).has_value());
    ZEXPECT(std::string_view(table.option(EmitLlvmId)->alias_args()) == "llvm");
    ZASSERT(table.option(EmitId).has_value());
    ZEXPECT(table.option(EmitId)->alias_args() == nullptr);
}

ZEST_CASE(matches_itself_its_group_and_through_an_alias) {
    const OptTable table(infos);
    ZASSERT(table.option(MemberId).has_value());
    const auto member = *table.option(MemberId);
    ZEXPECT(member.matches(MemberId));
    ZEXPECT(member.matches(GroupId));
    ZEXPECT(!member.matches(EmitId));

    ZASSERT(table.option(AliasId).has_value());
    const auto alias = *table.option(AliasId);
    ZEXPECT(alias.matches(MemberId));
    ZEXPECT(alias.matches(GroupId));
    ZEXPECT(!alias.matches(AliasId));
}

ZEST_CASE(flags_and_visibility_are_bit_tests) {
    const OptTable table(infos);
    ZASSERT(table.option(RawId).has_value());
    const auto raw = *table.option(RawId);
    ZEXPECT(raw.has_flag(HelpHidden));
    ZEXPECT(raw.has_flag(RenderJoined));
    ZEXPECT(!raw.has_flag(RenderSeparate));
    ZEXPECT(raw.has_visibility_flag(DefaultVis));
    ZEXPECT(!raw.has_no_opt_as_input());

    ZASSERT(table.option(AsInputId).has_value());
    const auto as_input = *table.option(AsInputId);
    ZEXPECT(as_input.has_no_opt_as_input());
    ZEXPECT(as_input.has_visibility_flag(internal_visibility));
    ZEXPECT(!as_input.has_visibility_flag(DefaultVis));
}

ZEST_CASE(render_style_follows_the_kind) {
    const std::array<std::pair<Kind, RenderStyle>, 10> styles = {
        std::pair{Kind::Flag,                RenderStyle::Separate   },
        std::pair{Kind::Joined,              RenderStyle::Joined     },
        std::pair{Kind::Values,              RenderStyle::Separate   },
        std::pair{Kind::Separate,            RenderStyle::Separate   },
        std::pair{Kind::CommaJoined,         RenderStyle::CommaJoined},
        std::pair{Kind::MultiArg,            RenderStyle::Separate   },
        std::pair{Kind::JoinedOrSeparate,    RenderStyle::Separate   },
        std::pair{Kind::JoinedAndSeparate,   RenderStyle::Joined     },
        std::pair{Kind::RemainingArgs,       RenderStyle::Separate   },
        std::pair{Kind::RemainingArgsJoined, RenderStyle::Separate   },
    };
    for(const auto& [kind, style]: styles) {
        ZEST_CONTEXT("kind {}", static_cast<int>(kind));
        const OneOption one(kind);
        ZEXPECT(one.option().render_style() == style);
    }
}

ZEST_CASE(render_style_of_input_unknown_and_group_is_values) {
    const OptTable table(infos);
    for(const auto id: {InputId, UnknownId, GroupId}) {
        ZEST_CONTEXT("option {}", static_cast<std::uint32_t>(id));
        ZASSERT(table.option(id).has_value());
        ZEXPECT(table.option(id)->render_style() == RenderStyle::Values);
    }
}

ZEST_CASE(render_flags_override_the_kind) {
    ZEXPECT(OneOption(Kind::CommaJoined, RenderJoined).option().render_style() ==
            RenderStyle::Joined);
    ZEXPECT(OneOption(Kind::Joined, RenderSeparate).option().render_style() ==
            RenderStyle::Separate);
}

ZEST_CASE(print_shows_kind_prefixes_and_name) {
    const OptTable table(infos);
    ZASSERT(table.option(MemberId).has_value());
    ZEXPECT(printed(*table.option(MemberId)) ==
            R"(<Kind::Flag Prefixes:["-", "--"] Name:"m" Group:<Kind::Group Name:"group">>)");
    ZASSERT(table.option(AliasId).has_value());
    ZEXPECT(
        printed(*table.option(AliasId)) ==
        R"(<Kind::Flag Prefixes:["-"] Name:"am" Alias:<Kind::Flag Prefixes:["-", "--"] Name:"m" Group:<Kind::Group Name:"group">>>)");
    ZASSERT(table.option(PairId).has_value());
    ZEXPECT(printed(*table.option(PairId)) ==
            R"(<Kind::MultiArg Prefixes:["-"] Name:"p" NumArgs:2>)");
}

ZEST_CASE(print_can_end_the_line) {
    const OptTable table(infos);
    ZASSERT(table.option(InputId).has_value());
    std::ostringstream out;
    table.option(InputId)->print(out, true);
    ZEXPECT(out.str() == std::string(R"(<Kind::Input Name:"<input>">)") + "\n");
}

};  // ZEST_SUITE(deco_option)

}  // namespace

}  // namespace kota::option
