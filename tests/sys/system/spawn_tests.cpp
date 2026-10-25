#include <algorithm>
#include <chrono>
#include <csignal>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "fs/harness/files.h"
#include "sys/harness/env.h"
#include "sys/harness/spawn.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/fs/file.h"
#include "kota/fs/path.h"
#include "kota/sys/spawn.h"

#ifdef _WIN32
#include "kota/fs/windows.h"
#endif

namespace kota::sys {

namespace {

/// The shell, by its full path: an environment given need hold no PATH.
std::string shell_path() {
#ifdef _WIN32
    wchar_t directory[MAX_PATH];
    auto size = ::GetSystemDirectoryW(directory, MAX_PATH);
    std::string path;
    fs::windows::from_native(path, std::wstring_view(directory, size));
    fs::path::push(path, "cmd.exe");
    return path;
#else
    return "/bin/sh";
#endif
}

/// The shell spelled by host: `posix`, or `windows` for cmd.
constexpr std::string_view by_platform([[maybe_unused]] std::string_view posix,
                                       [[maybe_unused]] std::string_view windows) {
#ifdef _WIN32
    return windows;
#else
    return posix;
#endif
}

/// Runs `command` through the shell, its stdio ignored.
SpawnOptions shell(std::string_view command) {
    SpawnOptions options{
        .program = shell_path(),
        .args = {std::string(by_platform("-c", "/c")), std::string(command)},
    };
    options.streams = {Stdio::ignore(), Stdio::ignore(), Stdio::ignore()};
    return options;
}

/// What `options` writes to its stdout, read through a pipe until the child
/// and whatever else holds the pipe have gone, and how the child ended.
struct Output {
    std::string text;
    std::expected<ExitStatus, std::error_code> status;
};

Output output_of(SpawnOptions options) {
    options.streams[1] = Stdio::pipe();
    auto child = spawn(options);
    ZASSERT(child.has_value());
    auto end = child->take_pipe(1);
    auto text = test::read_to_end(end);
    test::close_end(end);
    return Output{.text = std::move(text), .status = child->wait()};
}

/// The status `options`' child exits with.
ExitStatus status_of(const SpawnOptions& options) {
    auto child = spawn(options);
    ZASSERT(child.has_value());
    auto status = child->wait();
    ZASSERT(status.has_value());
    return *status;
}

std::string trim_newlines(std::string text) {
    while(!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

ZEST_SUITE(sys_spawn) {

test::TempDir dir;

ZEST_CASE(wait_reports_the_exit_code) {
    auto child = spawn(shell("exit 3"));
    ZASSERT(child.has_value());
    ZEXPECT(child->pid() > 0);
    auto status = child->wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->code == 3);
    ZEXPECT(status->term_signal == 0);
    ZEXPECT(!status->success());
    // Reaped once, the status stays.
    ZEXPECT(child->wait() == status);
    ZEXPECT(child->try_wait() == std::optional(*status));
}

// The child reads its stdin until it closes.
ZEST_CASE(try_wait_is_empty_while_the_child_runs) {
    auto options = shell(by_platform("cat", "more"));
    options.streams[0] = Stdio::pipe();
    auto child = spawn(options);
    ZASSERT(child.has_value());
    ZEXPECT(child->try_wait() == std::optional<ExitStatus>());
    test::close_end(child->take_pipe(0));
    auto status = child->wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->success());
}

ZEST_CASE(kill_ends_a_running_child) {
    auto options = shell(by_platform("cat", "more"));
    options.streams[0] = Stdio::pipe();
    auto child = spawn(options);
    ZASSERT(child.has_value());
    ZEXPECT(!child->kill(SIGTERM));
    auto status = child->wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->term_signal == SIGTERM);
    // Reaped, its pid may be another's.
    ZEXPECT(child->kill(SIGTERM) == std::errc::no_such_process);
}

ZEST_CASE(kill_with_an_invalid_signal_fails) {
    auto options = shell(by_platform("cat", "more"));
    options.streams[0] = Stdio::pipe();
    auto child = spawn(options);
    ZASSERT(child.has_value());
    ZEXPECT(child->kill(-1) == std::errc::invalid_argument);
    test::close_end(child->take_pipe(0));
    ZEXPECT(child->wait().has_value());
}

/// Checks that `child` is inert.
void expect_inert(Child& child) {
    ZEXPECT(child.pid() == -1);
    ZEXPECT(child.native_handle() == Child().native_handle());
    ZEXPECT(child.take_pipe(0) == fs::File::closed);
    auto waited = child.wait();
    ZASSERT(!waited.has_value());
    ZEXPECT(waited.error() == std::errc::invalid_argument);
    auto tried = child.try_wait();
    ZASSERT(!tried.has_value());
    ZEXPECT(tried.error() == std::errc::invalid_argument);
    ZEXPECT(child.kill() == std::errc::invalid_argument);
    ZEXPECT(child.kill(SIGTERM) == std::errc::invalid_argument);
    ZEXPECT(child.kill_tree() == std::errc::invalid_argument);
    ZEXPECT(child.kill_tree(SIGTERM) == std::errc::invalid_argument);
}

ZEST_CASE(inert_child_fails) {
    Child inert;
    expect_inert(inert);
}

ZEST_CASE(moved_from_child_is_inert) {
    auto options = shell("exit 2");
    options.streams[1] = Stdio::pipe();
    auto child = spawn(options);
    ZASSERT(child.has_value());
    const int pid = child->pid();
    Child moved(std::move(*child));
    expect_inert(*child);
    ZEXPECT(moved.pid() == pid);
    Child assigned;
    assigned = std::move(moved);
    expect_inert(moved);
    // The pipe went along.
    auto end = assigned.take_pipe(1);
    ZEXPECT(end != fs::File::closed);
    test::close_end(end);
    auto status = assigned.wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->code == 2);
}

ZEST_CASE(take_pipe_gives_a_pipe_once) {
    auto options = shell("exit 0");
    options.streams[1] = Stdio::pipe();
    auto child = spawn(options);
    ZASSERT(child.has_value());
    // Streams 0 and 2 are no pipes; 3 is no stream.
    ZEXPECT(child->take_pipe(0) == fs::File::closed);
    ZEXPECT(child->take_pipe(2) == fs::File::closed);
    ZEXPECT(child->take_pipe(3) == fs::File::closed);
    auto end = child->take_pipe(1);
    ZEXPECT(end != fs::File::closed);
    ZEXPECT(child->take_pipe(1) == fs::File::closed);
    test::close_end(end);
    ZEXPECT(child->wait().has_value());
}

// The child reads stdin's pipe and writes stdout's and stderr's.
ZEST_CASE(pipe_takes_its_direction_from_its_stream) {
    auto options = shell(by_platform("cat; echo err >&2", "more& echo err>&2"));
    options.streams = {Stdio::pipe(), Stdio::pipe(), Stdio::pipe()};
    auto child = spawn(options);
    ZASSERT(child.has_value());
    auto in = child->take_pipe(0);
    auto out = child->take_pipe(1);
    auto err = child->take_pipe(2);
    ZEXPECT(test::write_all(in, "kotatsu-pipe\n"));
    test::close_end(in);
    ZEXPECT(trim_newlines(test::read_to_end(out)) == "kotatsu-pipe");
    ZEXPECT(trim_newlines(test::read_to_end(err)) == "err");
    test::close_end(out);
    test::close_end(err);
    auto status = child->wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->success());
}

