#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/sys/detail/memory_model.h"

namespace kota::sys::detail {

namespace {

constexpr std::uint64_t MiB = std::uint64_t(1) << 20;
constexpr std::uint64_t GiB = std::uint64_t(1) << 30;

/// Cgroup hierarchies in memory, mounted under /cg.
struct Hierarchy {
    /// What /proc/self/mountinfo reads: v2 at /cg, v1's memory controller at
    /// /cg/memory, and its cpu and cpuacct ones together at /cg/cpu,cpuacct.
    std::string mounts =
        "25 21 0:22 / /cg rw,nosuid,nodev,noexec,relatime shared:4 - cgroup2 cgroup2 "
        "rw,nsdelegate,memory_recursiveprot\n"
        "33 21 0:28 / /cg/memory rw,nosuid,nodev,noexec,relatime shared:15 - cgroup cgroup "
        "rw,memory\n"
        "34 21 0:29 / /cg/cpu,cpuacct rw,nosuid,nodev,noexec,relatime shared:16 - cgroup cgroup "
        "rw,cpu,cpuacct\n";

    std::map<std::string, std::string, std::less<>> files;

    void add(std::string_view path, std::string text) {
        files.insert_or_assign(std::format("/cg/{}", path), std::move(text));
    }

    void add(std::string_view path, std::uint64_t value) {
        add(path, std::format("{}\n", value));
    }

