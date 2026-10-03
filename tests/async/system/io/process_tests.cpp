#include <chrono>
#include <csignal>
#include <cstddef>
#include <fcntl.h>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "async/harness/io.h"
#include "async/harness/os.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

constexpr std::string_view by_platform([[maybe_unused]] std::string_view posix,
                                       [[maybe_unused]] std::string_view windows) {
#ifdef _WIN32
    return windows;
#else
    return posix;
#endif
}

/// Runs `command` through the platform shell, its stdio ignored.
process::options shell(std::string_view command) {
    process::options opts;
    opts.file = by_platform("/bin/sh", "cmd.exe");
    opts.args = {opts.file, std::string(by_platform("-c", "/c")), std::string(command)};
    opts.streams = {process::stdio::ignore(), process::stdio::ignore(), process::stdio::ignore()};
    return opts;
}

/// A child that prints its environment, a `NAME=VALUE` line each.
process::options environment_printer() {
    process::options opts;
#ifdef _WIN32
    opts.file = "cmd.exe";
    opts.args = {opts.file, "/c", "set"};
#else
    opts.file = "/usr/bin/env";
#endif
    opts.streams[0] = process::stdio::ignore();
    return opts;
}

/// The values `printed`, what environment_printer() printed, gives `name`.
std::vector<std::string> values_of(std::string_view printed, std::string_view name) {
    std::vector<std::string> values;
    for(auto piece: printed | std::views::split('\n')) {
        std::string_view line(piece.begin(), piece.end());
        if(line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        if(line.starts_with(name) && line.substr(name.size()).starts_with('=')) {
            values.emplace_back(line.substr(name.size() + 1));
        }
    }
    return values;
}

std::string trim_newlines(std::string text) {
    while(!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

ZEST_SUITE(async_io_process, zest::LoopFixture) {

ZEST_CASE(wait_reports_the_exit_code) {
    auto success = process::spawn(shell("exit 0"), loop);
    auto failure = process::spawn(shell("exit 3"), loop);
    ASSERT(success.has_value());
    ASSERT(failure.has_value());
    EXPECT(success->proc.pid() > 0);

    auto [succeeded, failed] = run(success->proc.wait(), failure->proc.wait());
    EXPECT(test::exit_status_of(succeeded) == 0);
    ASSERT(succeeded.has_value());
    EXPECT(succeeded->term_signal == 0);
    EXPECT(succeeded->success());
    EXPECT(test::exit_status_of(failed) == 3);
    ASSERT(failed.has_value());
    EXPECT(!failed->success());
}

// With inherited stdio the child shares the test's own streams.
ZEST_CASE(spawn_with_inherited_stdio_runs) {
    auto opts = shell("exit 0");
    opts.streams = {process::stdio::inherit(),
                    process::stdio::inherit(),
                    process::stdio::inherit()};
    auto spawned = process::spawn(opts, loop);
    ASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    EXPECT(test::exit_status_of(status) == 0);
}

ZEST_CASE(spawn_of_a_missing_file_fails) {
    process::options opts;
    opts.file = by_platform("/nonexistent/kotatsu-nope", R"(Z:\nonexistent\kotatsu-nope.exe)");

    auto spawned = process::spawn(opts, loop);
    ASSERT(spawned.has_error());
    EXPECT(spawned.error() == error::no_such_file_or_directory);
}

ZEST_CASE(stdout_pipe_carries_the_output) {
    auto opts = shell(by_platform("printf kotatsu-stdout", "echo kotatsu-stdout"));
    opts.streams[1] = process::stdio::pipe(false, true);
    auto spawned = process::spawn(opts, loop);
    ASSERT(spawned.has_value());

    auto [output, status] = run(spawned->stdout_pipe.read(), spawned->proc.wait());
    ASSERT(output.has_value());
    EXPECT(trim_newlines(*output) == "kotatsu-stdout");
    EXPECT(test::exit_status_of(status) == 0);
}

ZEST_CASE(stderr_pipe_carries_the_errors) {
    auto opts = shell(by_platform("printf kotatsu-stderr 1>&2", "echo kotatsu-stderr 1>&2"));
    opts.streams[1] = process::stdio::pipe(false, true);
    opts.streams[2] = process::stdio::pipe(false, true);
    auto spawned = process::spawn(opts, loop);
    ASSERT(spawned.has_value());

    auto [output, errors, status] =
        run(spawned->stdout_pipe.read(), spawned->stderr_pipe.read(), spawned->proc.wait());
    ASSERT(output.has_error());
    EXPECT(output.error() == error::end_of_file);
    ASSERT(errors.has_value());
    EXPECT(zest::contains(*errors, "kotatsu-stderr"));
    EXPECT(test::exit_status_of(status) == 0);
}

ZEST_CASE(stdin_pipe_feeds_the_child) {
    auto opts = test::stdin_reader();
    opts.streams[1] = process::stdio::pipe(false, true);
    auto spawned = process::spawn(opts, loop);
    ASSERT(spawned.has_value());
    auto feed = [&]() -> task<void, error> {
        co_await spawned->stdin_pipe.write(std::string_view("kotatsu-stdin\n")).or_fail();
        // Closing stdin ends the child.
        spawned->stdin_pipe = pipe{};
    };

    auto [fed, output, status] = run(feed(), spawned->stdout_pipe.read(), spawned->proc.wait());
    EXPECT(fed.has_value());
    ASSERT(output.has_value());
    EXPECT(trim_newlines(*output) == "kotatsu-stdin");
    EXPECT(test::exit_status_of(status) == 0);
}

// cmd reads a digit before > as the handle to redirect, so the redirection
// goes first.
ZEST_CASE(capture_gives_the_status_and_what_the_child_wrote) {
    auto opts = shell(
        by_platform("printf out; printf err 1>&2; exit 3", "echo out& 1>&2 echo err& exit 3"));

    auto [captured] = run(process::capture(opts, loop));
    ASSERT(captured.has_value());
    EXPECT(captured->status.status == 3);
    EXPECT(trim_newlines(captured->stdout_text) == "out");
    EXPECT(trim_newlines(captured->stderr_text) == "err");
}

#ifndef _WIN32
// The child writes more to stderr than a pipe holds, then to stdout: it ends
// only because both pipes are read while it runs. cmd has no quick way to
// write that much.
ZEST_CASE(capture_reads_both_pipes_while_the_child_runs) {
    auto opts = shell(R"(head -c 300000 /dev/zero | tr '\0' e 1>&2; )"
                      R"(head -c 300000 /dev/zero | tr '\0' o)");

    auto [captured] = run(process::capture(opts, loop));
    ASSERT(captured.has_value());
    EXPECT(captured->status.success());
    EXPECT(captured->stdout_text == std::string(300000, 'o'));
    EXPECT(captured->stderr_text == std::string(300000, 'e'));
}
#endif

ZEST_CASE(capture_of_a_missing_file_fails) {
    process::options opts;
    opts.file = by_platform("/nonexistent/kotatsu-nope", R"(Z:\nonexistent\kotatsu-nope.exe)");

    auto [captured] = run(process::capture(opts, loop));
    ASSERT(captured.has_error());
    EXPECT(captured.error() == error::no_such_file_or_directory);
}

ZEST_CASE(env_set_and_env_unset_apply_over_env) {
    auto opts = environment_printer();
    opts.env = {"KOTA_KEPT=kept", "KOTA_DROPPED=dropped", "KOTA_REPLACED=old"};
    opts.env_set = {"KOTA_REPLACED=new", "KOTA_ADDED=added"};
    opts.env_unset = {"KOTA_DROPPED"};

    auto [captured] = run(process::capture(opts, loop));
    ASSERT(captured.has_value());
    const auto& printed = captured->stdout_text;
    EXPECT(values_of(printed, "KOTA_KEPT") == std::vector<std::string>{"kept"});
    EXPECT(values_of(printed, "KOTA_REPLACED") == std::vector<std::string>{"new"});
    EXPECT(values_of(printed, "KOTA_ADDED") == std::vector<std::string>{"added"});
    EXPECT(values_of(printed, "KOTA_DROPPED").empty());
}

ZEST_CASE(env_set_and_env_unset_apply_over_the_inherited_environment) {
    test::EnvironmentVariable kept("KOTA_TEST_KEPT", "kept");
    test::EnvironmentVariable dropped("KOTA_TEST_DROPPED", "dropped");
    test::EnvironmentVariable replaced("KOTA_TEST_REPLACED", "old");
    auto opts = environment_printer();
    opts.env_set = {"KOTA_TEST_REPLACED=new", "KOTA_TEST_ADDED=added"};
    opts.env_unset = {"KOTA_TEST_DROPPED"};

    auto [captured] = run(process::capture(opts, loop));
    ASSERT(captured.has_value());
    const auto& printed = captured->stdout_text;
    EXPECT(values_of(printed, "KOTA_TEST_KEPT") == std::vector<std::string>{"kept"});
    EXPECT(values_of(printed, "KOTA_TEST_REPLACED") == std::vector<std::string>{"new"});
    EXPECT(values_of(printed, "KOTA_TEST_ADDED") == std::vector<std::string>{"added"});
    EXPECT(values_of(printed, "KOTA_TEST_DROPPED").empty());
}

// The overlay names variables in another case than the inherited ones: the
// same variables on Windows only.
ZEST_CASE(env_overlay_names_match_as_the_system_matches_them) {
    test::EnvironmentVariable dropped("KOTA_TEST_DROPPED", "dropped");
    test::EnvironmentVariable replaced("KOTA_TEST_REPLACED", "old");
    auto opts = environment_printer();
    opts.env_set = {"kota_test_replaced=new"};
    opts.env_unset = {"kota_test_dropped"};

    auto [captured] = run(process::capture(opts, loop));
    ASSERT(captured.has_value());
    const auto& printed = captured->stdout_text;
    EXPECT(values_of(printed, "kota_test_replaced") == std::vector<std::string>{"new"});
#ifdef _WIN32
    EXPECT(values_of(printed, "KOTA_TEST_REPLACED").empty());
    EXPECT(values_of(printed, "KOTA_TEST_DROPPED").empty());
#else
    EXPECT(values_of(printed, "KOTA_TEST_REPLACED") == std::vector<std::string>{"old"});
    EXPECT(values_of(printed, "KOTA_TEST_DROPPED") == std::vector<std::string>{"dropped"});
#endif
}

// An overlay that leaves no variable gives the child none, rather than the
// inherited ones; libuv adds those Windows cannot run without.
ZEST_CASE(env_overlay_that_removes_every_variable_leaves_none) {
    auto opts = environment_printer();
    opts.env = {"KOTA_ONLY=only"};
    opts.env_unset = {"KOTA_ONLY"};

    auto [captured] = run(process::capture(opts, loop));
    ASSERT(captured.has_value());
    EXPECT(values_of(captured->stdout_text, "KOTA_ONLY").empty());
#ifndef _WIN32
    EXPECT(captured->stdout_text.empty());
#endif
}

ZEST_CASE(stdout_goes_to_a_given_descriptor) {
    test::TempDir dir;
    auto path = dir.file("stdout.txt");
    auto fd = fs::sync::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT(fd.has_value());
    auto opts = shell(by_platform("printf kotatsu-fd", "echo kotatsu-fd"));
    opts.streams[1] = process::stdio::from_fd(*fd);
    auto spawned = process::spawn(opts, loop);
    EXPECT(!fs::sync::close(*fd));
    ASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    EXPECT(test::exit_status_of(status) == 0);
    EXPECT(trim_newlines(test::read_file(dir.path / "stdout.txt")) == "kotatsu-fd");
}

ZEST_CASE(environment_and_directory_reach_the_child) {
    test::TempDir dir;
    // cmd reads a digit before > as the handle to redirect, so the
    // redirection goes first.
    auto opts = shell(by_platform(R"(printf "$KOTA_TEST_VALUE" > marker.txt)",
                                  ">marker.txt echo %KOTA_TEST_VALUE%"));
    opts.env = {"KOTA_TEST_VALUE=42"};
    opts.cwd = dir.path.string();
    auto spawned = process::spawn(opts, loop);
    ASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    EXPECT(test::exit_status_of(status) == 0);
    EXPECT(trim_newlines(test::read_file(dir.path / "marker.txt")) == "42");
}

#ifndef _WIN32
// libuv on Unix takes the handle of a spawn that fails before it forks off
// the loop's list again, so the process must free it without closing it: a
// second unlink would write through the neighbour it had then, the stdin
// pipe made just before, which is gone by then. Handles made afterwards must
// still work; the sanitizer builds catch the write into the freed pipe.
ZEST_CASE(spawn_with_a_bad_descriptor_fails) {
    auto opts = shell("exit 0");
    opts.streams = {process::stdio::pipe(true, false),
                    process::stdio::from_fd(-1),
                    process::stdio::ignore()};

    auto spawned = process::spawn(opts, loop);
    ASSERT(spawned.has_error());
    EXPECT(spawned.error() == error::invalid_argument);
    auto t = timer::create(loop);
    ASSERT(!t.start(std::chrono::milliseconds(1)));
    auto [waited] = run(t.wait());
    EXPECT(waited.has_value());
}
#endif

ZEST_CASE(spawn_in_a_missing_directory_fails) {
    test::TempDir dir;
    auto opts = shell("exit 0");
    opts.cwd = dir.file("missing");

    auto spawned = process::spawn(opts, loop);
    ASSERT(spawned.has_error());
    EXPECT(spawned.error() == error::no_such_file_or_directory);
}

ZEST_CASE(detached_child_still_reports_its_exit) {
    auto opts = shell("exit 7");
    opts.creation.detached = true;
    auto spawned = process::spawn(opts, loop);
    ASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    EXPECT(test::exit_status_of(status) == 7);
}

ZEST_CASE(second_wait_while_one_is_pending_fails) {
    auto spawned = process::spawn(shell("exit 0"), loop);
    ASSERT(spawned.has_value());

    auto [first, second] = run(spawned->proc.wait(), spawned->proc.wait());
    EXPECT(test::exit_status_of(first) == 0);
    ASSERT(second.has_error());
    EXPECT(second.error() == error::resource_busy_or_locked);
}

ZEST_CASE(wait_after_the_exit_returns_the_same_status) {
    auto spawned = process::spawn(shell("exit 5"), loop);
    ASSERT(spawned.has_value());
    using waited = result<process::exit_status>;
    auto wait_twice = [&]() -> task<std::pair<waited, waited>> {
        auto first = co_await spawned->proc.wait();
        auto second = co_await spawned->proc.wait();
        co_return std::pair{std::move(first), std::move(second)};
    };

    auto [result] = run(wait_twice());
    ASSERT(result.has_value());
    auto& [first, second] = *result;
    ASSERT(first.has_value());
    ASSERT(second.has_value());
    EXPECT(first->status == 5);
    EXPECT(second->status == 5);
}

ZEST_CASE(kill_ends_a_running_child) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ASSERT(spawned.has_value());
    EXPECT(!spawned->proc.kill(SIGTERM));

    auto [status] = run(spawned->proc.wait());
    ASSERT(status.has_value());
    EXPECT(status->term_signal == SIGTERM);
}

// libuv reports the TerminateProcess it does on Windows as SIGKILL too, which
// the CRT does not name: 9 everywhere.
ZEST_CASE(kill_without_a_signal_ends_the_child_at_once) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ASSERT(spawned.has_value());
    EXPECT(!spawned->proc.kill());

    auto [status] = run(spawned->proc.wait());
    ASSERT(status.has_value());
    EXPECT(status->term_signal == 9);
    EXPECT(!status->success());
}

ZEST_CASE(kill_with_an_invalid_signal_fails) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ASSERT(spawned.has_value());
    EXPECT(spawned->proc.kill(-1) == error::invalid_argument);
    // Closing its stdin ends the child.
    spawned->stdin_pipe = pipe{};

    auto [status] = run(spawned->proc.wait());
    EXPECT(test::exit_status_of(status) == 0);
}

ZEST_CASE(kill_after_the_exit_fails) {
    auto spawned = process::spawn(shell("exit 0"), loop);
    ASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    EXPECT(test::exit_status_of(status) == 0);
    EXPECT(spawned->proc.kill(SIGTERM) == error::no_such_process);
    EXPECT(spawned->proc.kill() == error::no_such_process);
}

// Cancelling wait() only abandons the wait: the child runs on until its
// stdin closes, and exits by itself, which a later wait() reports. The child
// runs until then, so only the cancel can end the first wait.
ZEST_CASE(cancelled_wait_leaves_the_child_running) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ASSERT(spawned.has_value());
    auto race = [&]() -> task<std::size_t, error> {
        auto first = co_await or_fail(co_await when_any(spawned->proc.wait(), yield()));
        co_return first.index();
    };

    auto [cancelled] = run(race());
    ASSERT(cancelled.has_value());
    EXPECT(*cancelled == 1U);
    spawned->stdin_pipe = pipe{};
    auto [status] = run(spawned->proc.wait());
    ASSERT(status.has_value());
    EXPECT(status->status == 0);
    EXPECT(status->term_signal == 0);
}

