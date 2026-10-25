#ifdef _WIN32

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <io.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "child.h"
#include "os.h"
#include "kota/support/small_string.h"
#include "kota/support/string_ref.h"
#include "kota/support/utf16.h"
#include "kota/fs/path.h"
#include "kota/sys/spawn.h"
#include "kota/sys/windows.h"

// Windows 10's, which MinGW's headers declare only for _WIN32_WINNT 0x0A00.
#ifndef PROC_THREAD_ATTRIBUTE_JOB_LIST
#define PROC_THREAD_ATTRIBUTE_JOB_LIST ProcThreadAttributeValue(13, FALSE, TRUE, FALSE)
#endif

namespace kota::sys {

namespace {

/// An inheritable duplicate of `source`; none for a handle that is none, as
/// the standard handles of a process without a console are.
std::expected<Handle, std::error_code> inheritable(HANDLE source) {
    if(source == nullptr || source == INVALID_HANDLE_VALUE) {
        return Handle();
    }
    HANDLE duplicate = nullptr;
    if(!::DuplicateHandle(::GetCurrentProcess(),
                          source,
                          ::GetCurrentProcess(),
                          &duplicate,
                          0,
                          TRUE,
                          DUPLICATE_SAME_ACCESS)) {
        return std::unexpected(fs::windows::last_error());
    }
    return Handle(duplicate);
}

#ifdef _MSC_VER
/// Does nothing, for _get_osfhandle: the C runtime would end the process
/// for a descriptor not open.
void ignore_invalid_parameter(const wchar_t*,
                              const wchar_t*,
                              const wchar_t*,
                              unsigned int,
                              std::uintptr_t) {}
#endif

/// The handle of C runtime descriptor `fd`, inheritable; none for a
/// standard stream the process does not have.
std::expected<Handle, std::error_code> inheritable_descriptor(int fd) {
#ifdef _MSC_VER
    auto previous = ::_set_thread_local_invalid_parameter_handler(ignore_invalid_parameter);
    auto handle = ::_get_osfhandle(fd);
    ::_set_thread_local_invalid_parameter_handler(previous);
#else
    // MinGW's runtime says EBADF and nothing more.
    auto handle = ::_get_osfhandle(fd);
#endif
    // -2: a standard stream without a handle.
    if(handle == -2) {
        return Handle();
    }
    if(handle == -1) {
        return std::unexpected(std::make_error_code(std::errc::bad_file_descriptor));
    }
    return inheritable(reinterpret_cast<HANDLE>(handle));
}

/// The two ends of a pipe: this process's, opened for overlapped I/O, and
/// the child's, inheritable, which the child reads, writes, or both.
std::expected<std::pair<Handle, Handle>, std::error_code> create_pipe(bool child_reads,
                                                                      bool child_writes) {
    static std::atomic<unsigned> serial = 0;
    auto name = std::format(LR"(\\.\pipe\kota-{}-{})", ::GetCurrentProcessId(), serial++);
    // Ours reads too when the child only reads, as libuv's does: a shutdown
    // asks how much of what it wrote is left, which takes read access.
    DWORD open_mode = FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE | WRITE_DAC;
    if(child_reads) {
        open_mode |= PIPE_ACCESS_INBOUND | PIPE_ACCESS_OUTBOUND;
    }
    if(child_writes) {
        open_mode |= PIPE_ACCESS_INBOUND;
    }
    Handle ours(::CreateNamedPipeW(name.c_str(),
                                   open_mode,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                   1,
                                   65536,
                                   65536,
                                   0,
                                   nullptr));
    if(ours.handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(fs::windows::last_error());
    }
    DWORD access = WRITE_DAC;
    access |= child_reads ? GENERIC_READ : FILE_READ_ATTRIBUTES;
    access |= child_writes ? GENERIC_WRITE : FILE_WRITE_ATTRIBUTES;
    SECURITY_ATTRIBUTES inherit{.nLength = sizeof(inherit),
                                .lpSecurityDescriptor = nullptr,
                                .bInheritHandle = TRUE};
    Handle theirs(::CreateFileW(name.c_str(), access, 0, &inherit, OPEN_EXISTING, 0, nullptr));
    if(theirs.handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(fs::windows::last_error());
    }
    // Both ends are there: it does not wait.
    if(!::ConnectNamedPipe(ours.handle, nullptr) && ::GetLastError() != ERROR_PIPE_CONNECTED) {
        return std::unexpected(fs::windows::last_error());
    }
    return std::pair(std::move(ours), std::move(theirs));
}

/// NUL, inheritable, for reading as stdin or writing as stdout and stderr.
std::expected<Handle, std::error_code> null_device(bool read) {
    SECURITY_ATTRIBUTES inherit{.nLength = sizeof(inherit),
                                .lpSecurityDescriptor = nullptr,
                                .bInheritHandle = TRUE};
    HANDLE handle =
        ::CreateFileW(L"NUL",
                      read ? FILE_GENERIC_READ : FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES,
                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                      &inherit,
                      OPEN_EXISTING,
                      0,
                      nullptr);
    if(handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(fs::windows::last_error());
    }
    return Handle(handle);
}

/// Whether `path` is a batch file, which CreateProcess runs through cmd,
/// as Windows names it: trailing dots and spaces dropped.
bool is_batch_file(std::string_view path) {
    while(!path.empty() && (path.back() == '.' || path.back() == ' ')) {
        path.remove_suffix(1);
    }
    auto extension = fs::path::extension(path);
    return string_ref(extension).equals_insensitive(".bat") ||
           string_ref(extension).equals_insensitive(".cmd");
}

/// The environment block of `variables`: each `name=value` and its NUL, and
/// a NUL after them all.
std::expected<std::wstring, std::error_code>
    environment_block(const std::vector<EnvVar>& variables) {
    std::wstring block;
    small_wstring<256> text;
    for(const auto& [name, value]: variables) {
        if(auto error = to_utf16(text, name)) {
            return std::unexpected(error);
        }
        block.append(text);
        block += L'=';
        if(auto error = to_utf16(text, value)) {
            return std::unexpected(error);
        }
        block.append(text);
        block += L'\0';
    }
    // An empty block is two NULs.
    if(block.empty()) {
        block += L'\0';
    }
    block += L'\0';
    return block;
}

std::chrono::microseconds microseconds(ULONGLONG hundreds_of_nanoseconds) {
    return std::chrono::microseconds(hundreds_of_nanoseconds / 10);
}

std::chrono::microseconds microseconds(FILETIME time) {
    return microseconds((ULONGLONG(time.dwHighDateTime) << 32) | time.dwLowDateTime);
}

/// What the process an exit was seen of used, and how it ended.
ExitStatus exit_status_of(HANDLE process, HANDLE job, bool tree, int killed_by) {
    DWORD code = 0;
    ::GetExitCodeProcess(process, &code);
    ExitStatus status{.code = static_cast<std::int64_t>(code), .term_signal = killed_by};
    if(tree) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        ::QueryInformationJobObject(job,
                                    JobObjectBasicAccountingInformation,
                                    &accounting,
                                    sizeof(accounting),
                                    nullptr);
        status.user_time = microseconds(static_cast<ULONGLONG>(accounting.TotalUserTime.QuadPart));
        status.system_time =
            microseconds(static_cast<ULONGLONG>(accounting.TotalKernelTime.QuadPart));
    } else {
        FILETIME created, ended, kernel, user;
        ::GetProcessTimes(process, &created, &ended, &kernel, &user);
        status.user_time = microseconds(user);
        status.system_time = microseconds(kernel);
    }
    return status;
}

/// Whether the processes of `job` are all gone.
bool job_is_empty(HANDLE job) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    ::QueryInformationJobObject(job,
                                JobObjectBasicAccountingInformation,
                                &accounting,
                                sizeof(accounting),
                                nullptr);
    return accounting.ActiveProcesses == 0;
}

