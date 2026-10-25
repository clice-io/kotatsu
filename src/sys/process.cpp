#include "kota/sys/process.h"

#include <climits>
#include <cstdlib>
#include <memory>

#include "os.h"

#ifdef _WIN32
// After windows.h, whose types it uses.
#include <winternl.h>

#include "kota/support/utf16.h"
#include "kota/sys/windows.h"
#elif defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#else
#include "kota/fs/file.h"
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

std::expected<std::string, std::error_code> executable_path() {
#ifdef _WIN32
    std::wstring path(MAX_PATH, L'\0');
    while(true) {
        auto size = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if(size == 0) {
            return std::unexpected(fs::windows::last_error());
        }
        // Cut short, the path fills the buffer.
        if(size < path.size()) {
            path.resize(size);
            return from_utf16(std::wstring_view(path));
        }
        path.resize(path.size() * 2);
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    // Given the size the first call asked for, it cannot fail.
    ::_NSGetExecutablePath(path.data(), &size);
    // The path the program was started by, which may be relative or a link.
    std::unique_ptr<char, decltype(&std::free)> real(::realpath(path.c_str(), nullptr), &std::free);
    if(!real) {
        return std::unexpected(errno_code());
    }
    return std::string(real.get());
#else
    // The link's size says nothing in /proc: grow until the target fits.
    std::string path(PATH_MAX, '\0');
    while(true) {
        auto size = ::readlink("/proc/self/exe", path.data(), path.size());
        if(size < 0) {
            return std::unexpected(errno_code());
        }
        if(static_cast<std::size_t>(size) < path.size()) {
            path.resize(static_cast<std::size_t>(size));
            return path;
        }
        path.resize(path.size() * 2);
    }
#endif
}

const std::vector<std::string>& arguments() {
    const static std::vector<std::string> arguments = [] {
#ifdef _WIN32
        auto line = windows::split_command_line(from_utf16(std::wstring_view(::GetCommandLineW())));
        // A program started with an empty command line has not even its name
        // in it: its executable stands for argv[0], as in Rust.
        if(line.empty()) {
            if(auto executable = executable_path()) {
                line.push_back(std::move(*executable));
            }
        }
        return line;
#elif defined(__APPLE__)
        const int argc = *::_NSGetArgc();
        char** argv = *::_NSGetArgv();
        return std::vector<std::string>(argv, argv + argc);
#else
        std::vector<std::string> line;
        auto text = fs::read_file("/proc/self/cmdline");
        if(!text) {
            return line;
        }
        // Each argument ends with a NUL.
        std::string_view rest = *text;
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