// Signal 0 tells whether it runs.
ZEST_CASE(kill_with_no_signal_tells_whether_it_runs) {
    auto options = shell(by_platform("cat", "more"));
    options.streams[0] = Stdio::pipe();
    auto child = spawn(options);
    ZASSERT(child.has_value());
    ZEXPECT(!child->kill(0));
    test::close_end(child->take_pipe(0));
    ZEXPECT(child->wait().has_value());
    ZEXPECT(child->kill(0) == std::errc::no_such_process);
}

// What waits for the exit without reaping the child, so that try_wait has
// one to reap.
void wait_without_reaping(Child& child) {
#ifdef _WIN32
    ZEXPECT(::WaitForSingleObject(child.native_handle(), INFINITE) == WAIT_OBJECT_0);
#else
    siginfo_t info{};
    ZEXPECT(::waitid(P_PID, static_cast<id_t>(child.pid()), &info, WEXITED | WNOWAIT) == 0);
#endif
}

ZEST_CASE(try_wait_reaps_a_child_that_exited) {
    auto child = spawn(shell("exit 4"));
    ZASSERT(child.has_value());
    wait_without_reaping(*child);
    auto status = child->try_wait();
    ZASSERT(status.has_value());
    ZASSERT(status->has_value());
    ZEXPECT((*status)->code == 4);
}

