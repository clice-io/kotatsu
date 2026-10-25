#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <fcntl.h>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/wait.h>

#include "uv.h"
#endif

#ifdef __linux__
#include <iterator>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

// As spawn names it for C libraries older than Linux 5.3.
#if !defined(SYS_pidfd_open) && !defined(__alpha__) && !defined(__mips__)
#define SYS_pidfd_open 434
#endif
#endif

#include "async/harness/io.h"
#include "async/harness/os.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/fs/file.h"
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

#ifndef _WIN32
/// Whether child `pid` of this process has been reaped: it is no child of
/// this one any more, whatever process took its pid since.
bool reaped(int pid) {
    siginfo_t info{};
    return ::waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT) < 0 &&
           errno == ECHILD;
}
#endif

/// What `opts`, a shell command that writes to marker.txt, writes there when
/// run in `dir`, or how it ended when it failed.
task<std::string> marker_of(process::options opts, const test::TempDir& dir, event_loop& loop) {
    opts.cwd = dir.root();
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
    co_return trim_newlines(test::read_text(dir.file("marker.txt")));
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
    int fd = test::open_fd(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ZASSERT(fd >= 0);
    auto opts = shell(by_platform("printf kotatsu-fd", "echo kotatsu-fd"));
    opts.streams[1] = process::stdio::from_fd(fd);
    auto spawned = process::spawn(opts, loop);
    ZEXPECT(test::close_fd(fd) == 0);
    ZASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    ZEXPECT(test::exit_status_of(status) == 0);
    ZEXPECT(trim_newlines(test::read_text(dir.file("stdout.txt"))) == "kotatsu-fd");
}

ZEST_CASE(stdout_goes_to_an_open_file) {
    test::TempDir dir;
    auto file =
        fs::File::open(dir.file("stdout.txt"), {.write = true, .truncate = true, .create = true});
    ZASSERT(file.has_value());
    // The child writes from where the file stands.
    ZEXPECT(!file->write("head-"));
    auto opts = shell(by_platform("printf kotatsu-file", "echo kotatsu-file"));
    opts.streams[1] = process::stdio::from_file(*file);
    auto spawned = process::spawn(opts, loop);
    // The child holds a handle of its own.
    ZEXPECT(!file->close());
    ZASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    ZEXPECT(test::exit_status_of(status) == 0);
    ZEXPECT(trim_newlines(test::read_text(dir.file("stdout.txt"))) == "head-kotatsu-file");
}

ZEST_CASE(environment_and_directory_reach_the_child) {
    test::TempDir dir;
    // cmd reads a digit before > as the handle to redirect, so the
    // redirection goes first.
    auto opts = shell(by_platform(R"(printf "$KOTA_TEST_VALUE" > marker.txt)",
                                  ">marker.txt echo %KOTA_TEST_VALUE%"));
    opts.env = std::vector<process::env_var>{
        {.name = "KOTA_TEST_VALUE", .value = "42"}
    };
    opts.cwd = dir.root();
    auto spawned = process::spawn(opts, loop);
    ZASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    ZEXPECT(test::exit_status_of(status) == 0);
    ZEXPECT(trim_newlines(test::read_text(dir.file("marker.txt"))) == "42");
}

// The changes go over the inherited environment: the child still has what
// this process has.
ZEST_CASE(env_changes_go_over_the_inherited_environment) {
    test::TempDir dir;
    test::ScopedEnv inherited("KOTA_TEST_INHERITED", "inherited");
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
    test::ScopedEnv inherited("KOTA_TEST_INHERITED", "inherited");
    auto opts = print_two("KOTA_TEST_GIVEN", "KOTA_TEST_INHERITED");
    opts.env = std::vector<process::env_var>{
        {.name = "KOTA_TEST_GIVEN", .value = "given"}
    };
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
    opts.env = std::vector<process::env_var>{
        {.name = "KOTA_TEST_REMOVED", .value = "given"}
    };
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
    test::ScopedEnv inherited("KOTA_TEST_INHERITED", "inherited");
    auto opts = print_two("KOTA_TEST_GIVEN", "KOTA_TEST_INHERITED");
    opts.env = std::vector<process::env_var>{
        {.name = "KOTA_TEST_GIVEN", .value = "given"}
    };
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
    opts.env = std::vector<process::env_var>{
        {.name = "kota_test_given", .value = "given"}
    };
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

// The pipe made for stdin before the bad descriptor is found goes with the
// failed spawn.
ZEST_CASE(spawn_with_a_bad_descriptor_fails) {
    auto opts = shell("exit 0");
    opts.streams = {process::stdio::pipe(true, false),
                    process::stdio::from_fd(-1),
                    process::stdio::ignore()};

    auto spawned = process::spawn(opts, loop);
    ZASSERT(spawned.has_error());
    ZEXPECT(spawned.error() == error::invalid_argument);
}

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

// The child runs until both waits have begun: its stdin closes after them.
ZEST_CASE(second_wait_while_one_is_pending_fails) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ZASSERT(spawned.has_value());
    auto close_stdin = [&]() -> task<> {
        spawned->stdin_pipe = pipe{};
        co_return;
    };

    auto [first, second, closed] = run(spawned->proc.wait(), spawned->proc.wait(), close_stdin());
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
    int pid_fifo = test::open_fd(dir.file("pid"), O_RDWR, 0);
    ZASSERT(pid_fifo >= 0);
    int hold = test::open_fd(dir.file("hold"), O_RDWR, 0);
    ZASSERT(hold >= 0);
    auto reader = pipe::open(pid_fifo, loop);
    ZASSERT(reader.has_value());
    auto opts = shell("echo $$ > pid; exec cat < hold");
    opts.cwd = dir.root();
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
    ZEXPECT(reaped(std::stoi(**pid)));
    ZEXPECT(test::close_fd(hold) == 0);
}
#endif

ZEST_CASE(cpu_time_reaches_the_exit_status) {
    auto spawned =
        process::spawn(shell(by_platform("i=0; while [ $i -lt 200000 ]; do i=$((i+1)); done",
                                         "for /L %i in (1,1,100000) do @rem")),
                       loop);
    ZASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    ZASSERT(status.has_value());
    ZEXPECT(status->success());
    ZEXPECT((status->user_time + status->system_time).count() > 0);
}

ZEST_CASE(kill_tree_without_a_group_fails) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ZASSERT(spawned.has_value());
    ZEXPECT(spawned->proc.kill_tree() == error::invalid_argument);
    spawned->stdin_pipe = pipe{};

    auto [status] = run(spawned->proc.wait());
    ZEXPECT(test::exit_status_of(status) == 0);
}