/// Whether `signum` is one kill takes to end a process.
bool ends_a_process(int signum) {
    return signum == SIGINT || signum == SIGTERM || signum == sigkill;
}

}  // namespace

Child::~Child() {
    for(HANDLE handle: {handle, static_cast<HANDLE>(job), pipes[0], pipes[1], pipes[2]}) {
        if(handle != nullptr) {
            ::CloseHandle(handle);
        }
    }
}

std::error_code Child::reap(bool block) {
    if(exited) {
        return {};
    }
    switch(::WaitForSingleObject(handle, block ? INFINITE : 0)) {
        case WAIT_OBJECT_0: exited = exit_status_of(handle, job, leads_group, killed_by); return {};
        case WAIT_TIMEOUT: return {};
        default: return fs::windows::last_error();
    }
}

std::error_code Child::kill(int signum) {
    if(id < 0 || (signum != 0 && !ends_a_process(signum))) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if(exited || ::WaitForSingleObject(handle, 0) == WAIT_OBJECT_0) {
        return std::make_error_code(std::errc::no_such_process);
    }
    if(signum == 0) {
        return {};
    }
    if(!::TerminateProcess(handle, 1)) {
        auto error = fs::windows::last_error();
        // It exited meanwhile.
        if(::WaitForSingleObject(handle, 0) == WAIT_OBJECT_0) {
            return std::make_error_code(std::errc::no_such_process);
        }
        return error;
    }
    killed_by = signum;
    return {};
}

