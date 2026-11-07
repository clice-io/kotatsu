#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "os.h"
#include "proc.h"
#include "kotatsu/support/string_ref.h"
#include "kotatsu/sys/detail/memory_model.h"
#include "kotatsu/sys/info.h"

#ifdef __linux__
#include <format>
#include <sched.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace kotatsu::sys {

namespace {

#ifdef _WIN32

/// The CPUs this process runs on: all of the machine's, or `mask` of the
/// processor group `group`.
struct Affinity {
    bool spread = false;

    DWORD_PTR mask = 0;

    USHORT group = 0;
};

Affinity affinity() {
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    // No mask for a process with threads in several groups.
    ::GetProcessAffinityMask(::GetCurrentProcess(), &process_mask, &system_mask);
    // Windows 11 and Server 2022 spread a process's threads over every group;
    // before, they stay in the group of its first.
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    ::RtlGetVersion(&version);
    Affinity affinity{
        .spread =
            version.dwBuildNumber >= 20348 && (process_mask == 0 || process_mask == system_mask),
        .mask = process_mask,
    };
    USHORT count = 1;
    ::GetProcessGroupAffinity(::GetCurrentProcess(), &count, &affinity.group);
    return affinity;
}

#elif defined(__linux__)

/// The CPUs this process may run on, in ascending order: those of its
/// affinity, or the CPUs online where a sandbox denies the call that reads
/// it.
std::vector<unsigned> affinity() {
    std::vector<unsigned> cpus;
    // EINVAL until the set is as large as the kernel's, which may count past
    // CPU_SETSIZE.
    for(std::size_t count = CPU_SETSIZE;; count *= 2) {
        auto* set = CPU_ALLOC(count);
        auto size = CPU_ALLOC_SIZE(count);
        bool read = ::sched_getaffinity(0, size, set) == 0;
        auto error = errno;
        for(unsigned cpu = 0; read && cpu < count; ++cpu) {
            if(CPU_ISSET_S(cpu, size, set)) {
                cpus.push_back(cpu);
            }
        }
        CPU_FREE(set);
        if(read) {
            return cpus;
        }
        if(error != EINVAL) {
            break;
        }
    }
    auto online = static_cast<unsigned>(::sysconf(_SC_NPROCESSORS_ONLN));
    for(unsigned cpu = 0; cpu < online; ++cpu) {
        cpus.push_back(cpu);
    }
    return cpus;
}

#endif

}  // namespace

unsigned parallelism() {
#ifdef _WIN32
    auto own = affinity();
    unsigned count = own.spread ? ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)
                                : static_cast<unsigned>(std::popcount(own.mask));
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION rate{};
    // Fails when the process is in no job.
    if(::QueryInformationJobObject(nullptr,
                                   JobObjectCpuRateControlInformation,
                                   &rate,
                                   sizeof(rate),
                                   nullptr) &&
       (rate.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_ENABLE) &&
       (rate.ControlFlags &
        (JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP | JOB_OBJECT_CPU_RATE_CONTROL_MIN_MAX_RATE))) {
        // A share of all the machine's CPUs, in hundredths of a percent, past
        // which the job's threads wait: a hard cap's rate, or of a minimum and
        // a maximum rate the maximum, the high half, which MinGW leaves
        // unnamed.
        const DWORD cap = (rate.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP)
                              ? rate.CpuRate
                              : rate.CpuRate >> 16;
        auto cpus = std::ceil(::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) * cap / 10000.0);
        count = std::min(count, static_cast<unsigned>(cpus));
    }
    return std::max(count, 1u);
#elif defined(__APPLE__)
    return static_cast<unsigned>(std::max(::sysconf(_SC_NPROCESSORS_ONLN), 1L));
#else
    auto count = static_cast<unsigned>(affinity().size());
    ProcText membership;
    ProcText mounts;
    if(!fs::read_file(membership, "/proc/self/cgroup") &&
       !fs::read_file(mounts, "/proc/self/mountinfo")) {
        // A quota may count more CPUs than any machine has.
        auto quota = detail::cgroup::cpu_quota(mounts, membership, read_cgroup_file);
        if(quota && *quota < count) {
            count = static_cast<unsigned>(std::ceil(*quota));
        }
    }
    return std::max(count, 1u);
#endif
}

unsigned physical_cores() {
#ifdef _WIN32
    auto own = affinity();
    std::vector<char> buffer;
    DWORD size = 0;
    // Asked again while the cores outgrow what the last call said, as a CPU
    // added between the calls makes them.
    while(!::GetLogicalProcessorInformationEx(
        RelationProcessorCore,
        reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()),
        &size)) {
        buffer.resize(size);
    }
    unsigned count = 0;
    for(DWORD at = 0; at < size;) {
        auto* core = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + at);
        // A core's CPUs are in one group.
        auto& cpus = core->Processor.GroupMask[0];
        if(own.spread || (cpus.Group == own.group && (cpus.Mask & own.mask) != 0)) {
            ++count;
        }
        at += core->Size;
    }
    return std::max(count, 1u);
#elif defined(__APPLE__)
    int count = 0;
    std::size_t size = sizeof(count);
    ::sysctlbyname("hw.physicalcpu", &count, &size, nullptr, 0);
    return static_cast<unsigned>(std::max(count, 1));
#else
    auto cpus = affinity();
    auto siblings = [](unsigned cpu) -> std::optional<std::string> {
        std::string text;
        auto path = std::format("/sys/devices/system/cpu/cpu{}/topology/thread_siblings_list", cpu);
        if(fs::read_file(text, path)) {
            return std::nullopt;
        }
        return text;
    };
    // The first of `cpus` counts.
    return detail::count_physical_cores(cpus, siblings);
#endif
}

unsigned
    detail::count_physical_cores(std::span<const unsigned> cpus,
                                 function_ref<std::optional<std::string>(unsigned cpu)> siblings) {
    unsigned count = 0;
    for(auto cpu: cpus) {
        auto listed = siblings(cpu).value_or("");
        std::string_view items = string_ref(listed).trim();
        // Whether a CPU before this one of `cpus` shares its core, and was
        // counted for it.
        bool shared = false;
        for(auto range: std::views::split(items, ',')) {
            std::string_view item(range.begin(), range.end());
            auto dash = item.find('-');
            auto first = leading_number(item.substr(0, dash));
            auto last =
                dash == std::string_view::npos ? first : leading_number(item.substr(dash + 1));
            auto before = std::ranges::lower_bound(cpus, first.value_or(cpu));
            if(first && last && before != cpus.end() && *before < cpu && *before <= *last) {
                shared = true;
                break;
            }
        }
        if(!shared) {
            ++count;
        }
    }
    return count;
}

std::size_t page_size() {
#ifdef _WIN32
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    return info.dwPageSize;
#else
    return static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
#endif
}

}  // namespace kotatsu::sys
