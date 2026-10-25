#pragma once

// The arithmetic of memory() and parallelism() on figures given rather than
// read: Linux's cgroups through a reader of their files, macOS's counts as
// numbers, so that a test's fixture stands in for the system on any host.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "kota/support/functional.h"
#include "kota/sys/info.h"

namespace kota::sys::detail {

/// The memory cgroups of Linux and their CPU quotas, read through a reader
/// of the hierarchy's files.
namespace cgroup {

/// The text of the file at `path`; unset when it cannot be read.
using Reader = function_ref<std::optional<std::string>(std::string_view path)>;

/// The least limit of a process's memory cgroups, and the least that a
/// level with a limit has left: the limit less the usage, plus the file
/// cache no process maps, which the kernel reclaims without a fault back.
struct MemoryLimit {
    std::uint64_t limit = 0;

    std::uint64_t available = 0;
};

/// The memory cgroups of a process whose /proc/<pid>/cgroup reads
/// `membership` and /proc/<pid>/mountinfo `mounts`: every level up to the
/// root of the mount that holds its cgroup, the first of the hierarchy whose
/// root is the cgroup or above it, under cgroup v2 (memory.max, memory.high)
/// or v1 (the memory controller's memory.limit_in_bytes). Unset when no level
/// has a limit, or no mount holds the cgroup.
std::optional<MemoryLimit> memory(std::string_view mounts,
                                  std::string_view membership,
                                  Reader read);

/// The least CPU quota, in CPUs, of every level of those cgroups: v2's
/// cpu.max, v1's cpu.cfs_quota_us over cpu.cfs_period_us. Unset when no
/// level has one, or no mount holds the cgroup.
std::optional<double> cpu_quota(std::string_view mounts, std::string_view membership, Reader read);

}  // namespace cgroup

/// The machine's memory as macOS counts it, in bytes.
struct MacMemory {
    std::uint64_t total = 0;

    std::uint64_t wired = 0;

    /// The anonymous pages in memory.
    std::uint64_t anonymous = 0;

    /// The anonymous pages processes gave back as reusable: macOS takes them
    /// without compressing them, and no footprint counts them.
    std::uint64_t reusable = 0;

    /// The compressor's pages, and what they hold before compression.
    std::uint64_t compressor = 0;

    std::uint64_t compressed = 0;
};

/// Memory::available on macOS, from the machine's counts and the processes
/// `own` at their footprint.
std::uint64_t mac_available(const MacMemory& host, std::span<const ProcessStats> own);

}  // namespace kota::sys::detail