std::error_code Child::kill_tree(int signum) {
    if(id < 0 || !leads_group || (signum != 0 && !ends_a_process(signum))) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if(job_is_empty(job)) {
        return std::make_error_code(std::errc::no_such_process);
    }
    if(signum == 0) {
        return {};
    }
    // The signal ends the child only if it still runs: a tree may outlive
    // it.
    const bool runs = !exited && ::WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;
    if(!::TerminateJobObject(job, 1)) {
        return fs::windows::last_error();
    }
    if(runs) {
        killed_by = signum;
    }
    return {};
}

std::expected<Child, std::error_code> spawn(const SpawnOptions& options) {
    const auto& creation = options.creation;
    auto environment = child_environment(options);
    auto program = child_program(options, environment);
    if(!program) {
        return std::unexpected(program.error());
    }
    // cmd would read the arguments by its own rules, which quoting for the
    // C runtime does not keep from running a command of theirs.
    if(options.windows_command_line.empty() && is_batch_file(*program)) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    small_wstring<264> application;
    if(auto error = to_utf16(application, *program)) {
        return std::unexpected(error);
    }
    std::string written;
    std::string_view command_line = options.windows_command_line;
    if(command_line.empty()) {
        auto arguments = child_arguments(options);
        // The program's rule has no way to write a quote.
        if(arguments[0].find('"') != std::string::npos) {
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }
        windows::join_command_line(written, arguments);
        command_line = written;
    }
    std::wstring line;
    if(auto error = to_utf16(line, command_line)) {
        return std::unexpected(error);
    }
    std::wstring block;
    if(environment) {
        auto made = environment_block(*environment);
        if(!made) {
            return std::unexpected(made.error());
        }
        block = std::move(*made);
    }
    std::wstring cwd;
    if(!options.cwd.empty()) {
        if(auto error = to_utf16(cwd, options.cwd)) {
            return std::unexpected(error);
        }
        // CreateProcess takes no longer one, as libuv found.
        if(cwd.size() >= MAX_PATH) {
            auto size = ::GetShortPathNameW(cwd.c_str(), nullptr, 0);
            std::wstring short_path(size, L'\0');
            size = ::GetShortPathNameW(cwd.c_str(), short_path.data(), size);
            if(size == 0) {
                return std::unexpected(fs::windows::last_error());
            }
            short_path.resize(size);
            cwd = std::move(short_path);
        }
    }

    Child child;
    // The child's standard handles, inheritable, closed here once it has
    // started; none where the stream is this process's and it has none.
    std::array<Handle, 3> handles;
    bool shares_streams = false;
    constexpr DWORD standard[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for(std::size_t i = 0; i < 3; ++i) {
        const auto& stream = options.streams[i];
        std::expected<Handle, std::error_code> made = Handle();
        switch(stream.kind) {
            case Stdio::Kind::Inherit: made = inheritable(::GetStdHandle(standard[i])); break;
            case Stdio::Kind::Ignore: made = null_device(i == 0); break;
            case Stdio::Kind::FD:
                if(stream.fd < 0) {
                    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
                }
                made = inheritable_descriptor(stream.fd);
                break;
            case Stdio::Kind::File:
                if(stream.file == fs::File::closed) {
                    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
                }
                made = inheritable(stream.file);
                break;
            case Stdio::Kind::Pipe: {
                auto pipe = create_pipe(stream.duplex || i == 0, stream.duplex || i != 0);
                if(!pipe) {
                    return std::unexpected(pipe.error());
                }
                child.pipes[i] = pipe->first.release();
                handles[i] = std::move(pipe->second);
                continue;
            }
        }
        if(!made) {
            return std::unexpected(made.error());
        }
        handles[i] = std::move(*made);
        // One of this process's streams, if it has one there.
        shares_streams |= stream.kind != Stdio::Kind::Ignore && handles[i].handle != nullptr;
    }

    // A job of its own: it ends a child that crashes rather than show
    // Windows's dialog, kills what it holds when it closes with
    // exit_with_parent, and holds its descendants too when it leads a tree.
    child.leads_group = creation.detached || creation.process_group;
    Handle job(::CreateJobObjectW(nullptr, nullptr));
    if(job.handle == nullptr) {
        return std::unexpected(fs::windows::last_error());
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    auto& flags = limits.BasicLimitInformation.LimitFlags;
    flags = JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    if(!child.leads_group) {
        flags |= JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
    }
    if(creation.exit_with_parent) {
        flags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    }
    if(!::SetInformationJobObject(job.handle,
                                  JobObjectExtendedLimitInformation,
                                  &limits,
                                  sizeof(limits))) {
        return std::unexpected(fs::windows::last_error());
    }

    // Each handle once (the same one twice fails the list), none null.
    std::vector<HANDLE> inherited;
    for(const auto& handle: handles) {
        if(handle.handle != nullptr &&
           std::ranges::find(inherited, handle.handle) == inherited.end()) {
            inherited.push_back(handle.handle);
        }
    }
    SIZE_T size = 0;
    ::InitializeProcThreadAttributeList(nullptr, 2, 0, &size);
    std::vector<std::byte> list(size);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(list.data());
    if(!::InitializeProcThreadAttributeList(attributes, 2, 0, &size)) {
        return std::unexpected(fs::windows::last_error());
    }

    struct Attributes {
        LPPROC_THREAD_ATTRIBUTE_LIST list;

        ~Attributes() {
            ::DeleteProcThreadAttributeList(list);
        }
    } owned{attributes};

    // Inherited alone, and only when there are some: an empty list fails.
    if(!inherited.empty() && !::UpdateProcThreadAttribute(attributes,
                                                          0,
                                                          PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                                          inherited.data(),
                                                          inherited.size() * sizeof(HANDLE),
                                                          nullptr,
                                                          nullptr)) {
        return std::unexpected(fs::windows::last_error());
    }
    if(!::UpdateProcThreadAttribute(attributes,
                                    0,
                                    PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                    &job.handle,
                                    sizeof(HANDLE),
                                    nullptr,
                                    nullptr)) {
        return std::unexpected(fs::windows::last_error());
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.hStdInput = handles[0].handle;
    startup.StartupInfo.hStdOutput = handles[1].handle;
    startup.StartupInfo.hStdError = handles[2].handle;
    startup.StartupInfo.wShowWindow =
        static_cast<WORD>(creation.windows_hide_gui ? SW_HIDE : SW_SHOWDEFAULT);
    startup.lpAttributeList = attributes;

    DWORD process_flags = CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
    if(child.leads_group) {
        process_flags |= CREATE_NEW_PROCESS_GROUP;
    }
    // A console without a window: the console programs it starts share it,
    // where a child without a console (DETACHED_PROCESS) has each of them
    // open one. libuv's rule: a stream of this process's keeps its console.
    if(creation.detached || (creation.windows_hide_console && !shares_streams)) {
        process_flags |= CREATE_NO_WINDOW;
    }
    if(options.windows_before_start) {
        process_flags |= CREATE_SUSPENDED;
    }
    PROCESS_INFORMATION created{};
    if(!::CreateProcessW(application.c_str(),
                         line.data(),
                         nullptr,
                         nullptr,
                         !inherited.empty(),
                         process_flags,
                         environment ? block.data() : nullptr,
                         cwd.empty() ? nullptr : cwd.c_str(),
                         &startup.StartupInfo,
                         &created)) {
        // What it says of a working directory that is not there, as
        // chdir's ENOENT says it on POSIX.
        if(::GetLastError() == ERROR_DIRECTORY) {
            return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
        }
        return std::unexpected(fs::windows::last_error());
    }
    Handle process(created.hProcess);
    Handle thread(created.hThread);
    if(options.windows_before_start) {
        // Ended unless it runs: a hook's error, or its exception, leaves no
        // child suspended for good.
        struct Suspended {
            HANDLE process;
            bool resumed = false;

            ~Suspended() {
                if(!resumed) {
                    ::TerminateProcess(process, 1);
                    ::WaitForSingleObject(process, INFINITE);
                }
            }
        } suspended{process.handle};

        if(auto error = options.windows_before_start(process.handle, thread.handle)) {
            return std::unexpected(error);
        }
        if(::ResumeThread(thread.handle) == static_cast<DWORD>(-1)) {
            return std::unexpected(fs::windows::last_error());
        }
        suspended.resumed = true;
    }
    child.id = static_cast<int>(created.dwProcessId);
    child.handle = process.release();
    child.job = job.release();
    return child;
}

}  // namespace kota::sys

#endif
