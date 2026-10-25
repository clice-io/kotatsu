#include <algorithm>
#include <charconv>
#include <cstdint>
#include <span>
#include <string>

#include "os.h"
#include "proc.h"
#include "kota/support/small_string.h"
#include "kota/sys/detail/memory_model.h"
#include "kota/sys/info.h"

#ifdef _WIN32
// After windows.h, whose types it uses.
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#ifdef __APPLE__
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/sysctl.h>
#elif !defined(_WIN32)
#include "kota/fs/directory.h"
#endif

namespace kota::sys {

namespace {

#ifndef _WIN32

/// Sets the switches of `stats` to this process's, those of the threads that
/// ended included.
void count_own_switches(ProcessStats& stats) {
    rusage own{};
    ::getrusage(RUSAGE_SELF, &own);
    stats.voluntary_switches = static_cast<std::uint64_t>(own.ru_nvcsw);
    stats.involuntary_switches = static_cast<std::uint64_t>(own.ru_nivcsw);
}

#endif

#ifdef _WIN32

/// A FILETIME span, in 100 ns units.
std::chrono::microseconds microseconds(FILETIME time) {
    auto ticks = (std::uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    return std::chrono::microseconds(ticks / 10);
}

#elif defined(__APPLE__)

/// A span in mach absolute time units.
std::chrono::microseconds microseconds(std::uint64_t ticks) {
    mach_timebase_info_data_t base{};
    ::mach_timebase_info(&base);
    return std::chrono::microseconds(ticks * base.numer / base.denom / 1000);
}

template <typename T>
std::error_code read_sysctl(const char* name, T& value) {
    auto size = sizeof(value);
    if(::sysctlbyname(name, &value, &size, nullptr, 0) != 0) {
        return errno_code();
    }
    return {};
}

#else

/// An error reading a process's /proc file: a missing one means no process.
std::error_code process_error(std::error_code error) {
    if(error == std::errc::no_such_file_or_directory) {
        return std::make_error_code(std::errc::no_such_process);
    }
    return error;
}

/// Adds to `stats` the switches of the threads process `pid` has, which
/// Linux counts each apart.
std::error_code count_thread_switches(ProcessStats& stats, int pid) {
    small_string<64> dir;
    proc_path(dir, pid, "task");
    auto threads = fs::read_dir(dir);
    if(!threads) {
        return process_error(threads.error());
    }
    for(const auto& thread: *threads) {
        small_string<64> path{dir, "/", thread.name, "/status"};
        ProcText status;
        // A thread may end before it is read.
        if(fs::read_file(status, path)) {
            continue;
        }
        stats.voluntary_switches += field(status, "voluntary_ctxt_switches:").value_or(0);
        stats.involuntary_switches += field(status, "nonvoluntary_ctxt_switches:").value_or(0);
    }
    return {};
}

#endif

}  // namespace

std::expected<ProcessStats, std::error_code> process_stats(int pid) {
    ProcessStats stats;
#ifdef _WIN32
    auto process = Handle::open_process(pid, PROCESS_QUERY_LIMITED_INFORMATION);
    if(!process) {
        return std::unexpected(process.error());
    }
    PROCESS_MEMORY_COUNTERS_EX counters{};
    FILETIME created, exited, kernel, user;
    if(!::GetProcessMemoryInfo(process->handle,
                               reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                               sizeof(counters)) ||
       !::GetProcessTimes(process->handle, &created, &exited, &kernel, &user)) {
        return std::unexpected(fs::windows::last_error());
    }
    stats.memory = counters.PrivateUsage;
    stats.peak_memory = counters.PeakPagefileUsage;
    stats.resident = counters.WorkingSetSize;
    stats.user_time = microseconds(user);
    stats.system_time = microseconds(kernel);
#elif defined(__APPLE__)
    auto target = pid == 0 ? ::getpid() : pid;
    rusage_info_v4 usage{};
    proc_taskinfo task{};
    if(::proc_pid_rusage(target, RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&usage)) != 0) {
        return std::unexpected(errno_code());
    }
    if(::proc_pidinfo(target, PROC_PIDTASKINFO, 0, &task, sizeof(task)) != sizeof(task)) {
        return std::unexpected(errno_code());
    }
    stats.memory = usage.ri_phys_footprint;
    stats.peak_memory = usage.ri_lifetime_max_phys_footprint;
    stats.resident = usage.ri_resident_size;
    stats.user_time = microseconds(usage.ri_user_time);
    stats.system_time = microseconds(usage.ri_system_time);
    // Counted in 32 bits, which wrap.
    stats.minor_faults = std::uint32_t(task.pti_faults) - std::uint32_t(task.pti_pageins);
    stats.major_faults = std::uint32_t(task.pti_pageins);
    // Only a process's own switches are told apart.
    if(target == ::getpid()) {
        count_own_switches(stats);
    }
#else
    ProcText status;
    if(auto error = read_proc(status, pid, "status")) {
        return std::unexpected(process_error(error));
    }
    ProcText stat;
    if(auto error = read_proc(stat, pid, "stat")) {
        return std::unexpected(process_error(error));
    }
    auto kilobytes = [&](std::string_view key) {
        return field(status, key).value_or(0) * 1024;
    };
    stats.swapped = kilobytes("VmSwap:");
    stats.memory = kilobytes("RssAnon:") + stats.swapped;
    stats.peak_memory = kilobytes("VmHWM:");
    stats.resident = kilobytes("VmRSS:");
    // Of another process, only the threads it has are there to count.
    if(pid == 0 || pid == ::getpid()) {
        count_own_switches(stats);
    } else if(auto error = count_thread_switches(stats, pid)) {
        return std::unexpected(error);
    }
    auto number = [&](std::size_t index) {
        return leading_number(stat_field(stat, index)).value_or(0);
    };
    auto ticks = static_cast<std::uint64_t>(::sysconf(_SC_CLK_TCK));
    stats.minor_faults = number(10);
    stats.major_faults = number(12);
    stats.user_time = std::chrono::microseconds(number(14) * 1'000'000 / ticks);
    stats.system_time = std::chrono::microseconds(number(15) * 1'000'000 / ticks);
#endif
    return stats;
}

std::expected<std::uint64_t, std::error_code> resident_memory() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if(!::GetProcessMemoryInfo(::GetCurrentProcess(), &counters, sizeof(counters))) {
        return std::unexpected(fs::windows::last_error());
    }
    return counters.WorkingSetSize;
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    // Of this very task, it cannot fail.
    ::task_info(::mach_task_self(),
                MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&info),
                &count);
    return info.resident_size;
