#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "fs/harness/files.h"
#include "sys/harness/pids.h"
#include "sys/harness/privileges.h"
#include "sys/harness/spawn.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/fs/file.h"
#include "kotatsu/fs/path.h"
#include "kotatsu/sys/process.h"
#include "kotatsu/sys/spawn.h"

#ifdef _WIN32
#include "kotatsu/support/utf16.h"
#endif

namespace kotatsu::sys {

namespace {

#ifdef _WIN32
/// A process whose security lets nobody open it, started suspended and ended
/// when this goes, or when a failed ZASSERT ends this process.
struct UnopenableProcess {
    UnopenableProcess() {
        SECURITY_DESCRIPTOR descriptor;
        ZASSERT(::InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION));
        // A list of access with no entry grants nothing.
        ACL acl;
        ZASSERT(::InitializeAcl(&acl, sizeof(acl), ACL_REVISION));
        ZASSERT(::SetSecurityDescriptorDacl(&descriptor, TRUE, &acl, FALSE));
        SECURITY_ATTRIBUTES attributes{
            .nLength = sizeof(attributes),
            .lpSecurityDescriptor = &descriptor,
            .bInheritHandle = FALSE,
        };
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        // The system's cmd.exe, which CreateProcess would look for in the
        // current directory first.
        std::wstring program;
        ZASSERT(!to_utf16(program, test::shell_path()));
        wchar_t command[] = L"cmd.exe /c exit";
        ZASSERT(::CreateProcessW(program.c_str(),
                                 command,
                                 &attributes,
                                 nullptr,
                                 FALSE,
                                 CREATE_SUSPENDED | CREATE_NO_WINDOW,
                                 nullptr,
                                 nullptr,
                                 &startup,
                                 &info));
    }

    UnopenableProcess(const UnopenableProcess&) = delete;
    UnopenableProcess& operator=(const UnopenableProcess&) = delete;

    ~UnopenableProcess() {
        ::TerminateProcess(info.hProcess, 0);
        ::CloseHandle(info.hThread);
        ::CloseHandle(info.hProcess);
    }

    PROCESS_INFORMATION info{};
    zest::FatalHook on_fatal{[this] { ::TerminateProcess(info.hProcess, 0); }};
};
#endif

#ifdef __linux__
/// The name of this process's main thread.
std::string main_thread_name() {
    std::string name;
    ZASSERT(!fs::read_file(name, "/proc/self/comm"));
    // The kernel ends it with a newline.
    if(name.ends_with('\n')) {
        name.pop_back();
    }
    return name;
}

/// This process's name, put back when this goes.
struct ScopedProcessName {
    ScopedProcessName() = default;

    ScopedProcessName(const ScopedProcessName&) = delete;
    ScopedProcessName& operator=(const ScopedProcessName&) = delete;

    ~ScopedProcessName() {
        set_process_name(previous);
    }

