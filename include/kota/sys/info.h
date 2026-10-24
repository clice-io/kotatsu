#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace kota::sys {

/// What a process uses.
struct ProcessStats {
    /// Its own memory: on Linux its anonymous pages, in memory or swapped out;
    /// on macOS its footprint, compressed pages counted at their size before
    /// compression; on Windows its private bytes. File mappings, of its
    /// executable among them, are left out.
    std::uint64_t memory = 0;

    /// The most it has held: on Linux the peak of its resident size, file
    /// pages included (VmHWM); on macOS the peak of its footprint; on Windows
    /// the peak of its private bytes.
    std::uint64_t peak_memory = 0;

    /// Its pages in memory, file pages included.
    std::uint64_t resident = 0;

    /// Linux: the part of `memory` swapped out.
    std::uint64_t swapped = 0;

    std::chrono::microseconds user_time{};

    std::chrono::microseconds system_time{};

    /// Linux and macOS: page faults served without I/O, and with it.
    std::uint64_t minor_faults = 0;

    std::uint64_t major_faults = 0;

    /// Linux, and on macOS this process's: the times it gave up the CPU to
    /// wait, and the times the scheduler took it away.
    std::uint64_t voluntary_switches = 0;

    std::uint64_t involuntary_switches = 0;
};

/// The usage of process `pid`, this one for 0. no_such_process when there is
/// none.
std::expected<ProcessStats, std::error_code> process_stats(int pid = 0);

/// This process's ProcessStats::resident, read more cheaply.
std::expected<std::uint64_t, std::error_code> resident_memory();

/// The memory a process can take.
struct Memory {
    /// The physical memory.
    std::uint64_t total = 0;

    /// The least of `total` and the limits the process runs under: of every
    /// level of its memory cgroups on Linux, of its job object on Windows.
    std::uint64_t limit = 0;

    /// What it can take before reclaim starts to hurt. On Linux the least of
    /// MemAvailable and, at each level of its memory cgroups with a limit,
    /// the limit less the usage plus the file cache no process maps. On macOS
    /// the physical memory less what the system holds otherwise (wired,
    /// anonymous but not reusable, and compressed). On Windows the least of
    /// the available physical memory and the commit charge left, and no more
    /// than `limit`: a job's usage is not counted.
    std::uint64_t available = 0;
};

/// The memory this process can take (the arithmetic is memory_model.h's).
/// On macOS the processes `counted` (the
/// caller's workers, say) are taken at their footprint, rather than at the
/// share of the compressor their pages hold, which macOS does not tell:
/// what they compressed is counted at the size they would take back.
/// Elsewhere `counted` changes nothing.
std::expected<Memory, std::error_code> memory(std::span<const ProcessStats> counted = {});

#ifdef __linux__
/// The OOM score adjustment of process `pid`, this one for 0: from -1000,
/// which the OOM killer spares, to 1000, which it kills first.
std::expected<int, std::error_code> oom_score_adjustment(int pid = 0);

/// Sets it. Below the value a privileged process last set, 0 if none,
/// lowering it takes CAP_SYS_RESOURCE.
std::error_code set_oom_score_adjustment(int value, int pid = 0);
#endif

/// The CPUs this process can run on at once, at least 1: the least of its
/// affinity and, on Linux, the CPU quota of every level of its cgroups,
/// rounded up. On Windows 11 and Server 2022 across all processor groups,
/// before them the one group a process's threads stay in; less a job
/// object's hard cap.
unsigned parallelism();

/// The size of a page of memory.
std::size_t page_size();

/// A logical CPU's time since boot.
struct CpuTimes {
    std::chrono::milliseconds user{};

    std::chrono::milliseconds nice{};

    std::chrono::milliseconds system{};

    std::chrono::milliseconds idle{};

    std::chrono::milliseconds irq{};
};

struct CpuCore {
    /// Its model ("Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz").
    std::string model;

    /// Its clock speed; 0 where the system does not say, as on Apple silicon
    /// and in some virtual machines.
    int speed_mhz = 0;

    CpuTimes times;
};

/// The machine's logical CPUs. On Windows their models and speeds come
/// from the registry: linking it imports advapi32.
std::expected<std::vector<CpuCore>, std::error_code> cpu_cores();

struct Uname {
    /// "Linux", "Darwin", "Windows_NT".
    std::string sysname;

    std::string release;

    std::string version;

    /// "x86_64", "arm64", "aarch64".
    std::string machine;
};

/// The system's name and version. On Windows the product's name comes from
/// the registry: linking it imports advapi32.
Uname uname();

std::expected<std::string, std::error_code> hostname();

/// The time since the system started.
std::chrono::milliseconds uptime();

/// Priorities, nice values from the highest to the lowest: what POSIX takes
/// as they are and Windows maps to its priority classes, from realtime to
/// idle.
constexpr inline int priority_highest = -20;
constexpr inline int priority_high = -14;
constexpr inline int priority_above_normal = -7;
constexpr inline int priority_normal = 0;
constexpr inline int priority_below_normal = 10;
constexpr inline int priority_low = 19;

/// The priority of process `pid`, this one for 0: its nice value on POSIX,
/// the constant of its priority class on Windows. Linux keeps a nice value
/// per thread: this is the main thread's, or for 0 the calling thread's.
std::expected<int, std::error_code> priority(int pid = 0);

/// Sets it: a nice value from -20 to 19, which Windows maps to the class of
/// the nearest constant at or below it (-10 to HIGH, as -14).
/// invalid_argument past that range. On Linux only the main thread's, or
/// for 0 the calling thread's, changes; threads started after inherit it.
std::error_code set_priority(int value, int pid = 0);

}  // namespace kota::sys
