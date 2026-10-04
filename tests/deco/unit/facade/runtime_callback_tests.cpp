#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "deco/harness/argv.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

using strings = std::vector<std::string>;

/// What the last callback saw, kept statically since a callback is a plain function.
struct Seen {
    inline static std::uint32_t index = 0;
    inline static std::uint32_t next_cursor = 0;
    inline static std::size_t argv_size = 0;
    inline static std::string value;
    inline static int calls = 0;

    Seen() {
        index = 0;
        next_cursor = 0;
        argv_size = 0;
        value.clear();
        calls = 0;
    }

    template <typename Step>
    static void record(const Step& step) {
        index = step.arg().index;
        next_cursor = step.next_cursor();
        argv_size = step.argv().size();
        ++calls;
    }
};

struct Stops {
    DecoInput(required = false; after_parsed = [](const Step& step) {
        Seen::record(step);
        Seen::value = step.value();
        return step.stop();
    };)
    <std::string> script;

    DecoKV()
    <std::string> required_after;
};

struct StopsByAction {
    DecoInput(required = false; after_parsed = Action::stop;)
    <std::string> script;

    DecoKV()
    <std::string> required_after;
};

struct RestartsOnAView {
    // Skips the two arguments after the script.
    DecoInput(required = false; after_parsed = [](const Step& step) {
        Seen::record(step);
        return step.restart(step.argv().subspan(step.next_cursor() + 2));
    };)
    <std::string> script;

    DecoKV(names = {"--skip"}; required = false;)
    <std::string> skip;

    DecoFlag(names = {"-v"}; required = false;)
    verbose;
};

struct RestartsOnItsOwnArgv {
    DecoInput(required = false; after_parsed = [](const Step& step) {
        Seen::record(step);
        return step.restart(strings{"-v"});
    };)
    <std::string> script;

    DecoFlag(names = {"-v"}; required = false;)
    verbose;
};

struct RestartsTwice {
    DecoInput(required = false; after_parsed = [](const Step& step) {
        ++Seen::calls;
        if(Seen::calls == 1) {
            return step.restart(strings{"second.cc"});
        }
        if(Seen::calls == 2) {
            return step.restart(strings{"--name", "final"});
        }
        return step.next();
    };)
    <std::string> script;

    DecoKV(names = {"--name"}; required = false;)
    <std::string> name;
};

struct EveryName {
    DecoFlag(names = {"-v", "--verbose"}; required = false; after_parsed = [](const Step& step) {
        Seen::record(step);
        return step.next();
    };)
    verbose;
};

struct PackCallback {
    DecoPack(required = false; after_parsed = [](const Step& step) {
        Seen::record(step);
        Seen::value = step.value().front();
        return step.next();
    };)
    <std::vector<std::string>> rest;
};

struct RestartsInsideAForward {
    DecoFlagAlias(names = {"--go"}; forward = {"--target", "t", "-v"};) _;

    // Goes on from the arguments after it, a view into the argv the alias forwarded to.
    DecoKV(names = {"--target"}; required = false; after_parsed = [](const Step& step) {
        return step.restart(step.argv().subspan(step.next_cursor()));
    };)
    <std::string> target;

    DecoFlag(names = {"-v"}; required = false;)
    verbose;
};

ZEST_SUITE(deco_facade_runtime_callback) {

ZEST_CASE(stop_ends_the_parse_with_what_it_has) {
    const Seen seen;
    auto argv = test::split("script.lua --required-after x");
    const auto parsed = cli::parse<Stops>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->options.script.as_optional() == std::optional<std::string>("script.lua"));
    // Not parsed, and not missing either: a stopped parse checks nothing.
    ZEXPECT(!parsed->options.required_after.has_value());
    ZEXPECT(parsed->next_cursor() == 1U);
    ZEXPECT(parsed->remaining().size() == 2U);
    ZEXPECT(Seen::calls == 1);
    ZEXPECT(Seen::value == "script.lua");
}

ZEST_CASE(step_shows_the_argument) {
    const Seen seen;
    auto argv = test::split("script.lua x y");
    ZASSERT(cli::parse<Stops>(argv).has_value());
    ZEXPECT(Seen::index == 0U);
    ZEXPECT(Seen::next_cursor == 1U);
    ZEXPECT(Seen::argv_size == 3U);
}

ZEST_CASE(action_stop_stops) {
    auto argv = test::split("script.lua");
    const auto parsed = cli::parse<StopsByAction>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(!parsed->options.required_after.has_value());
}

ZEST_CASE(restart_goes_on_from_a_view) {
    const Seen seen;
    auto argv = test::split("entry.cc --skip ignored -v");
    const auto parsed = cli::parse<RestartsOnAView>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->options.script.as_optional() == std::optional<std::string>("entry.cc"));
    ZEXPECT(!parsed->options.skip.has_value());
    ZEXPECT(parsed->options.verbose.as_optional() == std::optional(true));
    ZEXPECT(Seen::calls == 1);
    // The view is argv past the skipped arguments.
    ZASSERT(parsed->argv().size() == 1U);
    ZEXPECT(parsed->argv().data() == argv.data() + 3);
    ZEXPECT(parsed->original_argv.size() == 4U);
}

ZEST_CASE(restart_goes_on_from_an_argv_of_its_own) {
    const Seen seen;
    auto argv = test::split("entry.cc");
    const auto parsed = cli::parse<RestartsOnItsOwnArgv>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->options.verbose.as_optional() == std::optional(true));
    ZASSERT(parsed->argv().size() == 1U);
    ZEXPECT(parsed->argv()[0] == "-v");
    ZEXPECT(parsed->next_cursor() == 1U);
}

ZEST_CASE(restart_can_repeat) {
    const Seen seen;
    auto argv = test::split("first.cc");
    const auto parsed = cli::parse<RestartsTwice>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(Seen::calls == 2);
    ZEXPECT(parsed->options.script.as_optional() == std::optional<std::string>("second.cc"));
    ZEXPECT(parsed->options.name.as_optional() == std::optional<std::string>("final"));
    ZEXPECT(parsed->argv().size() == 2U);
}

ZEST_CASE(restart_inside_a_forward_keeps_its_argv) {
    auto argv = test::split("--go");
    const auto parsed = cli::parse<RestartsInsideAForward>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(parsed->options.target.as_optional() == std::optional<std::string>("t"));
    ZEXPECT(parsed->options.verbose.as_optional() == std::optional(true));
    // The argv parsing ended on is part of the forward's, kept alive with the result.
    ZASSERT(parsed->argv().size() == 1U);
    ZEXPECT(parsed->argv()[0] == "-v");
}

ZEST_CASE(callback_runs_for_every_name) {
    const Seen seen;
    auto argv = test::split("-v --verbose");
    ZASSERT(cli::parse<EveryName>(argv).has_value());
    ZEXPECT(Seen::calls == 2);
    ZEXPECT(Seen::index == 1U);
}

ZEST_CASE(callback_of_a_pack_runs) {
    const Seen seen;
    auto argv = test::split("-- make all");
    const auto parsed = cli::parse<PackCallback>(argv);
    ZASSERT(parsed.has_value());
    ZEXPECT(Seen::calls == 1);
    ZEXPECT(Seen::value == "make");
    ZEXPECT(Seen::next_cursor == 3U);
}

};  // ZEST_SUITE(deco_facade_runtime_callback)

}  // namespace

}  // namespace kota::deco