ZEST_CASE(spawn_of_a_missing_program_fails) {
    auto options = shell("exit 0");
    options.program = dir.file("missing");
    auto child = spawn(options);
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::no_such_file_or_directory);
    // Nowhere on PATH either.
    options.program = "kotatsu-no-such-program";
    child = spawn(options);
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(spawn_in_a_missing_directory_fails) {
    auto options = shell("exit 0");
    options.cwd = dir.file("missing");
    auto child = spawn(options);
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(spawn_with_a_bad_descriptor_fails) {
    auto options = shell("exit 0");
    options.streams[1] = Stdio::from_fd(-1);
    auto child = spawn(options);
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::invalid_argument);
    // Open in this process it is not.
    options.streams[1] = Stdio::from_fd(1000);
    child = spawn(options);
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::bad_file_descriptor);
#ifndef _WIN32
    // The lowest number free: a pipe the spawn makes would take it.
    int freed = ::dup(0);
    ZASSERT(freed >= 0);
    ::close(freed);
    options.streams = {Stdio::pipe(), Stdio::from_fd(freed), Stdio::pipe()};
    child = spawn(options);
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::bad_file_descriptor);
    options.streams = {Stdio::ignore(), Stdio::ignore(), Stdio::ignore()};
#endif
    fs::File closed;
    options.streams[1] = Stdio::from_file(closed);
    child = spawn(options);
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::invalid_argument);
}

ZEST_CASE(stdout_goes_to_a_file) {
    auto file =
        fs::File::open(dir.file("out.txt"), {.write = true, .truncate = true, .create = true});
    ZASSERT(file.has_value());
    auto options = shell("echo kotatsu-file");
    options.streams[1] = Stdio::from_file(*file);
    auto child = spawn(options);
    ZASSERT(child.has_value());
    ZEXPECT(!file->close());
    ZEXPECT(child->wait().has_value());
    ZEXPECT(trim_newlines(test::read_text(dir.file("out.txt"))) == "kotatsu-file");
}

// The given environment is the child's, and nothing else of this process's.
ZEST_CASE(environment_reaches_the_child) {
    test::ScopedEnv inherited("KOTA_INHERITED", "inherited");
    auto options = shell(by_platform("echo \"$KOTA_GIVEN,${KOTA_INHERITED-unset}\"",
                                     "echo %KOTA_GIVEN%,%KOTA_INHERITED%"));
    options.env = std::vector<EnvVar>{
        {.name = "KOTA_GIVEN", .value = "given"}
    };
    auto output = output_of(options);
    ZEXPECT(output.status.has_value());
    ZEXPECT(trim_newlines(output.text) == by_platform("given,unset", "given,%KOTA_INHERITED%"));
}

ZEST_CASE(env_changes_go_over_the_given_environment) {
    auto options = shell(
        by_platform("echo \"$KOTA_SET,${KOTA_REMOVED-unset}\"", "echo %KOTA_SET%,%KOTA_REMOVED%"));
    options.env = std::vector<EnvVar>{
        {.name = "KOTA_REMOVED", .value = "given"}
    };
    options.env_changes = {
        {.name = "KOTA_SET",     .value = "set"       },
        {.name = "KOTA_REMOVED", .value = std::nullopt},
    };
    auto output = output_of(options);
    ZEXPECT(output.status.has_value());
    ZEXPECT(trim_newlines(output.text) == by_platform("set,unset", "set,%KOTA_REMOVED%"));
}

