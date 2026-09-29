// A git-like program end to end: a SubCommander routing to Commands, each with required
// options and categories of its own.

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "deco/harness/argv.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

struct CommitOptions {
    DecoFlag(names = {"-a", "--all"}; help = "Stage all modified and deleted files";
             required = false;)
    all;

    DecoKV(names = {"-m", "--message"}; meta_var = "MSG";
           help = "Use the given message as the commit message";)
    <std::string> message;
};

struct CloneOptions {
    DecoInput(meta_var = "REPO"; help = "Repository URL";)
    <std::string> repo;

    DecoKV(names = {"-b", "--branch"}; meta_var = "BRANCH";
           help = "Checkout BRANCH instead of HEAD";
           required = false;)
    <std::string> branch;
};

struct TagOptions {
    constexpr static decl::Category mode{
        .exclusive = false,
        .required = true,
        .name = "mode",
        .description = "tag operation mode",
    };

    DecoFlag(names = {"-l", "--list"}; help = "List tags"; required = false; category = mode;)
    list;
};

/// git, with what its commands saw and what went wrong.
struct Git {
    std::optional<CommitOptions> committed;
    std::optional<CloneOptions> cloned;
    std::optional<std::string> error;
    std::optional<std::string> routing_error;

    cli::Command<CommitOptions> commit = cli::command<CommitOptions>("git commit [OPTIONS]");
    cli::Command<CloneOptions> clone = cli::command<CloneOptions>("git clone [OPTIONS] <REPO>");
    cli::Command<TagOptions> tag = cli::command<TagOptions>("git tag [OPTIONS]");
    cli::SubCommander git{"git [--version] [--help] <command> [<args>]",
                          "A fast, scalable, distributed version control system"};

    Git() {
        auto report = [this](cli::ParseError err) {
            error = std::move(err.message);
        };
        commit.match_all([this](CommitOptions options) { committed = std::move(options); })
            .on_error(report);
        clone.match_all([this](CloneOptions options) { cloned = std::move(options); })
            .on_error(report);
        tag.match_all([](TagOptions) {}).on_error(report);
        git.add(decl::SubCommand{.name = "commit",
                                 .description = "Record changes to the repository"},
                commit)
            .add(decl::SubCommand{.name = "clone",
                                  .description = "Clone a repository into a new directory"},
                 clone)
            .add(decl::SubCommand{.name = "tag", .description = "Create, list or delete tags"}, tag)
            .when_err([this](cli::SubCommandError err) { routing_error = std::move(err.message); });
    }

    Git(const Git&) = delete;
    auto operator=(const Git&) -> Git& = delete;

    void operator()(std::string_view line) {
        auto argv = test::split(line);
        git(argv);
    }
};

ZEST_SUITE(deco_facade_runtime_git) {

ZEST_CASE(clone_takes_its_repository_and_branch) {
    Git git;
    git("clone https://example.com/demo.git -b main");
    EXPECT(!git.error.has_value());
    EXPECT(!git.routing_error.has_value());
    ASSERT(git.cloned.has_value());
    EXPECT(git.cloned->repo.as_optional() ==
           std::optional<std::string>("https://example.com/demo.git"));
    EXPECT(git.cloned->branch.as_optional() == std::optional<std::string>("main"));
}

ZEST_CASE(commit_without_its_message_fails) {
    Git git;
    git("commit -a");
    EXPECT(!git.committed.has_value());
    ASSERT(git.error.has_value());
    EXPECT(zest::ends_with(*git.error, "required option -m|--message <MSG> is missing"));
}

ZEST_CASE(commit_with_its_message_commits) {
    Git git;
    git("commit -a -m fix");
    ASSERT(git.committed.has_value());
    EXPECT(git.committed->all.as_optional() == std::optional(true));
    EXPECT(git.committed->message.as_optional() == std::optional<std::string>("fix"));
}

ZEST_CASE(tag_without_a_mode_fails) {
    Git git;
    git("tag");
    ASSERT(git.error.has_value());
    EXPECT(zest::ends_with(*git.error, "required <mode> (tag operation mode) is missing"));
}

ZEST_CASE(unknown_command_fails) {
    Git git;
    git("cherry-pick main");
    ASSERT(git.routing_error.has_value());
    EXPECT(zest::ends_with(*git.routing_error, "unknown subcommand 'cherry-pick'"));
    EXPECT(!git.error.has_value());
}

ZEST_CASE(usage_lists_the_commands_without_a_usage_line) {
    Git git;
    std::ostringstream usage;
    git.git.usage(usage);
    EXPECT_SNAPSHOT(usage.str());
}

ZEST_CASE(command_usage_lists_its_options) {
    Git git;
    std::ostringstream usage;
    git.clone.usage(usage);
    EXPECT_SNAPSHOT(usage.str());
}

};  // ZEST_SUITE(deco_facade_runtime_git)

}  // namespace

}  // namespace kota::deco