// The child leaves a command running that holds its stdout: the pipe ends
// only once both are gone, the tree's kill having reached the command.
ZEST_CASE(kill_tree_ends_the_childs_children) {
    auto opts = shell(by_platform("sleep 1000 & echo started; wait",
                                  "start /b ping -n 1000 127.0.0.1 & ping -n 1000 127.0.0.1"));
    opts.creation.process_group = true;
    opts.streams[1] = process::stdio::pipe(false, true);
    auto spawned = process::spawn(opts, loop);
    ZASSERT(spawned.has_value());
    auto kill_then_read = [&]() -> task<std::string, error> {
        co_await spawned->stdout_pipe.read_chunk().or_fail();
        if(auto killed = spawned->proc.kill_tree()) {
            // The wait ends all the same.
            spawned->proc.kill();
            co_await fail(killed);
        }
        co_return co_await spawned->stdout_pipe.read_to_end().or_fail();
    };

    auto [rest, status] = run(kill_then_read(), spawned->proc.wait());
    ZEXPECT(rest.has_value());
    ZASSERT(status.has_value());
    ZEXPECT(status->term_signal == 9);
}

#ifndef _WIN32
// The child is another's to reap once this test has: the wait says so
// rather than wait for good.
ZEST_CASE(wait_for_a_child_another_reaped_fails) {
    auto spawned = process::spawn(shell("exit 0"), loop);
    ZASSERT(spawned.has_value());
    ZASSERT(::waitpid(spawned->proc.pid(), nullptr, 0) == spawned->proc.pid());

    auto [status] = run(spawned->proc.wait());
    ZASSERT(status.has_error());
    ZEXPECT(status.error() == error::no_such_process);
}

