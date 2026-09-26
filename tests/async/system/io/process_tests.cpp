#include <csignal>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <utility>

#include "async/harness/loop_fixture.h"
#include "async/harness/os.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

// What process::wait() sends a child when it is cancelled: SIGKILL, which
// libuv also reads as 9 on Windows, where <csignal> has no SIGKILL.
constexpr int kill_signal = 9;

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

ZEST_SUITE(async_io_process, test::LoopFixture) {

ZEST_CASE(wait_reports_the_exit_code) {
    auto success = process::spawn(shell("exit 0"), loop);
    auto failure = process::spawn(shell("exit 3"), loop);
    ASSERT(success.has_value());
    ASSERT(failure.has_value());
    EXPECT(success->proc.pid() > 0);

    auto [succeeded, failed] = run(success->proc.wait(), failure->proc.wait());
    ASSERT(succeeded.has_value());
    ASSERT(succeeded->has_value());
    EXPECT((*succeeded)->status == 0);
    EXPECT((*succeeded)->term_signal == 0);
    ASSERT(failed.has_value());
    ASSERT(failed->has_value());
    EXPECT((*failed)->status == 3);
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
    ASSERT(status.has_value());
    ASSERT(status->has_value());
    EXPECT((*status)->status == 0);
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
    EXPECT(status.has_value());
}

ZEST_CASE(stdin_pipe_feeds_the_child) {
    process::options opts;
    opts.file = by_platform("/bin/cat", "more.com");
    opts.streams = {process::stdio::pipe(true, false),
                    process::stdio::pipe(false, true),
                    process::stdio::ignore()};
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
    ASSERT(status.has_value());
    ASSERT(status->has_value());
    EXPECT((*status)->status == 0);
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
    EXPECT(status.has_value());
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
    EXPECT(status.has_value());
    EXPECT(trim_newlines(test::read_file(dir.path / "marker.txt")) == "42");
}

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
    ASSERT(status.has_value());
    ASSERT(status->has_value());
    EXPECT((*status)->status == 7);
}

ZEST_CASE(second_wait_while_one_is_pending_fails) {
    auto spawned = process::spawn(shell("exit 0"), loop);
    ASSERT(spawned.has_value());

    auto [first, second] = run(spawned->proc.wait(), spawned->proc.wait());
    ASSERT(first.has_value());
    EXPECT(first->has_value());
    ASSERT(second.has_value());
    ASSERT(second->has_error());
    EXPECT(second->error() == error::connection_already_in_progress);
}

ZEST_CASE(wait_after_the_exit_returns_the_same_status) {
    auto spawned = process::spawn(shell("exit 5"), loop);
    ASSERT(spawned.has_value());
    auto wait_twice = [&]() -> task<std::pair<process::wait_result, process::wait_result>> {
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
    ASSERT(status->has_value());
    EXPECT((*status)->term_signal == SIGTERM);
}

ZEST_CASE(kill_with_an_invalid_signal_fails) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ASSERT(spawned.has_value());
    EXPECT(spawned->proc.kill(-1) == error::invalid_argument);
    // Closing its stdin ends the child.
    spawned->stdin_pipe = pipe{};

    auto [status] = run(spawned->proc.wait());
    EXPECT(status.has_value());
}

ZEST_CASE(kill_after_the_exit_fails) {
    auto spawned = process::spawn(shell("exit 0"), loop);
    ASSERT(spawned.has_value());

    auto [status] = run(spawned->proc.wait());
    EXPECT(status.has_value());
    EXPECT(spawned->proc.kill(SIGTERM) == error::no_such_process);
}

// Cancelling wait() kills the child, and a later wait() reports that.
ZEST_CASE(cancelled_wait_kills_the_child) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ASSERT(spawned.has_value());
    auto waiting = spawned->proc.wait();
    auto* node = waiting.operator->();
    auto cancel_it = [&]() -> task<> {
        node->cancel();
        co_return;
    };

    auto [cancelled, driver] = run(std::move(waiting), cancel_it());
    EXPECT(cancelled.is_cancelled());
    auto [status] = run(spawned->proc.wait());
    ASSERT(status.has_value());
    ASSERT(status->has_value());
    EXPECT((*status)->term_signal == kill_signal);
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
    EXPECT(status.has_value());
}
#endif

};  // ZEST_SUITE(async_io_process)

}  // namespace

}  // namespace kota
