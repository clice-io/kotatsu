#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <fcntl.h>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#ifndef _WIN32
#include <sys/stat.h>
#endif

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

std::string trim_newlines(std::string text) {
    while(!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

/// What `opts`, a shell command that writes to marker.txt, writes there when
/// run in `dir`, or how it ended when it failed.
task<std::string> marker_of(process::options opts, const test::TempDir& dir, event_loop& loop) {
    opts.cwd = dir.path.string();
    auto spawned = process::spawn(opts, loop);
    if(!spawned) {
        co_return std::format("spawn failed: {}", spawned.error().message());
    }
    auto status = co_await spawned->proc.wait();
    if(!status) {
        co_return std::format("wait failed: {}", status.error().message());
    }
    if(!status->success()) {
        co_return status->to_string();
    }
    co_return trim_newlines(test::read_file(dir.path / "marker.txt"));
}

/// A shell command that writes variables `first` and `second` to marker.txt,
/// joined by '|'. One that is not set reads as unset(name).
process::options print_two(std::string_view first, std::string_view second) {
    return shell(
        by_platform(std::format(R"(printf "%s|%s" "${{{}-unset}}" "${{{}-unset}}" > marker.txt)",
                                first,
                                second),
                    std::format(">marker.txt echo %{}%^|%{}%", first, second)));
}

/// What print_two() writes for a variable `name` that is not set: cmd leaves
/// the reference as it was written.
std::string unset(std::string_view name) {
    return std::string(by_platform("unset", std::format("%{}%", name)));
}

ZEST_SUITE(async_io_process, zest::LoopFixture) {

ZEST_CASE(wait_reports_the_exit_code) {
    auto success = process::spawn(shell("exit 0"), loop);
    auto failure = process::spawn(shell("exit 3"), loop);
    ZASSERT(success.has_value());
    ZASSERT(failure.has_value());
    ZEXPECT(success->proc.pid() > 0);

    auto [succeeded, failed] = run(success->proc.wait(), failure->proc.wait());
    ZEXPECT(test::exit_status_of(succeeded) == 0);
    ZASSERT(succeeded.has_value());
    ZEXPECT(succeeded->term_signal == 0);
    ZEXPECT(succeeded->success());
    ZEXPECT(test::exit_status_of(failed) == 3);
    ZASSERT(failed.has_value());
    ZEXPECT(!failed->success());
}

// With inherited stdio the child shares the test's own streams.
ZEST_CASE(spawn_with_inherited_stdio_runs) {
    auto opts = shell("exit 0");
    opts.streams = {process::stdio::inherit(),
                    process::stdio::inherit(),
                    process::stdio::inherit()};
    auto spawned = process::spawn(opts, loop);
    ZASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    ZEXPECT(test::exit_status_of(status) == 0);
}

ZEST_CASE(spawn_of_a_missing_file_fails) {
    process::options opts;
    opts.file = by_platform("/nonexistent/kotatsu-nope", R"(Z:\nonexistent\kotatsu-nope.exe)");

    auto spawned = process::spawn(opts, loop);
    ZASSERT(spawned.has_error());
    ZEXPECT(spawned.error() == error::no_such_file_or_directory);
}

ZEST_CASE(stdout_pipe_carries_the_output) {
    auto opts = shell(by_platform("printf kotatsu-stdout", "echo kotatsu-stdout"));
    opts.streams[1] = process::stdio::pipe(false, true);
    auto spawned = process::spawn(opts, loop);
    ZASSERT(spawned.has_value());

    auto [output, status] = run(spawned->stdout_pipe.read(), spawned->proc.wait());
    ZASSERT(output.has_value());
    ZEXPECT(trim_newlines(*output) == "kotatsu-stdout");
    ZEXPECT(test::exit_status_of(status) == 0);
}

ZEST_CASE(stderr_pipe_carries_the_errors) {
    auto opts = shell(by_platform("printf kotatsu-stderr 1>&2", "echo kotatsu-stderr 1>&2"));
    opts.streams[1] = process::stdio::pipe(false, true);
    opts.streams[2] = process::stdio::pipe(false, true);
    auto spawned = process::spawn(opts, loop);
    ZASSERT(spawned.has_value());

    auto [output, errors, status] =
        run(spawned->stdout_pipe.read(), spawned->stderr_pipe.read(), spawned->proc.wait());
    ZASSERT(output.has_error());
    ZEXPECT(output.error() == error::end_of_file);
    ZASSERT(errors.has_value());
    ZEXPECT(zest::contains(*errors, "kotatsu-stderr"));
    ZEXPECT(test::exit_status_of(status) == 0);
}

ZEST_CASE(stdin_pipe_feeds_the_child) {
    auto opts = test::stdin_reader();
    opts.streams[1] = process::stdio::pipe(false, true);
    auto spawned = process::spawn(opts, loop);
    ZASSERT(spawned.has_value());
    auto feed = [&]() -> task<void, error> {
        co_await spawned->stdin_pipe.write(std::string_view("kotatsu-stdin\n")).or_fail();
        // Closing stdin ends the child.
        spawned->stdin_pipe = pipe{};
    };

    auto [fed, output, status] = run(feed(), spawned->stdout_pipe.read(), spawned->proc.wait());
    ZEXPECT(fed.has_value());
    ZASSERT(output.has_value());
    ZEXPECT(trim_newlines(*output) == "kotatsu-stdin");
    ZEXPECT(test::exit_status_of(status) == 0);
}

ZEST_CASE(stdout_goes_to_a_given_descriptor) {
    test::TempDir dir;
    auto path = dir.file("stdout.txt");
    auto fd = fs::sync::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ZASSERT(fd.has_value());
    auto opts = shell(by_platform("printf kotatsu-fd", "echo kotatsu-fd"));
    opts.streams[1] = process::stdio::from_fd(*fd);
    auto spawned = process::spawn(opts, loop);
    ZEXPECT(!fs::sync::close(*fd));
    ZASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    ZEXPECT(test::exit_status_of(status) == 0);
    ZEXPECT(trim_newlines(test::read_file(dir.path / "stdout.txt")) == "kotatsu-fd");
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
    ZASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    ZEXPECT(test::exit_status_of(status) == 0);
    ZEXPECT(trim_newlines(test::read_file(dir.path / "marker.txt")) == "42");
}

// The changes go over the inherited environment: the child still has what
// this process has.
ZEST_CASE(env_changes_go_over_the_inherited_environment) {
    test::TempDir dir;
    test::ScopedVariable inherited("KOTA_TEST_INHERITED", "inherited");
    auto opts = print_two("KOTA_TEST_SET", "KOTA_TEST_INHERITED");
    opts.env_changes = {
        {.name = "KOTA_TEST_SET", .value = "set"}
    };

    auto [written] = run(marker_of(opts, dir, loop));
    ZASSERT(written.has_value());
    ZEXPECT(*written == "set|inherited");
}

ZEST_CASE(env_changes_go_over_a_given_environment) {
    test::TempDir dir;
    test::ScopedVariable inherited("KOTA_TEST_INHERITED", "inherited");
    auto opts = print_two("KOTA_TEST_GIVEN", "KOTA_TEST_INHERITED");
    opts.env = {"KOTA_TEST_GIVEN=given"};
    opts.env_changes = {
        {.name = "KOTA_TEST_SET", .value = "set"}
    };

    auto [written] = run(marker_of(opts, dir, loop));
    ZASSERT(written.has_value());
    ZEXPECT(*written == "given|" + unset("KOTA_TEST_INHERITED"));
}

ZEST_CASE(last_env_change_of_a_name_counts) {
    test::TempDir dir;
    auto opts = print_two("KOTA_TEST_SET", "KOTA_TEST_REMOVED");
    opts.env = {"KOTA_TEST_REMOVED=given"};
    opts.env_changes = {
        {.name = "KOTA_TEST_SET",     .value = "first"     },
        {.name = "KOTA_TEST_REMOVED", .value = std::nullopt},
        {.name = "KOTA_TEST_SET",     .value = "last"      },
    };

    auto [written] = run(marker_of(opts, dir, loop));
    ZASSERT(written.has_value());
    ZEXPECT(*written == "last|" + unset("KOTA_TEST_REMOVED"));
}

// Changes that remove every variable leave the child an empty environment,
// not the inherited one.
ZEST_CASE(env_changes_removing_every_variable_inherit_nothing) {
    test::TempDir dir;
    test::ScopedVariable inherited("KOTA_TEST_INHERITED", "inherited");
    auto opts = print_two("KOTA_TEST_GIVEN", "KOTA_TEST_INHERITED");
    opts.env = {"KOTA_TEST_GIVEN=given"};
    opts.env_changes = {
        {.name = "KOTA_TEST_GIVEN", .value = std::nullopt}
    };

    auto [written] = run(marker_of(opts, dir, loop));
    ZASSERT(written.has_value());
    ZEXPECT(*written == unset("KOTA_TEST_GIVEN") + "|" + unset("KOTA_TEST_INHERITED"));
}

#ifdef _WIN32
ZEST_CASE(env_changes_match_names_without_regard_to_case) {
    test::TempDir dir;
    auto opts = print_two("KOTA_TEST_SET", "KOTA_TEST_GIVEN");
    opts.env = {"kota_test_given=given"};
    opts.env_changes = {
        {.name = "kota_test_set",   .value = "lower"     },
        {.name = "KOTA_TEST_SET",   .value = "upper"     },
        {.name = "KOTA_TEST_GIVEN", .value = std::nullopt},
    };
    auto [written] = run(marker_of(opts, dir, loop));
    ZASSERT(written.has_value());
    ZEXPECT(*written == "upper|" + unset("KOTA_TEST_GIVEN"));
}
#endif

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
    ZASSERT(spawned.has_error());
    ZEXPECT(spawned.error() == error::invalid_argument);
    auto t = timer::create(loop);
    ZASSERT(!t.start(std::chrono::milliseconds(1)));
    auto [waited] = run(t.wait());
    ZEXPECT(waited.has_value());
}
#endif

ZEST_CASE(spawn_in_a_missing_directory_fails) {
    test::TempDir dir;
    auto opts = shell("exit 0");
    opts.cwd = dir.file("missing");

    auto spawned = process::spawn(opts, loop);
    ZASSERT(spawned.has_error());
    ZEXPECT(spawned.error() == error::no_such_file_or_directory);
}

ZEST_CASE(detached_child_still_reports_its_exit) {
    auto opts = shell("exit 7");
    opts.creation.detached = true;
    auto spawned = process::spawn(opts, loop);
    ZASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    ZEXPECT(test::exit_status_of(status) == 7);
}

ZEST_CASE(second_wait_while_one_is_pending_fails) {
    auto spawned = process::spawn(shell("exit 0"), loop);
    ZASSERT(spawned.has_value());

    auto [first, second] = run(spawned->proc.wait(), spawned->proc.wait());
    ZEXPECT(test::exit_status_of(first) == 0);
    ZASSERT(second.has_error());
    ZEXPECT(second.error() == error::resource_busy_or_locked);
}

ZEST_CASE(wait_after_the_exit_returns_the_same_status) {
    auto spawned = process::spawn(shell("exit 5"), loop);
    ZASSERT(spawned.has_value());
    using waited = result<process::exit_status>;
    auto wait_twice = [&]() -> task<std::pair<waited, waited>> {
        auto first = co_await spawned->proc.wait();
        auto second = co_await spawned->proc.wait();
        co_return std::pair{std::move(first), std::move(second)};
    };

    auto [result] = run(wait_twice());
    ZASSERT(result.has_value());
    auto& [first, second] = *result;
    ZASSERT(first.has_value());
    ZASSERT(second.has_value());
    ZEXPECT(first->status == 5);
    ZEXPECT(second->status == 5);
}

ZEST_CASE(kill_ends_a_running_child) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ZASSERT(spawned.has_value());
    ZEXPECT(!spawned->proc.kill(SIGTERM));

    auto [status] = run(spawned->proc.wait());
    ZASSERT(status.has_value());
    ZEXPECT(status->term_signal == SIGTERM);
}

ZEST_CASE(kill_without_a_signal_ends_a_running_child) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ZASSERT(spawned.has_value());
    ZEXPECT(!spawned->proc.kill());

    auto [status] = run(spawned->proc.wait());
    ZASSERT(status.has_value());
    ZEXPECT(!status->success());
    ZEXPECT(status->to_string() == "signal 9 (SIGKILL)");
}