// Nothing tells when the loop has reaped a child whose process is gone: the
// test looks until it has, waitid's WNOWAIT leaving a zombie for the loop.
ZEST_CASE(child_of_a_destroyed_process_is_still_reaped) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ZASSERT(spawned.has_value());
    const int pid = spawned->proc.pid();
    spawned->proc = process{};
    spawned->stdin_pipe = pipe{};
    auto reaped = [&]() -> task<bool> {
        for(int tries = 0; tries < 2000; ++tries) {
            siginfo_t info{};
            if(::waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT) < 0 &&
               errno == ECHILD) {
                co_return true;
            }
            co_await sleep(5, loop);
        }
        co_return false;
    };

    auto [gone] = run(reaped());
    ZASSERT(gone.has_value());
    ZEXPECT(*gone);
}

// libuv's own children, which it reaps through its SIGCHLD handler, and
// these, which it does not know of, end each as they do.
ZEST_CASE(children_of_uv_spawn_and_of_spawn_both_report) {
    struct Exited {
        bool seen = false;
        std::int64_t status = -1;
    } theirs;

    char sh[] = "/bin/sh";
    char flag[] = "-c";
    char command[] = "exit 4";
    char* args[] = {sh, flag, command, nullptr};
    uv_process_options_t uv_options{};
    uv_options.file = sh;
    uv_options.args = args;
    uv_options.exit_cb = [](uv_process_t* handle, std::int64_t status, int) {
        auto& exited = *static_cast<Exited*>(handle->data);
        exited.seen = true;
        exited.status = status;
        ::uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
    };
    uv_process_t uv_child{};
    uv_child.data = &theirs;
    ZASSERT(::uv_spawn(loop.native_handle(), &uv_child, &uv_options) == 0);
    auto ours = process::spawn(shell("exit 6"), loop);
    ZASSERT(ours.has_value());

    auto [status] = run(ours->proc.wait());
    ZEXPECT(test::exit_status_of(status) == 6);
    // Nothing of libuv's tells a task when it has reaped its child: the
    // loop turns until it has.
    for(int turns = 0; !theirs.seen && turns < 2000; ++turns) {
        auto [turned] = run(sleep(5, loop));
        ZEXPECT(turned.has_value());
    }
    ZASSERT(theirs.seen);
    ZEXPECT(theirs.status == 4);
}
#endif

#ifdef __linux__
// A kernel before 5.3 has no pidfd_open, nor has a sandbox that forbids
// it: a seccomp filter makes it fail with ENOSYS in a process of the test's
// own, which spawns and waits there, SIGCHLD telling it of the exit.
ZEST_CASE(exit_is_seen_without_pidfds) {
    const pid_t tester = ::fork();
    ZASSERT(tester >= 0);
    if(tester == 0) {
        // Ended by SIGALRM rather than hang the test.
        ::alarm(10);
        sock_filter filter[] = {
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_pidfd_open, 0, 1),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        };
        sock_fprog program{.len = std::size(filter), .filter = filter};
        if(::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
           ::prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0) {
            ::_exit(2);
        }
        // The filter holds.
        if(::syscall(SYS_pidfd_open, ::getpid(), 0) != -1 || errno != ENOSYS) {
            ::_exit(5);
        }
        event_loop own;
        auto spawned = process::spawn(shell("exit 7"), own);
        if(!spawned || spawned->proc.pid() <= 0) {
            ::_exit(3);
        }
        auto waited = spawned->proc.wait();
        own.schedule(waited);
        own.run();
        auto status = std::move(waited).catch_cancel().result();
        ::_exit(status.has_value() && status->status == 7 ? 0 : 4);
    }
    int status = 0;
    ZASSERT(::waitpid(tester, &status, 0) == tester);
    ZEXPECT(WIFEXITED(status));
    ZEXPECT(WEXITSTATUS(status) == 0);
}
#endif

