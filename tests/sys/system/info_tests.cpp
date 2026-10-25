#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <pthread.h>
#else
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "sys/harness/pids.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/sys/info.h"
#include "kota/sys/process.h"

namespace kota::sys {

namespace {

/// The calling thread's priority as the system keeps it: its priority within
/// the process's class on Windows, its quality of service on macOS, its nice
/// value on Linux.
int thread_priority() {
#ifdef _WIN32
    return ::GetThreadPriority(::GetCurrentThread());
#elif defined(__APPLE__)
    return static_cast<int>(::qos_class_self());
#else
    return ::getpriority(PRIO_PROCESS, static_cast<id_t>(::syscall(SYS_gettid)));
#endif
}

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
    auto stats = process_stats(test::missing_pid);
    ZASSERT(!stats.has_value());
    ZEXPECT(stats.error() == std::errc::no_such_process);
}

#ifdef __linux__
// Linux counts each thread's switches apart from the others'.
ZEST_CASE(process_stats_count_the_switches_of_every_thread) {
    auto before = process_stats();
    ZASSERT(before.has_value());
    long slept = 0;
    std::thread([&] {
        rusage start{};
        ::getrusage(RUSAGE_THREAD, &start);
        for(int i = 0; i < 1000; ++i) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        rusage end{};
        ::getrusage(RUSAGE_THREAD, &end);
        slept = end.ru_nvcsw - start.ru_nvcsw;
    }).join();
    // Each sleep gave the CPU up; the thread has ended since.
    ZASSERT(slept >= 500);
    auto after = process_stats();
    ZASSERT(after.has_value());
    ZEXPECT(after->voluntary_switches - before->voluntary_switches >= std::uint64_t(slept));
    auto by_pid = process_stats(pid());
    ZASSERT(by_pid.has_value());
    ZEXPECT(by_pid->voluntary_switches >= after->voluntary_switches);
}
#endif

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

#ifdef __APPLE__
// Each mach_host_self() adds a reference to this task's send right to the
// host port.
ZEST_CASE(memory_and_cpu_cores_give_back_the_host_port) {
    // This test's own reference keeps the right while it is counted.
    auto host = ::mach_host_self();
    mach_port_urefs_t before = 0;
    ZASSERT(::mach_port_get_refs(::mach_task_self(), host, MACH_PORT_RIGHT_SEND, &before) ==
            KERN_SUCCESS);
    for(int i = 0; i < 100; ++i) {
        ZASSERT(sys::memory().has_value());
        ZASSERT(cpu_cores().has_value());
    }
    mach_port_urefs_t after = 0;
    ZASSERT(::mach_port_get_refs(::mach_task_self(), host, MACH_PORT_RIGHT_SEND, &after) ==
            KERN_SUCCESS);
    ::mach_port_deallocate(::mach_task_self(), host);
    ZEXPECT(after == before);
}
#endif

ZEST_CASE(cpu_cores_have_run) {
    auto cores = cpu_cores();
    ZASSERT(cores.has_value());
    for(std::size_t i = 0; i < cores->size(); ++i) {
        ZEST_CONTEXT("core {}", i);
        const auto& core = (*cores)[i];
        ZEXPECT((core.times.user + core.times.system + core.times.idle).count() > 0);
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

ZEST_CASE(os_names_the_system) {
    auto system = os();
    ZEXPECT(!system.version.empty());
    ZEXPECT(!system.machine.empty());
#ifdef _WIN32
    ZEXPECT(system.name == "Windows");
    ZEXPECT(system.version.starts_with("10.0."));
#elif defined(__APPLE__)
    ZEXPECT(system.name == "macOS");
#else
    ZEXPECT(system.name == "Linux");
#endif
}

ZEST_CASE(hostname_and_uptime_are_reported) {
    std::string host;
    ZEXPECT(!hostname(host));
    ZEXPECT(!host.empty());
    ZEXPECT(uptime().count() > 0);
}

// A nice value between two of Priority's reads as the one below it, which
// this process may not take back on POSIX: another process's is set by the
// integration tests, and Windows's own below.
ZEST_CASE(priority_is_read) {
    auto read = priority();
    ZASSERT(read.has_value());
    ZEXPECT(priority() == *read);
}

ZEST_CASE(set_priority_of_a_value_no_priority_names_fails) {
    ZEXPECT(set_priority(static_cast<Priority>(6)) == std::errc::invalid_argument);
}

ZEST_CASE(priority_of_a_missing_pid_fails) {
    auto read = priority(test::missing_pid);
    ZASSERT(!read.has_value());
    ZEXPECT(read.error() == std::errc::no_such_process);
    ZEXPECT(set_priority(Priority::Normal, test::missing_pid) == std::errc::no_such_process);
}

// Each on a thread of its own, which goes with what it set, the case's own
// left as it was. Linux sets each priority's nice value as it is, and raising
// a thread's takes privileges: there only those at or below the thread's own,
// which a process started under `nice` may have lowered.
ZEST_CASE(set_thread_priority_sets_the_calling_threads) {
#ifdef _WIN32
    constexpr int levels[] = {
        THREAD_PRIORITY_IDLE,
        THREAD_PRIORITY_BELOW_NORMAL,
        THREAD_PRIORITY_NORMAL,
        THREAD_PRIORITY_ABOVE_NORMAL,
        THREAD_PRIORITY_HIGHEST,
        THREAD_PRIORITY_TIME_CRITICAL,
    };
#elif defined(__APPLE__)
    constexpr int levels[] = {
        QOS_CLASS_BACKGROUND,
        QOS_CLASS_UTILITY,
        QOS_CLASS_DEFAULT,
        QOS_CLASS_USER_INITIATED,
        QOS_CLASS_USER_INTERACTIVE,
        QOS_CLASS_USER_INTERACTIVE,
    };
#else
    constexpr int levels[] = {19, 10, 0, -7, -14, -20};
#endif
    auto before = thread_priority();
    for(int index = 0; index < 6; ++index) {
        auto level = static_cast<Priority>(index);
        ZEST_CONTEXT("priority {}", index);
#if !defined(_WIN32) && !defined(__APPLE__)
        if(levels[index] < before) {
            continue;
        }
#endif
        std::error_code error;
        int set = 0;
        std::thread([&] {
            error = set_thread_priority(level);
            set = thread_priority();
        }).join();
        ZEXPECT(!error);
        ZEXPECT(set == levels[index]);
    }
    ZEXPECT(thread_priority() == before);
}

ZEST_CASE(set_thread_priority_of_a_value_no_priority_names_fails) {
    ZEXPECT(set_thread_priority(static_cast<Priority>(6)) == std::errc::invalid_argument);
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

    Priority original = Priority::Normal;
};

ZEST_CASE(set_priority_sets_a_priority_class) {
    ScopedPriority restored;
    ZEXPECT(!set_priority(Priority::BelowNormal));
    ZEXPECT(priority() == Priority::BelowNormal);
    ZEXPECT(!set_priority(Priority::AboveNormal));
    ZEXPECT(priority() == Priority::AboveNormal);
}
#endif

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
    auto read = oom_score_adjustment(test::missing_pid);
    ZASSERT(!read.has_value());
    ZEXPECT(read.error() == std::errc::no_such_process);
    ZEXPECT(set_oom_score_adjustment(0, test::missing_pid) == std::errc::no_such_process);
}
#else
ZEST_CASE(oom_score_adjustment_off_linux_fails) {
    auto read = oom_score_adjustment();
    ZASSERT(!read.has_value());
    ZEXPECT(read.error() == std::errc::not_supported);
    ZEXPECT(set_oom_score_adjustment(0) == std::errc::not_supported);
}
#endif

};  // ZEST_SUITE(sys_info)

}  // namespace

}  // namespace kota::sys
