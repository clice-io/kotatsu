#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "kotatsu/support/buffer_ref.h"

namespace kotatsu::sys {

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

    /// Linux, and on macOS this process's: the times its threads gave up the
    /// CPU to wait, and the times the scheduler took it away; of another
    /// process on Linux, only those of the threads it still has.
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
    /// level of its memory cgroups on Linux; on Windows of the job object it
    /// is in, not of the jobs that one is nested in, which a process cannot
    /// read.
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

/// The memory this process can take. Memory::available counts the
/// processes in `own` (the caller's workers, say) at their whole
/// ProcessStats::memory. Only macOS needs telling: its compressor keeps no
/// account of whose pages it holds, so without `own` what they compressed
/// counts at its compressed size, and grows back when they touch it.
/// Elsewhere the system counts them so already, and `own` changes nothing.
std::expected<Memory, std::error_code> memory(std::span<const ProcessStats> own = {});

/// The OOM score adjustment of process `pid`, this one for 0: from -1000,
/// which Linux's OOM killer spares, to 1000, which it kills first.
/// not_supported elsewhere, which has no such score.
std::expected<int, std::error_code> oom_score_adjustment(int pid = 0);

/// Sets it. Below the value a privileged process last set, 0 if none,
/// lowering it takes CAP_SYS_RESOURCE. not_supported off Linux.
std::error_code set_oom_score_adjustment(int value, int pid = 0);

/// The CPUs this process can run on at once, at least 1: the least of its
/// affinity and, on Linux, the CPU quota of every level of its cgroups,
/// rounded up. On Windows 11 and Server 2022 across all processor groups,
/// before them the one group a process's threads stay in; less the CPU cap
/// of the job object it is in, a hard cap or a maximum rate. A job nested in another has a share of
/// that one's cap, which a process cannot read: there this counts more CPUs than it gets.
unsigned parallelism();

/// The size of a page of memory.
std::size_t page_size();

/// A logical CPU's time since boot.
struct CPUTimes {
    /// Running programs, at any priority.
    std::chrono::milliseconds user{};

    /// Running the kernel, interrupts included.
    std::chrono::milliseconds system{};

    std::chrono::milliseconds idle{};
};

struct CPUCore {
    /// Its model ("Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz"); empty where
    /// the system names none, as Linux on some ARM machines.
    std::string model;

    CPUTimes times;
};

/// The machine's logical CPUs. On Windows their models come from the
/// registry: linking it imports advapi32.
std::expected<std::vector<CPUCore>, std::error_code> cpu_cores();

/// The operating system.
struct OS {
    /// "Linux", "macOS", "Windows"; elsewhere uname's name.
    std::string name;

    /// Linux's release ("6.8.0-45-generic"), macOS's product version
    /// ("15.7"), Windows's major, minor and build ("10.0.26100").
    std::string version;

    /// "x86_64", "arm64", "aarch64".
    std::string machine;
};

OS os();

/// Writes into `out` the machine's name.
std::error_code hostname(buffer_ref out);

/// The time since the system started.
std::chrono::milliseconds uptime();

/// A priority, from the lowest. A process's is one of Windows's priority
/// classes, and on POSIX one of the nice values 19, 10, 0, -7, -14 and -20; a
/// thread's is one of set_thread_priority's.
enum class Priority : std::uint8_t {
    Idle,
    BelowNormal,
    Normal,
    AboveNormal,
    High,
    Realtime,
};

/// The priority of process `pid`, this one for 0. On POSIX, that of the
/// nice value nearest at or below its own (5 is Normal, 12 BelowNormal).
/// Linux keeps a nice value per thread: this is the main thread's, or for 0
/// the calling thread's.
std::expected<Priority, std::error_code> priority(int pid = 0);

/// Sets it. Raising it takes privileges on POSIX. On Linux only the main
/// thread's, or for 0 the calling thread's, changes; threads started after
/// inherit it. invalid_argument for a value Priority does not name.
std::error_code set_priority(Priority priority, int pid = 0);

/// Sets the priority of the calling thread, the others' left as they are: on
/// Windows its priority within its process's class, THREAD_PRIORITY_IDLE to
/// THREAD_PRIORITY_TIME_CRITICAL; on macOS its quality of service,
/// QOS_CLASS_BACKGROUND to QOS_CLASS_USER_INTERACTIVE, which High and
/// Realtime share; on Linux its nice value, as set_priority(priority) sets
/// it. That value is the priority's own whatever the thread had: on Linux a
/// thread lower already, as in a process started under `nice -n 15`, is
/// raised, which takes privileges and fails without them, unless the caller
/// compares priority() first. invalid_argument for a value Priority does not
/// name.
std::error_code set_thread_priority(Priority priority);

}  // namespace kotatsu::sys
