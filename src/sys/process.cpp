#include "kotatsu/sys/process.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "os.h"
#include "kotatsu/support/small_string.h"

#ifdef _WIN32
// After windows.h, whose types it uses.
#include <winternl.h>

#include "kotatsu/support/utf16.h"
#include "kotatsu/sys/cmd.h"
#elif defined(__APPLE__)
#include <crt_externs.h>
#include <libproc.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <sys/stat.h>
#else
#include <signal.h>
#include <sys/auxv.h>
#include <sys/stat.h>

#include "proc.h"
#include "kotatsu/fs/directory.h"
#endif

namespace kotatsu::sys {

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

bool process_alive(int pid) noexcept {
    // kill takes 0 and below for process groups, -1 for every process.
    if(pid <= 0) {
        return false;
    }
#ifdef _WIN32
    auto process = Handle::open_process(pid, SYNCHRONIZE);
    if(!process) {
        return process.error() == std::errc::permission_denied;
    }
    // One that has exited lives on while a handle holds it.
    return ::WaitForSingleObject(process->handle, 0) == WAIT_TIMEOUT;
#else
    return ::kill(pid, 0) == 0 || errno == EPERM;
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
    // The name stays after a file was renamed over the image's: it is the
    // image's only when it reaches the file the image's header is mapped
    // from.
    proc_regionwithpathinfo region{};
    auto header = reinterpret_cast<std::uint64_t>(::_dyld_get_image_header(0));
    const auto& image = region.prp_vip.vip_vi.vi_stat;
    struct stat named;
    if(::proc_pidinfo(::getpid(), PROC_PIDREGIONPATHINFO, header, &region, sizeof(region)) !=
           static_cast<int>(sizeof(region)) ||
       ::stat(path, &named) != 0 || static_cast<std::uint32_t>(named.st_dev) != image.vst_dev ||
       named.st_ino != image.vst_ino) {
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

void started_path(buffer_ref out) {
#ifdef _WIN32
    small_wstring<264> path;
    path.resize(MAX_PATH);
    while(true) {
        auto size = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        // Cut short when it fills the buffer.
        if(size < path.size()) {
            from_utf16(out, std::wstring_view(path.data(), size));
            return;
        }
        path.resize(path.size() * 2);
    }
#elif defined(__APPLE__)
    // The first call tells the size it takes, the terminator included.
    std::uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);
    ::_NSGetExecutablePath(out.resize(size).data(), &size);
    out.resize(size - 1);
#else
    out.assign(reinterpret_cast<const char*>(::getauxval(AT_EXECFN)));
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
        for(auto argument: cmd::split(text, cmd::Quoting::Windows)) {
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

}  // namespace kotatsu::sys