ZEST_CASE(kill_with_an_invalid_signal_fails) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ZASSERT(spawned.has_value());
    ZEXPECT(spawned->proc.kill(-1) == error::invalid_argument);
    // Closing its stdin ends the child.
    spawned->stdin_pipe = pipe{};

    auto [status] = run(spawned->proc.wait());
    ZEXPECT(test::exit_status_of(status) == 0);
}

ZEST_CASE(kill_after_the_exit_fails) {
    auto spawned = process::spawn(shell("exit 0"), loop);
    ZASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    ZEXPECT(test::exit_status_of(status) == 0);
    ZEXPECT(spawned->proc.kill(SIGTERM) == error::no_such_process);
}

// Cancelling wait() only abandons the wait: the child runs on until its
// stdin closes, and exits by itself, which a later wait() reports. The child
// runs until then, so only the cancel can end the first wait.
ZEST_CASE(cancelled_wait_leaves_the_child_running) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ZASSERT(spawned.has_value());
    auto race = [&]() -> task<std::size_t, error> {
        auto first = co_await or_fail(co_await when_any(spawned->proc.wait(), yield()));
        co_return first.index();
    };

    auto [cancelled] = run(race());
    ZASSERT(cancelled.has_value());
    ZEXPECT(*cancelled == 1U);
    spawned->stdin_pipe = pipe{};
    auto [status] = run(spawned->proc.wait());
    ZASSERT(status.has_value());
    ZEXPECT(status->status == 0);
    ZEXPECT(status->term_signal == 0);
}

