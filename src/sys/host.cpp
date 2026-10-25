#include <cstddef>
#include <iterator>
#include <string>

#include "os.h"
#include "kota/support/small_string.h"
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

std::error_code hostname(buffer_ref out) {
#ifdef _WIN32
    // Not gethostname, which needs Winsock started.
    small_wstring<64> name;
    name.resize(64);
    while(true) {
        auto size = static_cast<DWORD>(name.size());
        if(::GetComputerNameExW(ComputerNameDnsHostname, name.data(), &size)) {
            from_utf16(out, std::wstring_view(name.data(), size));
            return {};
        }
        // Too small, it gives the size with the NUL.
        if(::GetLastError() != ERROR_MORE_DATA) {
            return fs::windows::last_error();
        }
        name.resize(size);
    }
#else
    // POSIX may leave a name that does not fit without its NUL.
    char name[256] = {};
    if(::gethostname(name, sizeof(name) - 1) != 0) {
        return errno_code();
    }
    out.assign(name);
    return {};
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

#ifndef _WIN32

namespace {

/// The nice value of each priority, in Priority's order.
constexpr int nice_values[] = {19, 10, 0, -7, -14, -20};

}  // namespace

#endif

std::expected<Priority, std::error_code> priority(int pid) {
#ifdef _WIN32
    auto process = Handle::open_process(pid, PROCESS_QUERY_LIMITED_INFORMATION);
    if(!process) {
        return std::unexpected(process.error());
    }
    switch(::GetPriorityClass(process->handle)) {
        case 0: return std::unexpected(fs::windows::last_error());
        case IDLE_PRIORITY_CLASS: return Priority::Idle;
        case BELOW_NORMAL_PRIORITY_CLASS: return Priority::BelowNormal;
        case ABOVE_NORMAL_PRIORITY_CLASS: return Priority::AboveNormal;
        case HIGH_PRIORITY_CLASS: return Priority::High;
        case REALTIME_PRIORITY_CLASS: return Priority::Realtime;
        default: return Priority::Normal;
    }
#else
    // -1 is a priority too.
    errno = 0;
    auto value = ::getpriority(PRIO_PROCESS, static_cast<id_t>(pid));
    if(value == -1 && errno != 0) {
        return std::unexpected(errno_code());
    }
    // The first, from the lowest, whose nice value is at or below it.
    std::size_t index = 0;
    while(nice_values[index] > value && index + 1 < std::size(nice_values)) {
        ++index;
    }
    return static_cast<Priority>(index);
#endif
}

std::error_code set_priority(Priority priority, int pid) {
    // One cast from a number that names none.
    if(static_cast<std::size_t>(priority) > static_cast<std::size_t>(Priority::Realtime)) {
        return std::make_error_code(std::errc::invalid_argument);
    }
#ifdef _WIN32
    constexpr DWORD classes[] = {
        IDLE_PRIORITY_CLASS,
        BELOW_NORMAL_PRIORITY_CLASS,
        NORMAL_PRIORITY_CLASS,
        ABOVE_NORMAL_PRIORITY_CLASS,
        HIGH_PRIORITY_CLASS,
        REALTIME_PRIORITY_CLASS,
    };
    auto process = Handle::open_process(pid, PROCESS_SET_INFORMATION);
    if(!process) {
        return process.error();
    }
    if(!::SetPriorityClass(process->handle, classes[static_cast<std::size_t>(priority)])) {
        return fs::windows::last_error();
    }
    return {};
#else
    auto value = nice_values[static_cast<std::size_t>(priority)];
    if(::setpriority(PRIO_PROCESS, static_cast<id_t>(pid), value) != 0) {
        return errno_code();
    }
    return {};
#endif
}

}  // namespace kota::sys
