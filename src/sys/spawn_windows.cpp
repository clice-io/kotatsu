#ifdef _WIN32

#include <array>
#include <atomic>
#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <io.h>
#include <span>
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
#include "kota/fs/resolve.h"
#include "kota/sys/spawn.h"
#include "kota/sys/windows.h"

// Windows 10's, which MinGW's headers declare only for _WIN32_WINNT 0x0A00.
#ifndef PROC_THREAD_ATTRIBUTE_JOB_LIST
#define PROC_THREAD_ATTRIBUTE_JOB_LIST ProcThreadAttributeValue(13, FALSE, TRUE, FALSE)
#endif

namespace kota::sys {

namespace {

/// A duplicate of `source`, not inheritable.
std::expected<Handle, std::error_code> duplicate(HANDLE source) {
    HANDLE copy = nullptr;
    if(!::DuplicateHandle(::GetCurrentProcess(),
                          source,
                          ::GetCurrentProcess(),
                          &copy,
                          0,
                          FALSE,
                          DUPLICATE_SAME_ACCESS)) {
        return std::unexpected(fs::windows::last_error());
    }
    return Handle(copy);
}

/// A duplicate of this process's standard handle `which`; none where it has
/// none open: a process without a console has none, and a program may be
/// started with one closed or never valid.
std::expected<Handle, std::error_code> duplicate_standard(DWORD which) {
    HANDLE source = ::GetStdHandle(which);
    // INVALID_HANDLE_VALUE would duplicate this process.
    if(source == nullptr || source == INVALID_HANDLE_VALUE) {
        return Handle();
    }
    auto made = duplicate(source);
    if(!made && made.error() == std::errc::bad_file_descriptor) {
        return Handle();
    }
    return made;
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

/// A duplicate of the handle of C runtime descriptor `fd`; none for a
/// standard stream the process does not have.
std::expected<Handle, std::error_code> duplicate_descriptor(int fd) {
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
    return duplicate(reinterpret_cast<HANDLE>(handle));
}

/// The two ends of a pipe: this process's, opened for overlapped I/O, and
/// the child's, which the child reads, writes, or both.
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
    Handle theirs(::CreateFileW(name.c_str(), access, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    if(theirs.handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(fs::windows::last_error());
    }
    // Both ends are there: it does not wait.
    if(!::ConnectNamedPipe(ours.handle, nullptr) && ::GetLastError() != ERROR_PIPE_CONNECTED) {
        return std::unexpected(fs::windows::last_error());
    }
    return std::pair(std::move(ours), std::move(theirs));
}

/// NUL, for reading as stdin or writing as stdout and stderr.
std::expected<Handle, std::error_code> null_device(bool read) {
    HANDLE handle =
        ::CreateFileW(L"NUL",
                      read ? FILE_GENERIC_READ : FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES,
                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                      nullptr,
                      OPEN_EXISTING,
                      0,
                      nullptr);
    if(handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(fs::windows::last_error());
    }
    return Handle(handle);
}

/// Held while a child's standard handles are inheritable, which they are
/// only while it is created: shared by children that inherit theirs through
/// a list, exclusive for one that inherits every inheritable handle, which
/// would take another's too.
constinit SRWLOCK inheritance = SRWLOCK_INIT;

/// Whether `path`, as fs::absolute writes it, its trailing dots and spaces
/// dropped as Windows drops them, is a batch file, which CreateProcess runs
/// through cmd.
bool is_batch_file(std::string_view path) {
    auto extension = fs::path::extension(path);
    return string_ref(extension).equals_insensitive(".bat") ||
           string_ref(extension).equals_insensitive(".cmd");
}

/// Appends `argument` to `out` as cmd is to pass it on to a batch file, by
/// the rules of Rust's Command (append_bat_arg): in quotes unless it is not
/// empty, ends in no backslash, which would escape a quote the script puts
/// after it, and each ASCII character of it is a letter, a digit or one of
/// `#$*+-./:?@\_` and none a control character; each quote doubled, and the
/// backslashes before it as before the closing quote; each `%` written
/// `%%cd:~,%`, which cmd reads as the `%` and an empty substring of %cd%,
/// not as the start of a variable's name.
void append_batch_argument(std::string& out, std::string_view argument) {
    constexpr std::string_view unquoted = R"(#$*+-./:?@\_)";
    bool quote = argument.empty() || argument.ends_with('\\');
    for(std::size_t i = 0; i < argument.size(); ++i) {
        auto c = static_cast<unsigned char>(argument[i]);
        const bool ascii_plain = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                                 (c >= 'a' && c <= 'z') || unquoted.contains(argument[i]);
        // U+0080 to U+009F, the control characters past ASCII.
        const bool control = c == 0xC2 && i + 1 < argument.size() &&
                             static_cast<unsigned char>(argument[i + 1]) < 0xA0;
        quote |= (c < 0x80 && !ascii_plain) || control;
    }
    if(quote) {
        out.push_back('"');
    }
    std::size_t backslashes = 0;
    for(char c: argument) {
        if(c == '\\') {
            ++backslashes;
        } else {
            if(c == '"') {
                out.append(backslashes, '\\');
                out.push_back('"');
            } else if(c == '%') {
                out.append("%%cd:~,");
            }
            backslashes = 0;
        }
        out.push_back(c);
    }
    if(quote) {
        out.append(backslashes, '\\');
        out.push_back('"');
    }
}

/// Writes into `out` the command line that has cmd run batch file `script`
/// with `arguments`, as Rust's Command writes it (make_bat_command_line):
/// `cmd.exe /e:ON /v:OFF /d /c ""<script>" <argument>..."`, command
/// extensions on, which append_batch_argument's `%` takes, and neither
/// delayed expansion nor AutoRun commands; cmd drops the outer quotes.
/// invalid_argument for an argument holding a carriage return or a newline,
/// which would end the command there.
std::error_code batch_command_line(std::string& out,
                                   std::string_view script,
                                   std::span<const std::string> arguments) {
    // A file found by name holds no quote, and one named .bat or .cmd ends
    // in no backslash.
    assert(!script.contains('"') && !script.ends_with('\\') && "a batch file's name");
    out = std::format(R"(cmd.exe /e:ON /v:OFF /d /c ""{}")", script);
    for(const auto& argument: arguments) {
        if(argument.find_first_of("\r\n") != std::string::npos) {
            return std::make_error_code(std::errc::invalid_argument);
        }
        out.push_back(' ');
        append_batch_argument(out, argument);
    }
    out.push_back('"');
    return {};
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
    if(holds_nul(options)) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    const auto& creation = options.creation;
    auto environment = child_environment(options);
    auto program = child_program(options, environment);
    if(!program) {
        return std::unexpected(program.error());
    }

    std::string_view application = *program;
    std::string cmd;
    std::string written;
    std::string_view command_line = options.windows_command_line;
    if(command_line.empty()) {
        // The name Windows gives the file, which cmd finds it by too.
        std::string file;
        if(auto error = fs::absolute(file, *program)) {
            return std::unexpected(error);
        }
        if(is_batch_file(file)) {
            // cmd, which CreateProcess would run with a line written for the
            // C runtime, reads the arguments by its own rules, which that
            // quoting does not keep from running a command of theirs.
            auto system = system_directories();
            if(!system) {
                return std::unexpected(system.error());
            }
            cmd = std::move((*system)[0]);
            fs::path::push(cmd, "cmd.exe");
            application = cmd;
            if(auto error = batch_command_line(written, file, options.args)) {
                return std::unexpected(error);
            }
        } else {
            auto arguments = child_arguments(options);
            // The program's rule has no way to write a quote.
            if(arguments[0].find('"') != std::string::npos) {
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            }
            windows::join_command_line(written, arguments);
        }
        command_line = written;
    }
    small_wstring<264> wide_application;
    if(auto error = to_utf16(wide_application, application)) {
        return std::unexpected(error);
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
    // The child's standard handles, inheritable only while it is created and
    // closed then; none where the stream is this process's and it has none
    // open.
    std::array<Handle, 3> handles;
    bool shares_streams = false;
    constexpr DWORD standard[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for(std::size_t i = 0; i < 3; ++i) {
        const auto& stream = options.streams[i];
        std::expected<Handle, std::error_code> made = Handle();
        switch(stream.kind) {
            case Stdio::Kind::Inherit: made = duplicate_standard(standard[i]); break;
            case Stdio::Kind::Ignore: made = null_device(i == 0); break;
            case Stdio::Kind::FD:
                if(stream.fd < 0) {
                    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
                }
                made = duplicate_descriptor(stream.fd);
                break;
            case Stdio::Kind::File:
                // INVALID_HANDLE_VALUE would duplicate this process.
                if(stream.file == fs::File::closed || stream.file == INVALID_HANDLE_VALUE) {
                    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
                }
                made = duplicate(stream.file);
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

    // The handles it inherits, none null, unless it inherits every
    // inheritable one.
    std::vector<HANDLE> inherited;
    if(!creation.windows_inherit_handles) {
        for(const auto& handle: handles) {
            if(handle.handle != nullptr) {
                inherited.push_back(handle.handle);
            }
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
    {
        // Closed, and the lock let go, before the hook below runs: a child it
        // starts inheriting every handle gets none of these, and does not
        // wait for a lock this thread holds.
        struct Inheriting {
            std::array<Handle, 3>& handles;
            bool exclusive;

            Inheriting(std::array<Handle, 3>& handles, bool exclusive) :
                handles(handles), exclusive(exclusive) {
                if(exclusive) {
                    ::AcquireSRWLockExclusive(&inheritance);
                } else {
                    ::AcquireSRWLockShared(&inheritance);
                }
            }

            ~Inheriting() {
                handles = {};
                if(exclusive) {
                    ::ReleaseSRWLockExclusive(&inheritance);
                } else {
                    ::ReleaseSRWLockShared(&inheritance);
                }
            }
        } inheriting(handles, creation.windows_inherit_handles);

        for(const auto& handle: handles) {
            if(handle.handle != nullptr &&
               !::SetHandleInformation(handle.handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
                return std::unexpected(fs::windows::last_error());
            }
        }
        if(!::CreateProcessW(wide_application.c_str(),
                             line.data(),
                             nullptr,
                             nullptr,
                             creation.windows_inherit_handles || !inherited.empty(),
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