// The child is not killed and runs on until its stdin closes.
ZEST_CASE(wait_ended_by_destroying_its_process_fails) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ZASSERT(spawned.has_value());
    std::optional<process> proc = std::move(spawned->proc);
    auto destroy = [&]() -> task<> {
        proc.reset();
        spawned->stdin_pipe = pipe{};
        co_return;
    };

    auto [waited, destroyed] = run(proc->wait(), destroy());
    ZASSERT(waited.has_error());
    ZEXPECT(waited.error() == error::operation_aborted);
}

// The destroyed process's wait is cancelled after its destruction has ended
// it, before the loop has resumed it: the cancel leaves that ending alone.
ZEST_CASE(wait_cancelled_after_its_process_is_destroyed_ends) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ZASSERT(spawned.has_value());
    std::optional<process> proc = std::move(spawned->proc);
    auto destroy = [&]() -> task<> {
        proc.reset();
        spawned->stdin_pipe = pipe{};
        co_return;
    };

    auto [result] = run(test::winner(proc->wait(), destroy()));
    ZASSERT(result.has_value());
    ZEXPECT(*result == 1U);
}

ZEST_CASE(inert_process_fails) {
    process inert;

    auto [waited] = run(inert.wait());
    ZASSERT(waited.has_error());
    ZEXPECT(waited.error() == error::invalid_argument);
    ZEXPECT(inert.kill(SIGTERM) == error::invalid_argument);
    ZEXPECT(inert.pid() == -1);
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
    ZASSERT(spawned.has_value());
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
    ZASSERT(chunks.has_value());
    ZEXPECT(chunks->first == "chunk-one");
    ZEXPECT(chunks->second == "chunk-two");
    ZEXPECT(test::exit_status_of(status) == 0);
}
#endif