// The last change of a name counts.
ZEST_CASE(env_changes_go_over_this_processs_environment) {
    test::ScopedEnv kept("KOTA_KEPT", "kept");
    test::ScopedEnv removed("KOTA_REMOVED", "inherited");
    auto options = shell(by_platform("echo \"$KOTA_KEPT,$KOTA_SET,${KOTA_REMOVED-unset}\"",
                                     "echo %KOTA_KEPT%,%KOTA_SET%,%KOTA_REMOVED%"));
    options.env_changes = {
        {.name = "KOTA_SET",     .value = "first"     },
        {.name = "KOTA_REMOVED", .value = std::nullopt},
        {.name = "KOTA_SET",     .value = "set"       },
    };
    auto output = output_of(options);
    ZEXPECT(output.status.has_value());
    ZEXPECT(trim_newlines(output.text) == by_platform("kept,set,unset", "kept,set,%KOTA_REMOVED%"));
}

// A program named without a path is looked for in the child's PATH, a
// relative entry in the child's directory.
ZEST_CASE(program_is_looked_for_in_the_childs_path) {
#ifdef _WIN32
    std::filesystem::create_directories(test::std_path(dir.file("bin")));
    std::filesystem::copy_file(test::std_path(shell_path()),
                               test::std_path(dir.file(R"(bin\kotatool.exe)")));
    SpawnOptions options{
        .program = "kotatool",
        .args = {"/c", "exit 5"},
    };
#else
    std::filesystem::create_directories(test::std_path(dir.file("bin")));
    test::write_text(dir.file("bin/kotatool"), "#!/bin/sh\nexit 5\n");
    std::filesystem::permissions(test::std_path(dir.file("bin/kotatool")),
                                 std::filesystem::perms::owner_all);
    SpawnOptions options{.program = "kotatool"};
#endif
    options.cwd = dir.root();
    options.env_changes = {
        {.name = "PATH", .value = "bin"}
    };
    auto child = spawn(options);
    ZASSERT(child.has_value());
    auto status = child->wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->code == 5);
}

ZEST_CASE(cpu_time_is_reported) {
    auto child = spawn(shell(by_platform("i=0; while [ $i -lt 200000 ]; do i=$((i+1)); done",
                                         "for /L %i in (1,1,100000) do @rem")));
    ZASSERT(child.has_value());
    auto status = child->wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->success());
    ZEXPECT((status->user_time + status->system_time).count() > 0);
}

// What the buffers held before goes.
ZEST_CASE(capture_returns_what_the_child_wrote_and_how_it_ended) {
    std::string out = "stale";
    std::string err = "stale";
    auto status = capture(out,
                          err,
                          shell(by_platform("printf out; printf err >&2; exit 3",
                                            "echo out& echo err>&2& exit /b 3")));
    ZASSERT(status.has_value());
    ZEXPECT(status->code == 3);
    ZEXPECT(trim_newlines(out) == "out");
    ZEXPECT(trim_newlines(err) == "err");
}

/// A child that fills stderr before it writes stdout, more than a pipe holds
/// to each: `lines` times `err_line` and `out_line`, one of 'e's and one of
/// 'o's.
struct Flood {
    SpawnOptions options;

    std::string err_line;

    std::string out_line;

    std::size_t lines;
};

Flood flood() {
#ifdef _WIN32
    // cmd's loop is slow: fewer, longer lines.
    std::string e(98, 'e');
    std::string o(98, 'o');
    constexpr std::string_view loop = "for /L %i in (1,1,2000) do @echo ";
    return {
        .options = shell(std::format("({0}{1}) 1>&2& {0}{2}", loop, e, o)),
        .err_line = e + "\r\n",
        .out_line = o + "\r\n",
        .lines = 2000,
    };
#else
    return {
        .options = shell("yes eee | head -n 50000 >&2; yes ooo | head -n 50000"),
        .err_line = "eee\n",
        .out_line = "ooo\n",
        .lines = 50000,
    };
#endif
}