#else
    // "<size> <resident> ...", in pages.
    ProcText statm;
    if(auto error = fs::read_file(statm, "/proc/self/statm")) {
        return std::unexpected(error);
    }
    std::string_view fields = statm;
    auto pages = leading_number(fields.substr(std::min(fields.find(' '), fields.size())));
    return pages.value_or(0) * page_size();
#endif
}

std::uint64_t detail::mac_available(const MacMemory& host, std::span<const ProcessStats> own) {
    // Neither the free count nor the inactive one tells what the processes
    // can still take: macOS compresses their memory long before it frees
    // any. What the rest of the system holds is the wired, anonymous and
    // compressed memory less the own processes' anonymous pages and
    // their share of the compressor, at its current ratio. A resident size
    // counts the file pages and the reusable ones too, which a footprint
    // leaves out, and a footprint the compressed pages, at their size before
    // compression.
    // Without both counts, as when the compressor is empty, nothing is
    // compressed: the ratio is 1.
    auto ratio = host.compressor > 0 && host.compressed > 0
                     ? static_cast<double>(host.compressed) / static_cast<double>(host.compressor)
                     : 1.0;
    std::uint64_t ours = 0;
    std::uint64_t footprint = 0;
    for(const auto& process: own) {
        auto anonymous = std::min(process.resident, process.memory);
        ours += anonymous +
                static_cast<std::uint64_t>(static_cast<double>(process.memory - anonymous) / ratio);
        footprint += process.memory;
    }
    // The reusable count is read apart from the anonymous one that holds it.
    auto in_use = host.anonymous - std::min(host.anonymous, host.reusable);
    auto held = host.wired + in_use + host.compressor;
    auto others = held > ours ? held - ours : 0;
    auto taken = others + footprint;
    return host.total > taken ? host.total - taken : 0;
}