ZEST_CASE(capture_returns_what_the_child_wrote_and_how_it_ended) {
    auto opts = shell(
        by_platform("printf out; printf err 1>&2; exit 3", "echo out& echo err 1>&2& exit /b 3"));

    auto [captured] = run(process::capture(opts, loop));
    ZASSERT(captured.has_value());
    ZEXPECT(trim_newlines(captured->stdout_data) == "out");
    ZEXPECT(zest::contains(captured->stderr_data, "err"));
    ZEXPECT(captured->status.status == 3);
}

// A child that reads its stdin to the end exits at once: there is nothing
// to read.
ZEST_CASE(capture_gives_the_child_no_input) {
    auto [captured] = run(process::capture(test::stdin_reader(), loop));
    ZASSERT(captured.has_value());
    ZEXPECT(captured->status.success());
    ZEXPECT(trim_newlines(captured->stdout_data).empty());
}

ZEST_CASE(capture_of_a_missing_file_fails) {
    process::options opts;
    opts.file = by_platform("/nonexistent/kotatsu-nope", R"(Z:\nonexistent\kotatsu-nope.exe)");

    auto [captured] = run(process::capture(opts, loop));
    ZASSERT(captured.has_error());
    ZEXPECT(captured.error() == error::no_such_file_or_directory);
}

