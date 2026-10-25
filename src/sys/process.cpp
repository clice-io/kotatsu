#include "kota/sys/process.h"

#include <string>
#include <string_view>

#include "os.h"
#include "kota/support/small_string.h"

#ifdef _WIN32
// After windows.h, whose types it uses.
#include <winternl.h>

#include "kota/support/utf16.h"
#include "kota/sys/windows.h"
#elif defined(__APPLE__)
#include <crt_externs.h>
#include <libproc.h>
#else
#include <sys/stat.h>

#include "proc.h"
#include "kota/fs/directory.h"
#endif

namespace kota::sys {

int pid() noexcept {
#ifdef _WIN32
    return static_cast<int>(::GetCurrentProcessId());
#else
    return static_cast<int>(::getpid());
#endif
}

int parent_pid() noexcept {
#ifdef _WIN32
    // PROCESS_BASIC_INFORMATION, whose fields the Windows SDK and MinGW name
    // apart.
    struct {
        void* exit_status;
        void* peb;
        void* affinity_and_priority[2];
        ULONG_PTR id;
        ULONG_PTR parent;
    } info{};

    // Of this very process, it cannot fail.
    ::NtQueryInformationProcess(::GetCurrentProcess(),
                                ProcessBasicInformation,
                                &info,
                                sizeof(info),
                                nullptr);
    return static_cast<int>(info.parent);
#else
    return static_cast<int>(::getppid());
#endif
}

std::error_code executable_path(buffer_ref out) {
#ifdef _WIN32
    // The image's name now, where GetModuleFileNameW keeps the one it was
    // loaded by: Windows lets a running program's file be renamed.
    small_wstring<264> path;
    path.resize(MAX_PATH);
    while(true) {
        auto size = static_cast<DWORD>(path.size());
        if(::QueryFullProcessImageNameW(::GetCurrentProcess(), 0, path.data(), &size)) {
            from_utf16(out, std::wstring_view(path.data(), size));
            return {};
        }
        if(::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            return fs::windows::last_error();
        }
        path.resize(path.size() * 2);
    }
#elif defined(__APPLE__)
    // The image's name now, absolute, where _NSGetExecutablePath keeps the
    // one it was started by, which may be relative to a directory this
    // process has left.
    char path[PROC_PIDPATHINFO_MAXSIZE];
    if(::proc_pidpath(::getpid(), path, sizeof(path)) <= 0) {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }
    out.assign(path);
    return {};
#else
    if(auto error = fs::read_symlink(out, "/proc/self/exe")) {
        return error;
    }
    // The link names a removed file with this after it; a file of that name
    // is the image only when it is the one the link reaches.
    constexpr std::string_view removed = " (deleted)";
    if(!out.view().ends_with(removed)) {
        return {};
    }
    struct stat image;
    struct stat named;
    small_string<256> name(out.view());
    if(::stat("/proc/self/exe", &image) != 0 || ::stat(name.c_str(), &named) != 0 ||
       image.st_dev != named.st_dev || image.st_ino != named.st_ino) {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }
    return {};
#endif
}

std::error_code self_executable(buffer_ref out) {
#if !defined(_WIN32) && !defined(__APPLE__)
    // What exec of this link runs is the image itself, the file renamed or
    // removed or not.
    constexpr const char* self = "/proc/self/exe";
    if(::access(self, X_OK) != 0) {
        return errno_code();
    }
    out.assign(self);
    return {};
#else
    return executable_path(out);
#endif
}

const std::vector<std::string>& arguments() {
    const static std::vector<std::string> arguments = [] {
#ifdef _WIN32
        std::string text;
        from_utf16(text, std::wstring_view(::GetCommandLineW()));
        std::vector<std::string> line;
        for(auto argument: windows::split_command_line(text)) {
            line.emplace_back(argument);
        }
        // A program started with an empty command line has not even its name
        // in it: its executable stands for argv[0], as in Rust.
        if(line.empty()) {
            if(std::string executable; !executable_path(executable)) {
                line.push_back(std::move(executable));
            }
        }
        return line;
#elif defined(__APPLE__)
        const int argc = *::_NSGetArgc();
        char** argv = *::_NSGetArgv();
        return std::vector<std::string>(argv, argv + argc);
#else
        std::vector<std::string> line;
        ProcText text;
        if(fs::read_file(text, "/proc/self/cmdline")) {
            return line;
        }
        // Each argument ends with a NUL.
        std::string_view rest = text;
        while(!rest.empty()) {
            auto end = rest.find('\0');
            line.emplace_back(rest.substr(0, end));
            if(end == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(end + 1);
        }
        return line;
#endif
    }();
    return arguments;
}

}  // namespace kota::sys
