#include <string>

#include "os.h"
#include "kota/sys/info.h"

#ifdef _WIN32
#include "kota/support/utf16.h"
#else
#include <ctime>
#include <sys/resource.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace kota::sys {

std::expected<std::string, std::error_code> hostname() {
#ifdef _WIN32
    // Not gethostname, which needs Winsock started.
    std::wstring name(64, L'\0');
    while(true) {
        auto size = static_cast<DWORD>(name.size());
        if(::GetComputerNameExW(ComputerNameDnsHostname, name.data(), &size)) {
            name.resize(size);
            return from_utf16(std::wstring_view(name));
        }
        // Too small, it gives the size with the NUL.
        if(::GetLastError() != ERROR_MORE_DATA) {
            return std::unexpected(fs::windows::last_error());
        }
        name.resize(size);
    }
#else
    // POSIX may leave a name that does not fit without its NUL.
    char name[256] = {};
    if(::gethostname(name, sizeof(name) - 1) != 0) {
        return std::unexpected(errno_code());
    }
    return std::string(name);
#endif
}

std::chrono::milliseconds uptime() {
#ifdef _WIN32
    return std::chrono::milliseconds(::GetTickCount64());
#elif defined(__APPLE__)
    timeval boot{};
    auto size = sizeof(boot);
    int mib[] = {CTL_KERN, KERN_BOOTTIME};
    ::sysctl(mib, 2, &boot, &size, nullptr, 0);
    auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
    auto booted = std::chrono::seconds(boot.tv_sec) + std::chrono::microseconds(boot.tv_usec);
    return std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch - booted);
#else
    // Suspended time included, which CLOCK_MONOTONIC leaves out.
    timespec now{};
    ::clock_gettime(CLOCK_BOOTTIME, &now);
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::seconds(now.tv_sec) + std::chrono::nanoseconds(now.tv_nsec));
#endif
}

std::expected<int, std::error_code> priority(int pid) {
#ifdef _WIN32
    auto process = ProcessHandle::open(pid, PROCESS_QUERY_LIMITED_INFORMATION);
    if(!process) {
        return std::unexpected(process.error());
    }
    switch(::GetPriorityClass(process->handle)) {
        case REALTIME_PRIORITY_CLASS: return priority_highest;
        case HIGH_PRIORITY_CLASS: return priority_high;
        case ABOVE_NORMAL_PRIORITY_CLASS: return priority_above_normal;
        case BELOW_NORMAL_PRIORITY_CLASS: return priority_below_normal;
        case IDLE_PRIORITY_CLASS: return priority_low;
        default: return priority_normal;
    }
#else
    // -1 is a priority too.
    errno = 0;
    auto value = ::getpriority(PRIO_PROCESS, static_cast<id_t>(pid));
    if(value == -1 && errno != 0) {
        return std::unexpected(errno_code());
    }
    return value;
#endif
}

std::error_code set_priority(int value, int pid) {
    if(value < priority_highest || value > priority_low) {
        return std::make_error_code(std::errc::invalid_argument);
    }
#ifdef _WIN32
    DWORD priority_class = IDLE_PRIORITY_CLASS;
    if(value < priority_high) {
        priority_class = REALTIME_PRIORITY_CLASS;
    } else if(value < priority_above_normal) {
        priority_class = HIGH_PRIORITY_CLASS;
    } else if(value < priority_normal) {
        priority_class = ABOVE_NORMAL_PRIORITY_CLASS;
    } else if(value < priority_below_normal) {
        priority_class = NORMAL_PRIORITY_CLASS;
    } else if(value < priority_low) {
        priority_class = BELOW_NORMAL_PRIORITY_CLASS;
    }
    auto process = ProcessHandle::open(pid, PROCESS_SET_INFORMATION);
    if(!process) {
        return process.error();
    }
    if(!::SetPriorityClass(process->handle, priority_class)) {
        return fs::windows::last_error();
    }
    return {};
#else
    if(::setpriority(PRIO_PROCESS, static_cast<id_t>(pid), value) != 0) {
        return errno_code();
    }
    return {};
#endif
}

}  // namespace kota::sys