std::string repeated(std::string_view line, std::size_t times) {
    std::string text;
    for(std::size_t i = 0; i < times; ++i) {
        text += line;
    }
    return text;
}

// Reading one pipe after the other, the capture would wait on the first for
// good.
ZEST_CASE(capture_reads_both_streams_while_the_child_runs) {
    auto [options, err_line, out_line, lines] = flood();
    std::string out;
    std::vector<char> err;
    auto status = capture(out, err, options);
    ZASSERT(status.has_value());
    ZEXPECT(status->success());
    ZEXPECT(out == repeated(out_line, lines));
    ZEXPECT((std::string_view(err.data(), err.size()) == repeated(err_line, lines)));
}

// One buffer takes both, a read at a time.
ZEST_CASE(capture_into_one_buffer_takes_both_streams) {
    auto [options, err_line, out_line, lines] = flood();
    std::string both;
    auto status = capture(both, both, options);
    ZASSERT(status.has_value());
    ZEXPECT(status->success());
    ZEXPECT(both.size() == (err_line.size() + out_line.size()) * lines);
    ZEXPECT(std::ranges::count(both, 'e') == std::ranges::count(repeated(err_line, lines), 'e'));
    ZEXPECT(std::ranges::count(both, 'o') == std::ranges::count(repeated(out_line, lines), 'o'));
}

// A child that reads its stdin to the end exits at once: there is nothing
// to read, whatever the options asked for.
ZEST_CASE(capture_gives_the_child_no_input) {
    auto options = shell(by_platform("cat", "more"));
    options.streams[0] = Stdio::pipe();
    std::string out;
    std::string err;
    auto status = capture(out, err, options);
    ZASSERT(status.has_value());
    ZEXPECT(status->success());
    ZEXPECT(trim_newlines(out).empty());
    ZEXPECT(err.empty());
}

ZEST_CASE(capture_of_a_missing_program_fails) {
    auto options = shell("exit 0");
    options.program = dir.file("missing");
    std::string out;
    std::string err;
    auto status = capture(out, err, options);
    ZASSERT(!status.has_value());
    ZEXPECT(status.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(kill_tree_without_a_group_fails) {
    auto child = spawn(shell("exit 0"));
    ZASSERT(child.has_value());
    ZEXPECT(child->kill_tree() == std::errc::invalid_argument);
    ZEXPECT(child->wait().has_value());
}

/// Kills the tree `child` leads when it goes, for a test that fails before
/// it does: nothing it started is left running.
struct TreeKiller {
    Child& child;

    ~TreeKiller() {
        child.kill_tree();
    }
};

// The pipe ends only once the shell and the command it left running are
// both gone: the tree's kill reached it.
ZEST_CASE(kill_tree_ends_the_childs_children) {
    auto options = shell(by_platform("sleep 1000 & echo started; wait",
                                     "start /b ping -n 1000 127.0.0.1 & ping -n 1000 127.0.0.1"));
    options.creation.process_group = true;
    options.streams[1] = Stdio::pipe();
    auto child = spawn(options);
    ZASSERT(child.has_value());
    TreeKiller killer{*child};
    auto end = child->take_pipe(1);
    ZEXPECT(!test::read_some(end).empty());
    ZEXPECT(!child->kill_tree());
    test::read_to_end(end);
    test::close_end(end);
    auto status = child->wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->term_signal == 9);
}

// The group outlives its leader while what it started runs.
ZEST_CASE(kill_tree_reaches_the_group_after_its_leader_exited) {
    auto options = shell(
        by_platform("sleep 1000 & echo started", "start /b ping -n 1000 127.0.0.1 & echo started"));
    options.creation.process_group = true;
    options.streams[1] = Stdio::pipe();
    auto child = spawn(options);
    ZASSERT(child.has_value());
    TreeKiller killer{*child};
    auto end = child->take_pipe(1);
    ZEXPECT(!test::read_some(end).empty());
    auto status = child->wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->success());
    ZEXPECT(!child->kill_tree());
    test::read_to_end(end);
    test::close_end(end);
}

