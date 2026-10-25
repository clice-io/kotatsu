#include <algorithm>
#include <cstddef>
#include <system_error>

#ifdef __linux__
#include <sched.h>
#endif

#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/sys/info.h"
#include "kota/sys/process.h"

namespace kota::sys {

namespace {

// A pid no process has: above the largest Linux allows (2^22) and macOS
// uses, and not a multiple of four, which every Windows pid is.
constexpr int missing_pid = 999'999'999;

ZEST_SUITE(sys_info) {

ZEST_CASE(memory_figures_are_consistent) {
    auto memory = sys::memory();
    ZASSERT(memory.has_value());
    ZEXPECT(memory->total > 0U);
    ZEXPECT(memory->limit <= memory->total);
    ZEXPECT(memory->limit > 0U);
    ZEXPECT(memory->available <= memory->total);
}

ZEST_CASE(process_stats_describe_this_process) {
    auto self = process_stats();
    ZASSERT(self.has_value());
    ZEXPECT(self->memory > 0U);
    ZEXPECT(self->resident > 0U);
    ZEXPECT(self->peak_memory > 0U);

    auto by_pid = process_stats(pid());
    ZASSERT(by_pid.has_value());
    ZEXPECT(by_pid->resident > 0U);
}

ZEST_CASE(process_stats_of_a_missing_pid_fails) {
    auto stats = process_stats(missing_pid);
    ZASSERT(!stats.has_value());
    ZEXPECT(stats.error() == std::errc::no_such_process);
}

ZEST_CASE(resident_memory_is_reported) {
    auto resident = resident_memory();
    ZASSERT(resident.has_value());
    ZEXPECT(*resident > 0U);
}

ZEST_CASE(parallelism_is_at_most_the_cores) {
    auto cores = cpu_cores();
    ZASSERT(cores.has_value());
    ZEXPECT(!cores->empty());
    ZEXPECT(parallelism() >= 1U);
    ZEXPECT(parallelism() <= cores->size());
}

ZEST_CASE(cpu_cores_have_run) {
    auto cores = cpu_cores();
    ZASSERT(cores.has_value());
    for(std::size_t i = 0; i < cores->size(); ++i) {
        ZEST_CONTEXT("core {}", i);
        const auto& core = (*cores)[i];
        ZEXPECT((core.times.user + core.times.system + core.times.idle).count() > 0);
        // Virtual machines and Apple silicon may report no clock speed.
        ZEXPECT(core.speed_mhz >= 0);
#if defined(_WIN32) || defined(__APPLE__) || defined(__x86_64__)
        // Linux kernels for ARM name no model.
        ZEXPECT(!core.model.empty());
#endif
    }
}

#ifdef __linux__
/// The calling thread's CPU affinity, put back when this goes.
struct ScopedAffinity {
    ScopedAffinity() {
        ZASSERT(::sched_getaffinity(0, sizeof(original), &original) == 0);
    }

    ScopedAffinity(const ScopedAffinity&) = delete;
    ScopedAffinity& operator=(const ScopedAffinity&) = delete;

    ~ScopedAffinity() {
        ::sched_setaffinity(0, sizeof(original), &original);
    }

    cpu_set_t original{};
};

// Linux keeps the affinity per thread, and parallelism() reads the caller's.
ZEST_CASE(parallelism_follows_the_affinity) {
    ScopedAffinity affinity;
    cpu_set_t one;
    CPU_ZERO(&one);
    for(int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if(CPU_ISSET(cpu, &affinity.original)) {
            CPU_SET(cpu, &one);
            break;
        }
    }
    ZASSERT(::sched_setaffinity(0, sizeof(one), &one) == 0);
    ZEXPECT(parallelism() == 1U);
}
#endif

ZEST_CASE(page_size_is_a_power_of_two) {
    auto size = page_size();
    ZEXPECT(size >= 4096U);
    ZEXPECT((size & (size - 1)) == 0U);
}

ZEST_CASE(uname_names_the_system) {
    auto name = uname();
    ZEXPECT(!name.sysname.empty());
    ZEXPECT(!name.release.empty());
    ZEXPECT(!name.machine.empty());
#ifdef _WIN32
    ZEXPECT(name.sysname == "Windows_NT");
#elif defined(__APPLE__)
    ZEXPECT(name.sysname == "Darwin");
#else
    ZEXPECT(name.sysname == "Linux");
#endif
}

ZEST_CASE(hostname_and_uptime_are_reported) {
    auto host = hostname();
    ZASSERT(host.has_value());
    ZEXPECT(!host->empty());
    ZEXPECT(uptime().count() > 0);
}

// Setting the priority it already has leaves the process as it was.
ZEST_CASE(priority_is_read_and_set) {
    auto original = priority();
    ZASSERT(original.has_value());
    ZEXPECT(!set_priority(*original));
    ZEXPECT(priority() == *original);
}

ZEST_CASE(priority_of_a_missing_pid_fails) {
    auto read = priority(missing_pid);
    ZASSERT(!read.has_value());
    ZEXPECT(read.error() == std::errc::no_such_process);
    ZEXPECT(set_priority(priority_normal, missing_pid) == std::errc::no_such_process);
}

#ifdef _WIN32
/// This process's priority, put back when this goes: Windows lets a process
/// raise its own again.
struct ScopedPriority {
    ScopedPriority() {
        auto read = priority();
        ZASSERT(read.has_value());
        original = *read;
    }

    ScopedPriority(const ScopedPriority&) = delete;
    ScopedPriority& operator=(const ScopedPriority&) = delete;

    ~ScopedPriority() {
        set_priority(original);
    }

    int original = 0;
};

ZEST_CASE(set_priority_takes_the_class_of_the_constant_at_or_below) {
    ScopedPriority restored;
    ZEXPECT(!set_priority(12));
    ZEXPECT(priority() == priority_below_normal);
    ZEXPECT(!set_priority(-10));
    ZEXPECT(priority() == priority_high);
}
#endif

ZEST_CASE(set_priority_past_the_range_fails) {
    ZEXPECT(set_priority(priority_highest - 1) == std::errc::invalid_argument);
    ZEXPECT(set_priority(priority_low + 1) == std::errc::invalid_argument);
}

#ifdef __linux__
/// This process's OOM score adjustment, put back when this goes.
struct ScopedOomScore {
    ScopedOomScore() {
        auto read = oom_score_adjustment();
        ZASSERT(read.has_value());
        original = *read;
    }

    ScopedOomScore(const ScopedOomScore&) = delete;
    ScopedOomScore& operator=(const ScopedOomScore&) = delete;

    // Back down to where it was needs no privilege.
    ~ScopedOomScore() {
        set_oom_score_adjustment(original);
    }

    int original = 0;
};

// Raising it needs no privilege.
ZEST_CASE(oom_score_adjustment_is_read_and_set) {
    ScopedOomScore score;
    ZEXPECT(score.original >= -1000);
    ZEXPECT(score.original <= 1000);
    auto raised = std::min(score.original + 1, 1000);
    ZEXPECT(!set_oom_score_adjustment(raised));
    ZEXPECT(oom_score_adjustment() == raised);
    ZEXPECT(oom_score_adjustment(pid()) == raised);
}

ZEST_CASE(oom_score_adjustment_of_a_missing_pid_fails) {
    auto read = oom_score_adjustment(missing_pid);
    ZASSERT(!read.has_value());
    ZEXPECT(read.error() == std::errc::no_such_process);
    ZEXPECT(set_oom_score_adjustment(0, missing_pid) == std::errc::no_such_process);
}
#endif

};  // ZEST_SUITE(sys_info)

}  // namespace

}  // namespace kota::sys