    std::optional<std::string> operator()(std::string_view path) const {
        auto found = files.find(path);
        if(found == files.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    std::optional<cgroup::MemoryLimit> memory(std::string_view membership) const {
        return cgroup::memory(mounts, membership, *this);
    }

    std::optional<double> cpu_quota(std::string_view membership) const {
        return cgroup::cpu_quota(mounts, membership, *this);
    }
};

ZEST_SUITE(sys_detail_memory_model) {

ZEST_CASE(cgroup_v2_takes_the_tightest_level) {
    Hierarchy cg;
    cg.add("user.slice/memory.max", 8 * GiB);
    cg.add("user.slice/memory.current", 6 * GiB);
    cg.add("user.slice/memory.stat",
           std::format("active_file {}\ninactive_file {}\nfile_mapped {}\n", GiB, GiB, 512 * MiB));
    cg.add("user.slice/app/memory.max", "max\n");
    cg.add("user.slice/app/memory.high", 4 * GiB);
    cg.add("user.slice/app/memory.current", 3 * GiB);
    cg.add("user.slice/app/memory.stat",
           std::format("active_file 0\ninactive_file {}\nfile_mapped 0\n", 512 * MiB));

    auto memory = cg.memory("0::/user.slice/app\n");
    ZASSERT(memory.has_value());
    // The child's memory.high is the tighter limit; what it has left is the
    // limit less the usage, plus the file cache nothing maps.
    ZEXPECT(memory->limit == 4 * GiB);
    ZEXPECT(memory->available == GiB + 512 * MiB);
}

ZEST_CASE(cgroup_v2_counts_a_parent_with_less_left) {
    Hierarchy cg;
    cg.add("a/memory.max", 2 * GiB);
    cg.add("a/memory.current", 2 * GiB);
    cg.add("a/b/memory.max", 16 * GiB);
    cg.add("a/b/memory.current", GiB);

    auto memory = cg.memory("0::/a/b\n");
    ZASSERT(memory.has_value());
    ZEXPECT(memory->limit == 2 * GiB);
    ZEXPECT(memory->available == 0U);
}

ZEST_CASE(cgroup_v2_above_its_high_limit_gets_its_cache_back) {
    Hierarchy cg;
    cg.add("a/memory.high", 4 * GiB);
    cg.add("a/memory.current", 6 * GiB);
    cg.add("a/memory.stat", std::format("inactive_file {}\nfile_mapped 0\n", 3 * GiB));

    // Throttled past its limit, the cgroup has the cache back only for the
    // usage above it.
    auto memory = cg.memory("0::/a\n");
    ZASSERT(memory.has_value());
    ZEXPECT(memory->available == GiB);
}

ZEST_CASE(cgroup_v2_without_a_limit_is_unset) {
    Hierarchy cg;
    cg.add("a/memory.max", "max\n");
    cg.add("a/memory.high", "max\n");
    cg.add("a/memory.current", GiB);
    ZEXPECT(!cg.memory("0::/a\n").has_value());
    // The root cgroup, which has no limit files.
    ZEXPECT(!cg.memory("0::/\n").has_value());
}

ZEST_CASE(cgroup_v1_walks_every_level) {
    Hierarchy cg;
    cg.add("memory/docker/memory.limit_in_bytes", 4 * GiB);
    cg.add("memory/docker/memory.usage_in_bytes", 3 * GiB);
    cg.add("memory/docker/memory.stat",
           std::format("total_active_file {}\ntotal_inactive_file {}\ntotal_mapped_file {}\n",
                       GiB,
                       GiB,
                       GiB));
    // Unlimited, as v1 says it.
    cg.add("memory/docker/abc/memory.limit_in_bytes", "9223372036854771712\n");
    cg.add("memory/docker/abc/memory.usage_in_bytes", GiB);

    auto memory = cg.memory("5:cpu:/x\n12:memory:/docker/abc\n");
    ZASSERT(memory.has_value());
    ZEXPECT(memory->limit == 4 * GiB);
    ZEXPECT(memory->available == 2 * GiB);
}

ZEST_CASE(cgroup_v1_finds_a_joined_controller) {
    Hierarchy cg;
    cg.add("memory/a/memory.limit_in_bytes", 2 * GiB);
    cg.add("memory/a/memory.usage_in_bytes", GiB);

    auto memory = cg.memory("4:cpu,memory:/a\n");
    ZASSERT(memory.has_value());
    ZEXPECT(memory->limit == 2 * GiB);
    ZEXPECT(memory->available == GiB);
}

ZEST_CASE(cgroup_v1_reads_a_container_at_the_root) {
    // A container without a cgroup namespace mounts its own cgroup at the
    // hierarchy's root, while /proc/self/cgroup names the path outside.
    Hierarchy cg;
    cg.add("memory/memory.limit_in_bytes", 2 * GiB);
    cg.add("memory/memory.usage_in_bytes", GiB);

    auto memory = cg.memory("12:memory:/docker/abc\n");
    ZASSERT(memory.has_value());
    ZEXPECT(memory->limit == 2 * GiB);
    ZEXPECT(memory->available == GiB);
}

ZEST_CASE(cgroup_v1_without_a_limit_is_unset) {
    Hierarchy cg;
    cg.add("memory/memory.limit_in_bytes", "9223372036854771712\n");
    cg.add("memory/memory.usage_in_bytes", GiB);
    ZEXPECT(!cg.memory("12:memory:/\n").has_value());
}

// Hybrid mode mounts both: the memory controller is v1's.
ZEST_CASE(cgroup_v1_wins_over_an_empty_v2_hierarchy) {
    Hierarchy cg;
    cg.add("memory/a/memory.limit_in_bytes", 2 * GiB);
    cg.add("memory/a/memory.usage_in_bytes", GiB);
    cg.add("b/memory.max", 8 * GiB);

    auto memory = cg.memory("0::/b\n12:memory:/a\n");
    ZASSERT(memory.has_value());
    ZEXPECT(memory->limit == 2 * GiB);
}

ZEST_CASE(cgroup_cpu_quota_takes_the_least_of_every_level) {
    Hierarchy cg;
    cg.add("a/cpu.max", "max 100000\n");
    cg.add("a/b/cpu.max", "150000 100000\n");
    cg.add("cpu.max", "max 100000\n");
    ZEXPECT(cg.cpu_quota("0::/a/b\n") == 1.5);

    cg.add("a/cpu.max", "50000 100000\n");
    ZEXPECT(cg.cpu_quota("0::/a/b\n") == 0.5);
}

ZEST_CASE(cgroup_cpu_quota_reads_v1) {
    Hierarchy cg;
    cg.add("cpu,cpuacct/docker/cpu.cfs_quota_us", "200000\n");
    cg.add("cpu,cpuacct/docker/cpu.cfs_period_us", "100000\n");
    cg.add("cpu,cpuacct/docker/abc/cpu.cfs_quota_us", "-1\n");
    cg.add("cpu,cpuacct/docker/abc/cpu.cfs_period_us", "100000\n");
    ZEXPECT(cg.cpu_quota("3:cpu,cpuacct:/docker/abc\n2:memory:/docker/abc\n") == 2.0);
}

ZEST_CASE(cgroup_cpu_quota_reads_a_container_at_the_root) {
    Hierarchy cg;
    cg.add("cpu,cpuacct/cpu.cfs_quota_us", "50000\n");
    cg.add("cpu,cpuacct/cpu.cfs_period_us", "100000\n");
    ZEXPECT(cg.cpu_quota("3:cpu,cpuacct:/docker/abc\n") == 0.5);
}

ZEST_CASE(cgroup_cpu_quota_without_one_is_unset) {
    Hierarchy cg;
    cg.add("a/cpu.max", "max 100000\n");
    ZEXPECT(!cg.cpu_quota("0::/a\n").has_value());
    // cpuacct and cpuset are not the cpu controller.
    cg.add("cpuacct/cpu.cfs_quota_us", "100000\n");
    cg.add("cpuacct/cpu.cfs_period_us", "100000\n");
    ZEXPECT(!cg.cpu_quota("3:cpuacct,cpuset:/\n").has_value());
    // Neither does a quota without its period.
    cg.add("b/cpu.max", "50000\n");
    ZEXPECT(!cg.cpu_quota("0::/b\n").has_value());
}

// Controllers may share a v1 hierarchy, mounted under any name.
ZEST_CASE(cgroup_v1_finds_a_hierarchy_where_it_is_mounted) {
    Hierarchy cg;
    cg.mounts = "40 21 0:35 / /cg/cpu,memory rw,relatime shared:20 - cgroup hier1 rw,cpu,memory\n";
    cg.add("cpu,memory/a/memory.limit_in_bytes", 2 * GiB);
    cg.add("cpu,memory/a/memory.usage_in_bytes", GiB);
    cg.add("cpu,memory/a/cpu.cfs_quota_us", "50000\n");
    cg.add("cpu,memory/a/cpu.cfs_period_us", "100000\n");

    auto memory = cg.memory("4:cpu,memory:/a\n");
    ZASSERT(memory.has_value());
    ZEXPECT(memory->limit == 2 * GiB);
    ZEXPECT(memory->available == GiB);
    ZEXPECT(cg.cpu_quota("4:cpu,memory:/a\n") == 0.5);
}

// Hybrid mode mounts v2 beside the v1 hierarchies.
ZEST_CASE(cgroup_v2_finds_its_hierarchy_where_it_is_mounted) {
    Hierarchy cg;
    cg.mounts = "30 21 0:26 / /cg/unified rw,relatime shared:5 - cgroup2 cgroup2 rw,nsdelegate\n";
    cg.add("unified/a/memory.max", 2 * GiB);
    cg.add("unified/a/memory.current", GiB);
    cg.add("unified/a/cpu.max", "50000 100000\n");

    auto memory = cg.memory("0::/a\n");
    ZASSERT(memory.has_value());
    ZEXPECT(memory->limit == 2 * GiB);
    ZEXPECT(cg.cpu_quota("0::/a\n") == 0.5);
}

ZEST_CASE(cgroup_mount_point_with_a_blank_is_read) {
    Hierarchy cg;
    // mountinfo writes a blank as an octal escape.
    cg.mounts = R"(30 21 0:26 / /cg/my\040cgroups rw shared:5 - cgroup2 cgroup2 rw)"
                "\n";
    cg.add("my cgroups/a/memory.max", 2 * GiB);

    auto memory = cg.memory("0::/a\n");
    ZASSERT(memory.has_value());
    ZEXPECT(memory->limit == 2 * GiB);
}

ZEST_CASE(cgroup_without_its_hierarchy_mounted_is_unset) {
    Hierarchy cg;
    // cpuacct is not the cpu controller.
    cg.mounts = "34 21 0:29 / /cg/cpuacct rw shared:16 - cgroup cgroup rw,cpuacct\n";
    cg.add("memory/a/memory.limit_in_bytes", 2 * GiB);
    cg.add("memory/a/memory.usage_in_bytes", GiB);
    cg.add("cpuacct/a/cpu.cfs_quota_us", "50000\n");
    cg.add("cpuacct/a/cpu.cfs_period_us", "100000\n");
    cg.add("a/memory.max", 2 * GiB);
    ZEXPECT(!cg.memory("12:memory:/a\n").has_value());
    ZEXPECT(!cg.cpu_quota("3:cpu:/a\n").has_value());
    ZEXPECT(!cg.memory("0::/a\n").has_value());
}

ZEST_CASE(mac_available_leaves_out_file_pages) {
    // A process mapping a 1 GiB file: resident 2 GiB, 1 GiB of it
    // anonymous, which the machine's anonymous count holds.
    MacMemory host{.total = 16 * GiB, .wired = 2 * GiB, .anonymous = 5 * GiB};
    std::vector<ProcessStats> own{
        {.memory = GiB, .resident = 2 * GiB}
    };
    ZEXPECT(mac_available(host, own) == 9 * GiB);
}

ZEST_CASE(mac_available_takes_reusable_pages_as_free) {
    // A process whose allocator gave 1 GiB back as reusable: resident 2 GiB,
    // its footprint 1 GiB; the machine's anonymous count holds both.
    MacMemory host{.total = 16 * GiB, .wired = 2 * GiB, .anonymous = 5 * GiB, .reusable = GiB};
    std::vector<ProcessStats> own{
        {.memory = GiB, .resident = 2 * GiB}
    };
    ZEXPECT(mac_available(host, own) == 10 * GiB);
}

ZEST_CASE(mac_available_counts_compressed_pages_at_their_footprint) {
    // 2 GiB of the process's 3 compressed fourfold: the compressor holds
    // 512 MiB of it.
    MacMemory host{
        .total = 16 * GiB,
        .wired = 2 * GiB,
        .anonymous = 4 * GiB,
        .compressor = GiB,
        .compressed = 4 * GiB,
    };
    std::vector<ProcessStats> own{
        {.memory = 3 * GiB, .resident = GiB}
    };
    ZEXPECT(mac_available(host, own) == 7 * GiB + 512 * MiB);
    // Counting none, the compressor's pages are the system's.
    ZEXPECT(mac_available(host, {}) == 9 * GiB);
}

ZEST_CASE(mac_available_without_a_compression_ratio_takes_none) {
    // The two counts are read apart: the compressor may hold pages it says
    // hold nothing.
    MacMemory host{.total = 16 * GiB, .wired = 2 * GiB, .anonymous = 4 * GiB, .compressor = GiB};
    std::vector<ProcessStats> own{
        {.memory = 3 * GiB, .resident = GiB}
    };
    ZEXPECT(mac_available(host, own) == 9 * GiB);
}

};  // ZEST_SUITE(sys_detail_memory_model)

}  // namespace

}  // namespace kota::sys::detail