// The child is not killed and runs on until its stdin closes.
ZEST_CASE(wait_ended_by_destroying_its_process_fails) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ASSERT(spawned.has_value());
    std::optional<process> proc = std::move(spawned->proc);
    auto destroy = [&]() -> task<> {
        proc.reset();
        spawned->stdin_pipe = pipe{};
        co_return;
    };

    auto [waited, destroyed] = run(proc->wait(), destroy());
    ASSERT(waited.has_error());
    EXPECT(waited.error() == error::operation_aborted);
}

// The destroyed process's wait is cancelled after its destruction has ended
// it, before the loop has resumed it: the cancel leaves that ending alone.
ZEST_CASE(wait_cancelled_after_its_process_is_destroyed_ends) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ASSERT(spawned.has_value());
    std::optional<process> proc = std::move(spawned->proc);
    auto destroy = [&]() -> task<> {
        proc.reset();
        spawned->stdin_pipe = pipe{};
        co_return;
    };

    auto [result] = run(test::winner(proc->wait(), destroy()));
    ASSERT(result.has_value());
    EXPECT(*result == 1U);
}

ZEST_CASE(inert_process_fails) {
    process inert;

    auto [waited] = run(inert.wait());
    ASSERT(waited.has_error());
    EXPECT(waited.error() == error::invalid_argument);
    EXPECT(inert.kill(SIGTERM) == error::invalid_argument);
    EXPECT(inert.kill() == error::invalid_argument);
    EXPECT(inert.pid() == -1);
}