// The console host Windows starts for a console child may stay in its job
// a moment after the child: signal 0 tells when the group is empty.
ZEST_CASE(kill_tree_of_an_empty_group_fails) {
    auto options = shell("exit 0");
    options.creation.process_group = true;
    auto child = spawn(options);
    ZASSERT(child.has_value());
    ZEXPECT(child->wait().has_value());
    for(int tries = 0; tries < 1000 && !child->kill_tree(0); ++tries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ZEXPECT(child->kill_tree(0) == std::errc::no_such_process);
    ZEXPECT(child->kill_tree() == std::errc::no_such_process);
}

#ifndef _WIN32
// kill -0 of the group the shell's pid names finds it only when it leads one.
ZEST_CASE(process_group_makes_the_child_lead_its_group) {
    auto options = shell("kill -0 -$$");
    ZEXPECT(status_of(options).code != 0);
    options.creation.process_group = true;
    ZEXPECT(status_of(options).code == 0);
    options.creation = {.detached = true};
    ZEXPECT(status_of(options).code == 0);
}

// A stream that ends early leaves the other read to its end.
ZEST_CASE(capture_reads_on_past_a_stream_that_ended) {
    std::string out;
    std::string err;
    auto status = capture(out, err, shell("printf out; exec >&-; yes eee | head -n 50000 >&2"));
    ZASSERT(status.has_value());
    ZEXPECT(status->success());
    ZEXPECT(out == "out");
    ZEXPECT(err.size() == 200000U);
}

// With -c and nothing after the command, the shell's $0 is its argv[0]: the
// program, unless arg0 says otherwise.
ZEST_CASE(arg0_need_not_be_the_program) {
    auto options = shell("echo \"$0\"");
    ZEXPECT(trim_newlines(output_of(options).text) == options.program);
    options.arg0 = "kotatsu z\xC3\xA9ro";
    ZEXPECT(trim_newlines(output_of(options).text) == "kotatsu z\xC3\xA9ro");
}

// The child answers on its stdin what it read there. cmd has no plain way to
// do so; zest's runner, which speaks to its workers this way, covers Windows.
ZEST_CASE(duplex_pipe_is_read_and_written_by_both) {
    auto options = shell(R"(read line; echo "$line-back" >&0)");
    options.streams[0] = Stdio::duplex_pipe();
    auto child = spawn(options);
    ZASSERT(child.has_value());
    auto end = child->take_pipe(0);
    ZASSERT(test::write_all(end, "kotatsu\n"));
    ZEXPECT(trim_newlines(test::read_to_end(end)) == "kotatsu-back");
    test::close_end(end);
    auto status = child->wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->success());
}

/// Writes an executable script `name` in the test's directory: `text`.
std::string write_script(std::string_view name, std::string_view text) {
    auto path = dir.file(name);
    test::write_text(path, text);
    std::filesystem::permissions(test::std_path(path), std::filesystem::perms::owner_all);
    return path;
}

// A relative path is taken in the child's directory.
ZEST_CASE(relative_program_is_in_the_childs_directory) {
    write_script("tool", "#!/bin/sh\nexit 6\n");
    ZEXPECT(status_of({.program = "./tool", .cwd = dir.root()}).code == 6);
}

// The child changes to a relative directory once: the program is found
// where it runs.
ZEST_CASE(relative_directory_is_this_processs) {
    write_script("tool", "#!/bin/sh\nexit 6\n");
    auto relative = std::filesystem::relative(test::std_path(dir.root())).string();
    ZEXPECT(status_of({.program = "./tool", .cwd = relative}).code == 6);
    std::filesystem::create_directories(test::std_path(dir.file("bin")));
    write_script("bin/kotatool", "#!/bin/sh\nexit 7\n");
    ZEXPECT(status_of({
                          .program = "kotatool",
                          .env_changes = {{.name = "PATH", .value = "bin"}},
                          .cwd = relative,
                      })
                .code == 7);
}

// As execvp runs it.
ZEST_CASE(script_without_an_interpreter_runs_in_the_shell) {
    auto path = write_script("plain", "exit 8\n");
    ZEXPECT(status_of({.program = path}).code == 8);
}