    std::string previous = main_thread_name();
};
#endif

// What this process is; its parent is the integration tests', whose
// sys_probe has the test's runner for one.
ZEST_SUITE(sys_process_self) {

ZEST_CASE(pid_is_a_process) {
    ZEXPECT(pid() > 0);
}

// CMake builds system_tests, Bazel a binary per module: sys_system_tests.
ZEST_CASE(executable_path_names_this_program) {
    std::string path;
    ZASSERT(!executable_path(path));
    ZEXPECT(fs::path::is_absolute(path));
    ZEXPECT(fs::path::stem(path).ends_with("system_tests"));
    ZEXPECT(std::filesystem::is_regular_file(test::std_path(path)));
}

// The path this program was started by reaches its file; the integration
// tests start sys_probe through links.
ZEST_CASE(started_path_names_this_program) {
    std::string started;
    started_path(started);
    std::string path;
    ZASSERT(!executable_path(path));
    ZEXPECT(std::filesystem::equivalent(test::std_path(started), test::std_path(path)));
}

// Linux names the image itself, which outlives its file; elsewhere the file.
// sys_probe starts copies of itself through it.
ZEST_CASE(self_executable_names_this_program) {
    std::string self;
    ZASSERT(!self_executable(self));
#ifdef __linux__
    ZEXPECT(self == "/proc/self/exe");
#else
    std::string path;
    ZASSERT(!executable_path(path));
    ZEXPECT(self == path);
#endif
    ZEXPECT(std::filesystem::is_regular_file(test::std_path(self)));
}

// Read from the system: the program's name first, as it was started.
ZEST_CASE(arguments_start_with_this_program) {
    const auto& arguments = sys::arguments();
    ZASSERT(!arguments.empty());
    ZEXPECT(fs::path::stem(arguments[0]).ends_with("system_tests"));
    ZEXPECT(&sys::arguments() == &arguments);
}

ZEST_CASE(process_alive_holds_for_this_process) {
    ZEXPECT(process_alive(pid()));
}

// kill(0, 0) and kill(-1, 0) would ask of a process group and of every
// process.
ZEST_CASE(process_alive_of_no_pid_is_false) {
    ZEXPECT(!process_alive(0));
    ZEXPECT(!process_alive(-1));
    ZEXPECT(!process_alive(test::missing_pid));
}

ZEST_CASE(process_alive_of_a_child_waited_for_is_false) {
    auto child = spawn(test::shell("exit 0"));
    ZASSERT(child.has_value());
    ZASSERT(child->wait().has_value());
    ZEXPECT(!process_alive(child->pid()));
}

#ifdef _WIN32
ZEST_CASE(process_alive_holds_for_a_process_this_one_may_not_open) {
    UnopenableProcess process;
    // With the debug privilege an administrator opens any process.
    test::ScopedNoPrivileges unprivileged({L"SeDebugPrivilege"});
    auto id = process.info.dwProcessId;
    HANDLE opened = ::OpenProcess(SYNCHRONIZE, FALSE, id);
    auto error = ::GetLastError();
    if(opened != nullptr) {
        ::CloseHandle(opened);
    }
    ZEXPECT(opened == nullptr);
    ZEXPECT(error == static_cast<DWORD>(ERROR_ACCESS_DENIED));
    ZEXPECT(process_alive(static_cast<int>(id)));
}
#else
// One that has exited and is not yet waited for.
ZEST_CASE(process_alive_holds_for_a_zombie) {
    auto child = spawn(test::shell("exit 0"));
    ZASSERT(child.has_value());
    siginfo_t info{};
    ZASSERT(::waitid(P_PID, static_cast<id_t>(child->pid()), &info, WEXITED | WNOWAIT) == 0);
    ZEXPECT(process_alive(child->pid()));
    ZASSERT(child->wait().has_value());
}

// init, which a process other than root's, outside a namespace of its own
// pids, may not signal.
ZEST_CASE(process_alive_holds_for_a_process_this_one_may_not_signal) {
    if(::kill(1, 0) == 0) {
        zest::skip();
        return;
    }
    ZEXPECT(process_alive(1));
}
#endif

#ifdef __linux__
// The name top shows is the main thread's, which the arguments that ps shows
// do not change with.
ZEST_CASE(set_process_name_names_the_main_thread) {
    ScopedProcessName restore;
    std::string arguments;
    ZASSERT(!fs::read_file(arguments, "/proc/self/cmdline"));
    ZEXPECT(!set_process_name("kotatsu-process-name"));
    ZEXPECT(main_thread_name() == "kotatsu-process");
    std::string after;
    ZASSERT(!fs::read_file(after, "/proc/self/cmdline"));
    ZEXPECT(after == arguments);
}

// A character of three bytes after 13, which the fifteenth would cut, goes;
// after 12 it fits.
ZEST_CASE(set_process_name_cuts_at_a_character) {
    ScopedProcessName restore;
    ZEXPECT(!set_process_name("kotatsu-proc-\xE5\x90\x8D"));
    ZEXPECT(main_thread_name() == "kotatsu-proc-");
    ZEXPECT(!set_process_name("kotatsu-pro-\xE5\x90\x8D"));
    ZEXPECT(main_thread_name() == "kotatsu-pro-\xE5\x90\x8D");
}

ZEST_CASE(set_process_name_of_nothing_fails) {
    ScopedProcessName restore;
    ZEXPECT(set_process_name("") == std::errc::invalid_argument);
    // None of the 15 bytes kept starts a character.
    ZEXPECT(set_process_name(std::string(16, '\x80')) == std::errc::invalid_argument);
    ZEXPECT(main_thread_name() == restore.previous);
}

// One of which nothing is left for the name, as of bytes that start no
// character, leaves the name as an empty one does: in a child of this
// process, whose arguments it rewrites.
ZEST_CASE(set_process_title_with_nothing_for_the_name_leaves_it) {
    auto child = ::fork();
    ZASSERT(child >= 0);
    if(child == 0) {
        std::string before;
        std::string after;
        bool kept = !fs::read_file(before, "/proc/self/comm") &&
                    !set_process_title(std::string(16, '\x80')) &&
                    !fs::read_file(after, "/proc/self/comm") && after == before;
        ::_exit(kept ? 0 : 1);
    }
    int status = 0;
    ZASSERT(::waitpid(child, &status, 0) == child);
    ZEXPECT(WIFEXITED(status));
    ZEXPECT(WEXITSTATUS(status) == 0);
}
#else
ZEST_CASE(set_process_name_off_linux_fails) {
    ZEXPECT(set_process_name("kotatsu") == std::errc::not_supported);
}
#endif

};  // ZEST_SUITE(sys_process_self)

}  // namespace

}  // namespace kotatsu::sys
