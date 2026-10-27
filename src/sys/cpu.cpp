#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>

#include "os.h"
#include "proc.h"
#include "kota/sys/detail/memory_model.h"
#include "kota/sys/info.h"

#ifdef __linux__
#include <sched.h>
#endif

namespace kota::sys {

unsigned parallelism() {
#ifdef _WIN32
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    // No mask for a process with threads in several groups.
    ::GetProcessAffinityMask(::GetCurrentProcess(), &process_mask, &system_mask);
    // Windows 11 and Server 2022 spread a process's threads over every group;
    // before, they stay in the group of its first.
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    ::RtlGetVersion(&version);
    bool spread =
        version.dwBuildNumber >= 20348 && (process_mask == 0 || process_mask == system_mask);
    unsigned count = spread ? ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)
                            : static_cast<unsigned>(std::popcount(process_mask));
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
    unsigned count = 0;
    // EINVAL until the set is as large as the kernel's, which may count past
    // CPU_SETSIZE.
    for(std::size_t cpus = CPU_SETSIZE;; cpus *= 2) {
        auto* set = CPU_ALLOC(cpus);
        auto size = CPU_ALLOC_SIZE(cpus);
        bool read = ::sched_getaffinity(0, size, set) == 0;
        auto error = errno;
        if(read) {
            count = static_cast<unsigned>(CPU_COUNT_S(size, set));
        }
        CPU_FREE(set);
        if(read) {
            break;
        }
        // A sandbox may deny the call: the CPUs online then.
        if(error != EINVAL) {
            count = static_cast<unsigned>(::sysconf(_SC_NPROCESSORS_ONLN));
            break;
        }
    }
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

std::size_t page_size() {
#ifdef _WIN32
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    return info.dwPageSize;
#else
    return static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
#endif
}

}  // namespace kota::sys