#ifndef _WIN32
// The child prints its second chunk only once the parent has consumed the
// first, so the two reads cannot merge.
ZEST_CASE(stdout_chunks_arrive_as_written) {
    auto opts = shell("printf chunk-one; read line; printf chunk-two");
    opts.streams = {process::stdio::pipe(true, false),
                    process::stdio::pipe(false, true),
                    process::stdio::ignore()};
    auto spawned = process::spawn(opts, loop);
    ASSERT(spawned.has_value());
    auto read_chunks = [&]() -> task<std::pair<std::string, std::string>, error> {
        auto first = co_await spawned->stdout_pipe.read_chunk().or_fail();
        std::string one(first.data(), first.size());
        spawned->stdout_pipe.consume(first.size());
        co_await spawned->stdin_pipe.write(std::string_view("\n")).or_fail();
        auto second = co_await spawned->stdout_pipe.read_chunk().or_fail();
        std::string two(second.data(), second.size());
        spawned->stdout_pipe.consume(second.size());
        co_return std::pair{std::move(one), std::move(two)};
    };

    auto [chunks, status] = run(read_chunks(), spawned->proc.wait());
    ASSERT(chunks.has_value());
    EXPECT(chunks->first == "chunk-one");
    EXPECT(chunks->second == "chunk-two");
    EXPECT(test::exit_status_of(status) == 0);
}
#endif

};  // ZEST_SUITE(async_io_process)

}  // namespace

}  // namespace kota