ZEST_CASE(spawn_of_a_file_not_executable_fails) {
    auto path = dir.file("data");
    test::write_text(path, "#!/bin/sh\n");
    auto child = spawn({.program = path});
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::permission_denied);
}

// make's jobserver reaches the commands it runs this way.
ZEST_CASE(descriptor_without_close_on_exec_reaches_the_child) {
    int fds[2];
    ZASSERT(::pipe(fds) == 0);
    auto child = spawn(shell(std::format("echo inherited >&{}", fds[1])));
    ::close(fds[1]);
    ZASSERT(child.has_value());
    auto status = child->wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->success());
    ZEXPECT(trim_newlines(test::read_to_end(fds[0])) == "inherited");
    ::close(fds[0]);
}

// Whatever the shell finds at that number, if anything, it is not the pipe.
ZEST_CASE(descriptor_with_close_on_exec_stays_here) {
    int fds[2];
    ZASSERT(::pipe(fds) == 0);
    ZASSERT(::fcntl(fds[1], F_SETFD, FD_CLOEXEC) == 0);
    auto child = spawn(shell(std::format("echo leaked >&{}", fds[1])));
    ::close(fds[1]);
    ZASSERT(child.has_value());
    ZEXPECT(child->wait().has_value());
    ZEXPECT(test::read_to_end(fds[0]).empty());
    ::close(fds[0]);
}

ZEST_CASE(signal_ends_the_child_with_it) {
    auto status = status_of(shell("kill -USR1 $$"));
    ZEXPECT(status.term_signal == SIGUSR1);
    ZEXPECT(status.code == 0);
}

// What this process ignores and blocks the child does not: a shell cannot
// handle a signal ignored when it started.
ZEST_CASE(child_starts_with_signals_at_their_defaults) {
    struct Restore {
        struct sigaction action;
        sigset_t mask;

        ~Restore() {
            ::sigaction(SIGUSR1, &action, nullptr);
            ::pthread_sigmask(SIG_SETMASK, &mask, nullptr);
        }
    } restore{};

    struct sigaction ignore{};
    ignore.sa_handler = SIG_IGN;
    ZASSERT(::sigaction(SIGUSR1, &ignore, &restore.action) == 0);
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGUSR2);
    ZASSERT(::pthread_sigmask(SIG_BLOCK, &blocked, &restore.mask) == 0);
    ZEXPECT(status_of(shell("kill -USR1 $$")).term_signal == SIGUSR1);
    ZEXPECT(status_of(shell("kill -USR2 $$")).term_signal == SIGUSR2);
}

// SIGCHLD set to SIG_IGN has the system reap children itself.
ZEST_CASE(child_reaped_by_another_is_lost) {
    struct Restore {
        struct sigaction action;

        ~Restore() {
            ::sigaction(SIGCHLD, &action, nullptr);
        }
    } restore{};

    struct sigaction ignore{};
    ignore.sa_handler = SIG_IGN;
    ZASSERT(::sigaction(SIGCHLD, &ignore, &restore.action) == 0);
    auto child = spawn(shell("exit 0"));
    ZASSERT(child.has_value());
    auto status = child->wait();
    ZASSERT(!status.has_value());
    ZEXPECT(status.error() == std::errc::no_child_process);
    // Its pid may be another's now.
    ZEXPECT(child->kill(0) == std::errc::no_such_process);
    // Gone before this process looked: kill says so once the system has
    // let it go, which Linux does a moment after waitid stops seeing it.
    auto unseen = spawn(shell("exit 0"));
    ZASSERT(unseen.has_value());
    for(int tries = 0; tries < 1000 && !unseen->kill(0); ++tries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ZEXPECT(unseen->kill(0) == std::errc::no_such_process);
}
#endif

#ifdef __APPLE__
ZEST_CASE(exit_with_parent_fails_on_macos) {
    auto options = shell("exit 0");
    options.creation.exit_with_parent = true;
    auto child = spawn(options);
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::not_supported);
}
#endif