std::expected<Memory, std::error_code> memory([[maybe_unused]] std::span<const ProcessStats> own) {
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if(!::GlobalMemoryStatusEx(&status)) {
        return std::unexpected(fs::windows::last_error());
    }
    Memory result{
        .total = status.ullTotalPhys,
        .limit = status.ullTotalPhys,
        .available = std::min(status.ullAvailPhys, status.ullAvailPageFile),
    };
    // Fails when the process is in no job.
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job{};
    if(::QueryInformationJobObject(nullptr,
                                   JobObjectExtendedLimitInformation,
                                   &job,
                                   sizeof(job),
                                   nullptr)) {
        auto flags = job.BasicLimitInformation.LimitFlags;
        if(flags & JOB_OBJECT_LIMIT_JOB_MEMORY) {
            result.limit = std::min<std::uint64_t>(result.limit, job.JobMemoryLimit);
        }
        if(flags & JOB_OBJECT_LIMIT_PROCESS_MEMORY) {
            result.limit = std::min<std::uint64_t>(result.limit, job.ProcessMemoryLimit);
        }
    }
    result.available = std::min(result.available, result.limit);
    return result;
#elif defined(__APPLE__)
    std::uint64_t total = 0;
    std::uint32_t reusable = 0;
    if(auto error = read_sysctl("hw.memsize", total)) {
        return std::unexpected(error);
    }
    if(auto error = read_sysctl("vm.page_reusable_count", reusable)) {
        return std::unexpected(error);
    }
    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_size_t page = 0;
    // Of this very host, they cannot fail. Each mach_host_self() adds a
    // reference to the right, given back after.
    auto host = ::mach_host_self();
    ::host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count);
    ::host_page_size(host, &page);
    ::mach_port_deallocate(::mach_task_self(), host);
    detail::MacMemory counts{
        .total = total,
        .wired = std::uint64_t(vm.wire_count) * page,
        .anonymous = std::uint64_t(vm.internal_page_count) * page,
        .reusable = std::uint64_t(reusable) * page,
        .compressor = std::uint64_t(vm.compressor_page_count) * page,
        .compressed = std::uint64_t(vm.total_uncompressed_pages_in_compressor) * page,
    };
    return Memory{.total = total, .limit = total, .available = detail::mac_available(counts, own)};
#else
    ProcText meminfo;
    if(auto error = fs::read_file(meminfo, "/proc/meminfo")) {
        return std::unexpected(error);
    }
    auto total = field(meminfo, "MemTotal:").value_or(0) * 1024;
    Memory result{
        .total = total,
        .limit = total,
        .available = field(meminfo, "MemAvailable:").value_or(0) * 1024,
    };
    // Read afresh every time: the process may have moved.
    ProcText membership;
    ProcText mounts;
    if(!fs::read_file(membership, "/proc/self/cgroup") &&
       !fs::read_file(mounts, "/proc/self/mountinfo")) {
        if(auto cgroups = detail::cgroup::memory(mounts, membership, read_cgroup_file)) {
            result.limit = std::min(result.limit, cgroups->limit);
            result.available = std::min(result.available, cgroups->available);
        }
    }
    return result;
#endif
}

std::expected<int, std::error_code> oom_score_adjustment([[maybe_unused]] int pid) {
#ifndef __linux__
    return std::unexpected(std::make_error_code(std::errc::not_supported));
#else
    small_string<16> text;
    if(auto error = read_proc(text, pid, "oom_score_adj")) {
        return std::unexpected(process_error(error));
    }
    int value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
#endif
}

std::error_code set_oom_score_adjustment([[maybe_unused]] int value, [[maybe_unused]] int pid) {
#ifndef __linux__
    return std::make_error_code(std::errc::not_supported);
#else
    small_string<64> path;
    proc_path(path, pid, "oom_score_adj");
    char digits[16];
    auto end = std::to_chars(digits, digits + sizeof(digits), value).ptr;
    return process_error(
        fs::write_file(path, std::string_view(digits, static_cast<std::size_t>(end - digits))));
#endif
}

}  // namespace kota::sys