#ifndef _WIN32
// The shell and its command inherit the end of a pipe the test reads, which
// has no close-on-exec: it ends only once the cancel has killed the group,
// both of them. The capture itself ends with the shell.
ZEST_CASE(cancelled_capture_kills_the_group) {
    test::TempDir dir;
    ZASSERT(::mkfifo(dir.file("pid").c_str(), 0600) == 0);
    int pid_fifo = test::open_fd(dir.file("pid"), O_RDWR, 0);
    ZASSERT(pid_fifo >= 0);
    int hold[2];
    ZASSERT(::pipe(hold) == 0);
    ZASSERT(::fcntl(hold[0], F_SETFD, FD_CLOEXEC) == 0);
    auto reader = pipe::open(pid_fifo, loop);
    ZASSERT(reader.has_value());
    auto held = pipe::open(hold[0], loop);
    ZASSERT(held.has_value());

    // What the cancel left running ends with the test.
    struct Cleanup {
        int end;
        int group = -1;

        ~Cleanup() {
            if(end >= 0) {
                ::close(end);
            }
            if(group > 0) {
                ::killpg(group, SIGKILL);
            }
        }
    } cleanup{hold[1]};

    auto opts = shell("sleep 1000 & echo $$ > pid; wait");
    opts.cwd = dir.root();
    opts.creation.process_group = true;
    cancellation_source source;
    auto canceller = [&]() -> task<std::optional<std::string>, error> {
        auto pid = co_await reader->read_line().or_fail();
        // The shell has it: this process's goes.
        ::close(std::exchange(cleanup.end, -1));
        source.cancel();
        co_return pid;
    };

    auto [captured, pid] =
        run(with_token(process::capture(opts, loop), source.token()), canceller());
    ZEXPECT(captured.is_cancelled());
    ZASSERT(pid.has_value());
    ZASSERT(pid->has_value());
    cleanup.group = std::stoi(**pid);
    ZEXPECT(reaped(cleanup.group));

    auto [rest] = run(held->read_to_end());
    ZEXPECT(rest.has_value());
    if(rest.has_value()) {
        cleanup.group = -1;
    }
}

// The shell exits at once, leaving its command holding the capture's pipes
// and the end of a pipe the test reads: the cancel, once the shell is
// reaped, kills the group still.
ZEST_CASE(capture_cancelled_after_the_child_exited_kills_the_group) {
    test::TempDir dir;
    ZASSERT(::mkfifo(dir.file("pid").c_str(), 0600) == 0);
    int pid_fifo = test::open_fd(dir.file("pid"), O_RDWR, 0);
    ZASSERT(pid_fifo >= 0);
    int hold[2];
    ZASSERT(::pipe(hold) == 0);
    ZASSERT(::fcntl(hold[0], F_SETFD, FD_CLOEXEC) == 0);
    auto reader = pipe::open(pid_fifo, loop);
    ZASSERT(reader.has_value());
    auto held = pipe::open(hold[0], loop);
    ZASSERT(held.has_value());

    struct Cleanup {
        int end;
        int group = -1;

        ~Cleanup() {
            if(end >= 0) {
                ::close(end);
            }
            if(group > 0) {
                ::killpg(group, SIGKILL);
            }
        }
    } cleanup{hold[1]};

    auto opts = shell("sleep 1000 & echo $$ > pid");
    opts.cwd = dir.root();
    opts.creation.process_group = true;
    cancellation_source source;
    auto canceller = [&]() -> task<bool, error> {
        auto pid = co_await reader->read_line().or_fail();
        ::close(std::exchange(cleanup.end, -1));
        cleanup.group = pid ? std::stoi(*pid) : -1;
        // Nothing tells when the loop has reaped the shell: this looks until
        // it has.
        bool gone = false;
        for(int tries = 0; tries < 1000 && !(gone = reaped(cleanup.group)); ++tries) {
            co_await sleep(5, loop);
        }
        source.cancel();
        co_return gone;
    };

    auto [captured, gone] =
        run(with_token(process::capture(opts, loop), source.token()), canceller());
    ZEXPECT(captured.is_cancelled());
    ZASSERT(gone.has_value());
    ZEXPECT(*gone);

    auto [rest] = run(held->read_to_end());
    ZEXPECT(rest.has_value());
    if(rest.has_value()) {
        cleanup.group = -1;
    }
}
#endif

};  // ZEST_SUITE(async_io_process)

}  // namespace

}  // namespace kota