#ifdef _WIN32
ZEST_CASE(child_is_born_in_a_job) {
    auto child = spawn(shell("exit 0"));
    ZASSERT(child.has_value());
    BOOL in_job = FALSE;
    ZEXPECT(::IsProcessInJob(child->native_handle(), nullptr, &in_job));
    ZEXPECT(in_job);
    ZEXPECT(child->wait().has_value());
}

ZEST_CASE(windows_command_line_is_taken_as_it_is) {
    auto options = shell("");
    options.windows_command_line = R"(cmd /c "exit 7")";
    ZEXPECT(status_of(options).code == 7);
}

// The program's rule in a command line has no way to write a quote.
ZEST_CASE(arg0_with_a_quote_fails) {
    auto options = shell("exit 0");
    options.arg0 = R"(a"b)";
    auto child = spawn(options);
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::invalid_argument);
}

// cmd would read the arguments by its own rules. Windows drops trailing dots
// and spaces from a name, so `tool.bat.` is a batch file too.
ZEST_CASE(batch_file_without_a_command_line_fails) {
    test::write_text(dir.file("tool.bat"), "@exit /b 4\r\n");
    for(auto name: {"tool.bat", "TOOL.CMD", "tool.bat.", "tool.bat "}) {
        auto child = spawn({.program = dir.file(name)});
        ZASSERT(!child.has_value());
        ZEXPECT(child.error() == std::errc::invalid_argument);
    }
    // Found in PATH too.
    auto child = spawn({
        .program = "tool.bat",
        .env_changes = {{.name = "PATH", .value = dir.root()}},
    });
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::invalid_argument);
    ZEXPECT(status_of({.program = dir.file("tool.bat"),
                       .windows_command_line = std::format(R"("{}")", dir.file("tool.bat"))})
                .code == 4);
}

// CreateProcess would look in the current directory first.
ZEST_CASE(program_is_not_looked_for_in_the_current_directory) {
    std::filesystem::copy_file(test::std_path(shell_path()),
                               test::std_path(dir.file("kotahere.exe")));
    auto child = spawn({
        .program = "kotahere",
        .env_changes = {{.name = "PATH"}},
        .cwd = dir.root(),
    });
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::no_such_file_or_directory);
}

// Its job closes with the Child.
ZEST_CASE(destroying_a_child_started_exit_with_parent_kills_it) {
    auto options = shell("ping -n 1000 127.0.0.1");
    options.creation.exit_with_parent = true;
    auto child = spawn(options);
    ZASSERT(child.has_value());
    HANDLE process = nullptr;
    ZASSERT(::DuplicateHandle(::GetCurrentProcess(),
                              child->native_handle(),
                              ::GetCurrentProcess(),
                              &process,
                              SYNCHRONIZE,
                              FALSE,
                              0));
    *child = Child();
    ZEXPECT(::WaitForSingleObject(process, 10000) == WAIT_OBJECT_0);
    ::CloseHandle(process);
}

// Before its first instruction, the child is suspended in its job.
ZEST_CASE(windows_before_start_sees_the_child_suspended) {
    bool called = false;
    auto options = shell("exit 0");
    options.windows_before_start = [&](void* process, void* thread) -> std::error_code {
        called = true;
        BOOL in_job = FALSE;
        ZEXPECT(::IsProcessInJob(process, nullptr, &in_job));
        ZEXPECT(in_job);
        // Suspended once already: twice now, and back.
        ZEXPECT(::SuspendThread(thread) == 1U);
        ZEXPECT(::ResumeThread(thread) == 2U);
        return {};
    };
    auto child = spawn(options);
    ZASSERT(child.has_value());
    ZEXPECT(called);
    auto status = child->wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->success());
}

ZEST_CASE(windows_before_start_error_fails_the_spawn) {
    auto options = shell("exit 0");
    options.windows_before_start = [](void*, void*) {
        return std::make_error_code(std::errc::operation_canceled);
    };
    auto child = spawn(options);
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::operation_canceled);
}
#endif

};  // ZEST_SUITE(sys_spawn)

}  // namespace

}  // namespace kota::sys