#ifndef _WIN32
// The child fills stderr before it writes to stdout: reading one pipe after
// the other, the capture would wait on it for good. The command is POSIX.
ZEST_CASE(capture_reads_both_pipes_while_the_child_runs) {
    auto opts = shell("head -c 1048576 /dev/zero >&2; head -c 1048576 /dev/zero");

    auto [captured] = run(process::capture(opts, loop));
    ZASSERT(captured.has_value());
    ZEXPECT(captured->status.success());
    ZEXPECT(captured->stdout_data.size() == 1048576U);
    ZEXPECT(captured->stderr_data.size() == 1048576U);
}

// The child writes its pid to one FIFO, then reads another that never ends,
// so only a kill ends it. Once the pid is read the capture is cancelled, and
// it ends only once the child is gone: no process has that pid any more.
// Both FIFOs are opened for writing too, so that neither reads as ended.
// mkfifo is POSIX.
ZEST_CASE(cancelled_capture_kills_the_child) {
    test::TempDir dir;
    ZASSERT(::mkfifo(dir.file("pid").c_str(), 0600) == 0);
    ZASSERT(::mkfifo(dir.file("hold").c_str(), 0600) == 0);
    auto pid_fifo = fs::sync::open(dir.file("pid"), O_RDWR, 0);
    ZASSERT(pid_fifo.has_value());
    auto hold = fs::sync::open(dir.file("hold"), O_RDWR, 0);
    ZASSERT(hold.has_value());
    auto reader = pipe::open(*pid_fifo, loop);
    ZASSERT(reader.has_value());
    auto opts = shell("echo $$ > pid; exec cat < hold");
    opts.cwd = dir.path.string();
    cancellation_source source;
    auto canceller = [&]() -> task<std::optional<std::string>, error> {
        auto pid = co_await reader->read_line().or_fail();
        source.cancel();
        co_return pid;
    };

    auto [captured, pid] =
        run(with_token(process::capture(opts, loop), source.token()), canceller());
    ZEXPECT(captured.is_cancelled());
    ZASSERT(pid.has_value());
    ZASSERT(pid->has_value());
    const int found = ::kill(std::stoi(**pid), 0);
    const int why = errno;
    ZEXPECT(found == -1);
    ZEXPECT(why == ESRCH);
    ZEXPECT(!fs::sync::close(*hold));
}
#endif

};  // ZEST_SUITE(async_io_process)

}  // namespace

}  // namespace kota
