#include <csignal>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
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

namespace kota::sys {

namespace {

/// The shell, by its full path: an environment given need hold no PATH.
std::string shell_path() {
#ifdef _WIN32
    wchar_t directory[MAX_PATH];
    auto size = ::GetSystemDirectoryW(directory, MAX_PATH);
    return fs::path::join(fs::windows::from_native(std::wstring_view(directory, size)), "cmd.exe");
#else
    return "/bin/sh";
#endif
}

/// Runs `command` through the shell, its stdio ignored.
SpawnOptions shell(std::string_view command) {
    SpawnOptions options{
        .file = shell_path(),
#ifdef _WIN32
        .args = {"cmd.exe", "/c", std::string(command)},
#else
        .args = {"sh", "-c", std::string(command)},
#endif
    };
    options.streams = {Stdio::ignore(), Stdio::ignore(), Stdio::ignore()};
    return options;
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

/// What `options` writes to its stdout, read through a pipe until the child
/// and whatever else holds the pipe have gone, and how the child ended.
struct Output {
    std::string text;
    std::expected<ExitStatus, std::error_code> status;
};

Output output_of(SpawnOptions options) {
    options.streams[1] = Stdio::pipe(false, true);
    auto child = spawn(options);
    ZASSERT(child.has_value());
    auto end = child->take_pipe(1);
    auto text = test::read_to_end(end);
    test::close_end(end);
    return Output{.text = std::move(text), .status = child->wait()};
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
    ZEXPECT(status->status == 3);
    ZEXPECT(status->term_signal == 0);
    ZEXPECT(!status->success());
    // Reaped once, the status stays.
    ZEXPECT(child->wait() == status);
    ZEXPECT(child->try_wait() == std::optional(*status));
}

// The child reads its stdin until it closes.
ZEST_CASE(try_wait_is_empty_while_the_child_runs) {
    auto options = shell(by_platform("cat", "more"));
    options.streams[0] = Stdio::pipe(true, false);
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
    options.streams[0] = Stdio::pipe(true, false);
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
    options.streams[0] = Stdio::pipe(true, false);
    auto child = spawn(options);
    ZASSERT(child.has_value());
    ZEXPECT(child->kill(-1) == std::errc::invalid_argument);
    test::close_end(child->take_pipe(0));
    ZEXPECT(child->wait().has_value());
}

ZEST_CASE(inert_child_fails) {
    Child inert;
    ZEXPECT(inert.pid() == -1);
    ZEXPECT(inert.wait().error() == std::errc::invalid_argument);
    ZEXPECT(inert.kill() == std::errc::invalid_argument);
    ZEXPECT(inert.kill_tree() == std::errc::invalid_argument);
}

ZEST_CASE(spawn_of_a_missing_program_fails) {
    auto options = shell("exit 0");
    options.file = dir.file("missing");
    auto child = spawn(options);
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::no_such_file_or_directory);
    // Nowhere on PATH either.
    options.file = "kotatsu-no-such-program";
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

ZEST_CASE(stdout_goes_to_a_file) {
    auto file = fs::File::open(dir.file("out.txt"), fs::OpenMode::Write);
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
    auto options = shell(by_platform("echo \"$KOTA_GIVEN|${KOTA_INHERITED-unset}\"",
                                     "echo %KOTA_GIVEN%|%KOTA_INHERITED%"));
    options.env = std::vector<EnvVar>{
        {.name = "KOTA_GIVEN", .value = "given"}
    };
    auto output = output_of(options);
    ZEXPECT(output.status.has_value());
    ZEXPECT(trim_newlines(output.text) == by_platform("given|unset", "given|%KOTA_INHERITED%"));
}

ZEST_CASE(env_changes_go_over_this_processs_environment) {
    auto options = shell(
        by_platform("echo \"$KOTA_SET|${KOTA_REMOVED-unset}\"", "echo %KOTA_SET%|%KOTA_REMOVED%"));
    options.env = std::vector<EnvVar>{
        {.name = "KOTA_REMOVED", .value = "given"}
    };
    options.env_changes = {
        {.name = "KOTA_SET",     .value = "set"       },
        {.name = "KOTA_REMOVED", .value = std::nullopt},
    };
    auto output = output_of(options);
    ZEXPECT(trim_newlines(output.text) == by_platform("set|unset", "set|%KOTA_REMOVED%"));
}

// A program named without a path is looked for in the child's PATH, a
// relative entry in the child's directory.
ZEST_CASE(program_is_looked_for_in_the_childs_path) {
#ifdef _WIN32
    std::filesystem::create_directories(test::std_path(dir.file("bin")));
    std::filesystem::copy_file(test::std_path(shell_path()),
                               test::std_path(dir.file(R"(bin\kotatool.exe)")));
    SpawnOptions options{
        .file = "kotatool",
        .args = {"kotatool", "/c", "exit 5"},
    };
#else
    std::filesystem::create_directories(test::std_path(dir.file("bin")));
    test::write_text(dir.file("bin/kotatool"), "#!/bin/sh\nexit 5\n");
    std::filesystem::permissions(test::std_path(dir.file("bin/kotatool")),
                                 std::filesystem::perms::owner_all);
    SpawnOptions options{.file = "kotatool"};
#endif
    options.cwd = dir.root();
    options.env_changes = {
        {.name = "PATH", .value = "bin"}
    };
    auto child = spawn(options);
    ZASSERT(child.has_value());
    auto status = child->wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->status == 5);
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

ZEST_CASE(kill_tree_without_a_group_fails) {
    auto child = spawn(shell("exit 0"));
    ZASSERT(child.has_value());
    ZEXPECT(child->kill_tree() == std::errc::invalid_argument);
    ZEXPECT(child->wait().has_value());
}

// The pipe ends only once the shell and the command it left running are
// both gone: the tree's kill reached it.
ZEST_CASE(kill_tree_ends_the_childs_children) {
    auto options = shell(by_platform("sleep 1000 & echo started; wait",
                                     "start /b ping -n 1000 127.0.0.1 & ping -n 1000 127.0.0.1"));
    options.creation.process_group = true;
    options.streams[1] = Stdio::pipe(false, true);
    auto child = spawn(options);
    ZASSERT(child.has_value());
    auto end = child->take_pipe(1);
    ZEXPECT(!test::read_some(end).empty());
    ZEXPECT(!child->kill_tree());
    test::read_to_end(end);
    test::close_end(end);
    auto status = child->wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->term_signal == 9);
}

#ifndef _WIN32
// kill -0 of the group the shell's pid names finds it only when it leads one.
ZEST_CASE(process_group_makes_the_child_lead_its_group) {
    auto options = shell("kill -0 -$$");
    auto child = spawn(options);
    ZASSERT(child.has_value());
    ZEXPECT(child->wait()->status != 0);
    options.creation.process_group = true;
    child = spawn(options);
    ZASSERT(child.has_value());
    ZEXPECT(child->wait()->status == 0);
    options.creation = {.detached = true};
    child = spawn(options);
    ZASSERT(child.has_value());
    ZEXPECT(child->wait()->status == 0);
}

ZEST_CASE(argv0_need_not_be_the_program) {
    auto options = shell("echo \"$0\"");
    options.args[0] = "kotatsu-zero";
    auto output = output_of(options);
    ZEXPECT(trim_newlines(output.text) == "kotatsu-zero");
}

// A relative path is taken in the child's directory.
ZEST_CASE(relative_program_is_in_the_childs_directory) {
    test::write_text(dir.file("tool"), "#!/bin/sh\nexit 6\n");
    std::filesystem::permissions(test::std_path(dir.file("tool")),
                                 std::filesystem::perms::owner_all);
    auto child = spawn({.file = "./tool", .cwd = dir.root()});
    ZASSERT(child.has_value());
    ZEXPECT(child->wait()->status == 6);
}

// make's jobserver reaches the commands it runs this way.
ZEST_CASE(descriptor_without_close_on_exec_reaches_the_child) {
    int fds[2];
    ZASSERT(::pipe(fds) == 0);
    auto child = spawn(shell(std::format("echo inherited >&{}", fds[1])));
    ::close(fds[1]);
    ZASSERT(child.has_value());
    ZEXPECT(child->wait()->success());
    ZEXPECT(trim_newlines(test::read_to_end(fds[0])) == "inherited");
    ::close(fds[0]);
}

ZEST_CASE(descriptor_with_close_on_exec_stays_here) {
    int fds[2];
    ZASSERT(::pipe(fds) == 0);
    ZASSERT(::fcntl(fds[1], F_SETFD, FD_CLOEXEC) == 0);
    auto child = spawn(shell(std::format("echo leaked >&{}", fds[1])));
    ::close(fds[1]);
    ZASSERT(child.has_value());
    ZEXPECT(!child->wait()->success());
    ZEXPECT(test::read_to_end(fds[0]).empty());
    ::close(fds[0]);
}

ZEST_CASE(signal_ends_the_child_with_it) {
    auto child = spawn(shell("kill -USR1 $$"));
    ZASSERT(child.has_value());
    auto status = child->wait();
    ZASSERT(status.has_value());
    ZEXPECT(status->term_signal == SIGUSR1);
    ZEXPECT(status->status == 0);
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
    auto child = spawn(options);
    ZASSERT(child.has_value());
    ZEXPECT(child->wait()->status == 7);
}

// cmd would read the arguments by its own rules.
ZEST_CASE(batch_file_without_a_command_line_fails) {
    test::write_text(dir.file("tool.bat"), "@exit /b 4\r\n");
    auto child = spawn({.file = dir.file("tool.bat")});
    ZASSERT(!child.has_value());
    ZEXPECT(child.error() == std::errc::invalid_argument);
    child = spawn({.file = dir.file("tool.bat"),
                   .windows_command_line = std::format(R"("{}")", dir.file("tool.bat"))});
    ZASSERT(child.has_value());
    ZEXPECT(child->wait()->status == 4);
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
    ZEXPECT(child->wait()->success());
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
