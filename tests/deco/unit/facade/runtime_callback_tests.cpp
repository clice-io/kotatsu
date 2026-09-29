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

/// What the last callback saw, kept per thread since a callback is a plain function.
struct Seen {
    inline thread_local static std::uint32_t index = 0;
    inline thread_local static std::uint32_t next_cursor = 0;
    inline thread_local static std::size_t argv_size = 0;
    inline thread_local static std::string value;
    inline thread_local static int calls = 0;

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
    ASSERT(parsed.has_value());
    EXPECT(parsed->options.script.as_optional() == std::optional<std::string>("script.lua"));
    // Not parsed, and not missing either: a stopped parse checks nothing.
    EXPECT(!parsed->options.required_after.has_value());
    EXPECT(parsed->next_cursor() == 1U);
    EXPECT(parsed->remaining().size() == 2U);
    EXPECT(Seen::calls == 1);
    EXPECT(Seen::value == "script.lua");
}

ZEST_CASE(step_shows_the_argument) {
    const Seen seen;
    auto argv = test::split("script.lua x y");
    ASSERT(cli::parse<Stops>(argv).has_value());
    EXPECT(Seen::index == 0U);
    EXPECT(Seen::next_cursor == 1U);
    EXPECT(Seen::argv_size == 3U);
}

ZEST_CASE(action_stop_stops) {
    auto argv = test::split("script.lua");
    const auto parsed = cli::parse<StopsByAction>(argv);
    ASSERT(parsed.has_value());
    EXPECT(!parsed->options.required_after.has_value());
}

ZEST_CASE(restart_goes_on_from_a_view) {
    const Seen seen;
    auto argv = test::split("entry.cc --skip ignored -v");
    const auto parsed = cli::parse<RestartsOnAView>(argv);
    ASSERT(parsed.has_value());
    EXPECT(parsed->options.script.as_optional() == std::optional<std::string>("entry.cc"));
    EXPECT(!parsed->options.skip.has_value());
    EXPECT(parsed->options.verbose.as_optional() == std::optional(true));
    EXPECT(Seen::calls == 1);
    // The view is argv past the skipped arguments.
    ASSERT(parsed->argv().size() == 1U);
    EXPECT(parsed->argv().data() == argv.data() + 3);
    EXPECT(parsed->original_argv.size() == 4U);
}

ZEST_CASE(restart_goes_on_from_an_argv_of_its_own) {
    const Seen seen;
    auto argv = test::split("entry.cc");
    const auto parsed = cli::parse<RestartsOnItsOwnArgv>(argv);
    ASSERT(parsed.has_value());
    EXPECT(parsed->options.verbose.as_optional() == std::optional(true));
    EXPECT(parsed->argv().size() == 1U);
    EXPECT(parsed->argv()[0] == "-v");
    EXPECT(parsed->next_cursor() == 1U);
}

ZEST_CASE(restart_can_repeat) {
    const Seen seen;
    auto argv = test::split("first.cc");
    const auto parsed = cli::parse<RestartsTwice>(argv);
    ASSERT(parsed.has_value());
    EXPECT(Seen::calls == 2);
    EXPECT(parsed->options.script.as_optional() == std::optional<std::string>("second.cc"));
    EXPECT(parsed->options.name.as_optional() == std::optional<std::string>("final"));
    EXPECT(parsed->argv().size() == 2U);
}

ZEST_CASE(restart_inside_a_forward_keeps_its_argv) {
    auto argv = test::split("--go");
    const auto parsed = cli::parse<RestartsInsideAForward>(argv);
    ASSERT(parsed.has_value());
    EXPECT(parsed->options.target.as_optional() == std::optional<std::string>("t"));
    EXPECT(parsed->options.verbose.as_optional() == std::optional(true));
    // The argv parsing ended on is part of the forward's, kept alive with the result.
    ASSERT(parsed->argv().size() == 1U);
    EXPECT(parsed->argv()[0] == "-v");
}

ZEST_CASE(callback_runs_for_every_name) {
    const Seen seen;
    auto argv = test::split("-v --verbose");
    ASSERT(cli::parse<EveryName>(argv).has_value());
    EXPECT(Seen::calls == 2);
    EXPECT(Seen::index == 1U);
}

ZEST_CASE(callback_of_a_pack_runs) {
    const Seen seen;
    auto argv = test::split("-- make all");
    const auto parsed = cli::parse<PackCallback>(argv);
    ASSERT(parsed.has_value());
    EXPECT(Seen::calls == 1);
    EXPECT(Seen::value == "make");
    EXPECT(Seen::next_cursor == 3U);
}

};  // ZEST_SUITE(deco_facade_runtime_callback)

}  // namespace

}  // namespace kota::deco
