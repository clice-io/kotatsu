#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

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
#include <pwd.h>
#include <sys/resource.h>
#include <unistd.h>
#else
#include <pwd.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "sys/harness/env.h"
#include "sys/harness/pids.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/support/scope_exit.h"
#include "kotatsu/support/string_ref.h"
#include "kotatsu/fs/file.h"
#include "kotatsu/sys/info.h"
#include "kotatsu/sys/process.h"

namespace kotatsu::sys {

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

/// TZ set to `zone` until this goes, which puts back what it was: on Windows
/// in the C runtime's environment, which its time functions read.
struct ScopedTimeZone {
#ifdef _WIN32
    explicit ScopedTimeZone(const char* zone) {
        char* value = nullptr;
        std::size_t size = 0;
        if(::_dupenv_s(&value, &size, "TZ") == 0 && value != nullptr) {
            previous = value;
            std::free(value);
        }
        ZASSERT(::_putenv_s("TZ", zone) == 0);
    }

    ScopedTimeZone(const ScopedTimeZone&) = delete;
    ScopedTimeZone& operator=(const ScopedTimeZone&) = delete;

    ~ScopedTimeZone() {
        // An empty value removes it.
        ::_putenv_s("TZ", previous ? previous->c_str() : "");
        // The C runtime's zone too, which it reads again only at a _tzset.
        ::_tzset();
    }

    std::optional<std::string> previous;
#else
    explicit ScopedTimeZone(const char* zone) {
        env.emplace("TZ", zone);
    }

    ScopedTimeZone(const ScopedTimeZone&) = delete;
    ScopedTimeZone& operator=(const ScopedTimeZone&) = delete;

    // The C library's zone too, which it reads again only at a tzset.
    ~ScopedTimeZone() {
        env.reset();
        ::tzset();
    }

    std::optional<test::ScopedEnv> env;
#endif
};

/// The variable user_name() reads first.
#ifdef _WIN32
constexpr auto user_variable = "USERNAME";
#else
constexpr auto user_variable = "USER";
#endif

/// 2001-09-09 01:46:40 UTC, a Sunday, the 252nd day of its year.
constexpr std::chrono::system_clock::time_point billennium{std::chrono::seconds(1'000'000'000)};

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

ZEST_CASE(process_stats_count_the_address_space) {
    auto self = process_stats();
    ZASSERT(self.has_value());
    ZEXPECT(self->address_space >= self->resident);
    auto by_pid = process_stats(pid());
    ZASSERT(by_pid.has_value());
    ZEXPECT(by_pid->address_space > 0U);
}

#ifdef _WIN32
ZEST_CASE(address_space_limit_is_unset_on_windows) {
    ZEXPECT(address_space_limit() == std::nullopt);
}
#else
// A soft limit past any address space, which no mapping of this process's
// then meets; its hard limit when that is finite.
ZEST_CASE(address_space_limit_is_rlimit_as) {
    rlimit original{};
    ZASSERT(::getrlimit(RLIMIT_AS, &original) == 0);
    rlimit limited = original;
    limited.rlim_cur = original.rlim_max == RLIM_INFINITY ? rlim_t(1) << 62 : original.rlim_max;
    ZASSERT(::setrlimit(RLIMIT_AS, &limited) == 0);
    auto limit = address_space_limit();
    ::setrlimit(RLIMIT_AS, &original);
    ZEXPECT(limit == std::uint64_t(limited.rlim_cur));
}

ZEST_CASE(address_space_limit_is_unset_without_one) {
    rlimit original{};
    ZASSERT(::getrlimit(RLIMIT_AS, &original) == 0);
    // A hard limit, which this process may not lift.
    if(original.rlim_max != RLIM_INFINITY) {
        zest::skip();
        return;
    }
    rlimit lifted = original;
    lifted.rlim_cur = RLIM_INFINITY;
    ZASSERT(::setrlimit(RLIMIT_AS, &lifted) == 0);
    auto limit = address_space_limit();
    ::setrlimit(RLIMIT_AS, &original);
    ZEXPECT(limit == std::nullopt);
}
#endif

// What it gave back is taken again as any memory.
ZEST_CASE(release_free_heap_leaves_the_heap_usable) {
    std::vector<std::unique_ptr<char[]>> blocks;
    for(int i = 0; i < 256; ++i) {
        blocks.push_back(std::make_unique<char[]>(64 * 1024));
    }
    blocks.clear();
    release_free_heap();
    for(int i = 0; i < 256; ++i) {
        blocks.push_back(std::make_unique<char[]>(64 * 1024));
        blocks.back()[64 * 1024 - 1] = static_cast<char>(i);
    }
    ZEXPECT(blocks[255][64 * 1024 - 1] == static_cast<char>(255));
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

ZEST_CASE(physical_cores_are_at_most_the_logical_cpus) {
    auto cpus = cpu_cores();
    ZASSERT(cpus.has_value());
    ZEXPECT(physical_cores() >= 1U);
    ZEXPECT(physical_cores() <= cpus->size());
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

/// The CPUs of list `text`, as the kernel writes them ("0,8", "0-3").
std::vector<int> cpu_list(std::string_view text) {
    std::vector<int> cpus;
    for(auto range: std::views::split(text, ',')) {
        std::string item(range.begin(), range.end());
        auto dash = item.find('-');
        int first = std::stoi(item.substr(0, dash));
        int last = dash == std::string::npos ? first : std::stoi(item.substr(dash + 1));
        for(int cpu = first; cpu <= last; ++cpu) {
            cpus.push_back(cpu);
        }
    }
    return cpus;
}

// A CPU and its sibling on a core, where the machine runs two threads on
// one (x86-64 runners do, arm64 ones do not).
ZEST_CASE(physical_cores_count_the_cpus_of_a_core_once) {
    ScopedAffinity affinity;
    int first = 0;
    while(!CPU_ISSET(first, &affinity.original)) {
        ++first;
    }
    std::string text;
    auto path = std::format("/sys/devices/system/cpu/cpu{}/topology/thread_siblings_list", first);
    if(kotatsu::fs::read_file(text, path)) {
        zest::skip();
        return;
    }
    auto siblings = cpu_list(string_ref(text).trim());
    auto sibling = std::ranges::find_if(siblings, [&](int cpu) {
        return cpu != first && CPU_ISSET(cpu, &affinity.original);
    });
    if(sibling == siblings.end()) {
        zest::skip();
        return;
    }
    cpu_set_t two;
    CPU_ZERO(&two);
    CPU_SET(first, &two);
    CPU_SET(*sibling, &two);
    ZASSERT(::sched_setaffinity(0, sizeof(two), &two) == 0);
    ZEXPECT(physical_cores() == 1U);
}

ZEST_CASE(physical_cores_follow_the_affinity) {
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
    ZEXPECT(physical_cores() == 1U);
}
#endif

#ifdef _WIN32
// A process in several processor groups has no mask to put back.
ZEST_CASE(parallelism_and_physical_cores_follow_the_affinity) {
    DWORD_PTR original = 0;
    DWORD_PTR system = 0;
    ZASSERT(::GetProcessAffinityMask(::GetCurrentProcess(), &original, &system));
    if(original == 0) {
        zest::skip();
        return;
    }
    ScopeExit restore([&] { ::SetProcessAffinityMask(::GetCurrentProcess(), original); });
    ZASSERT(::SetProcessAffinityMask(::GetCurrentProcess(), original & ~(original - 1)));
    ZEXPECT(parallelism() == 1U);
    ZEXPECT(physical_cores() == 1U);
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

ZEST_CASE(user_name_reads_the_variable) {
    test::ScopedEnv user(user_variable, "someone");
    std::string name;
    ZEXPECT(!user_name(name));
    ZEXPECT(name == "someone");
}

ZEST_CASE(user_name_without_the_variable_asks_the_system) {
    for(std::optional<std::string_view> value:
        std::initializer_list<std::optional<std::string_view>>{std::nullopt, ""}) {
        ZEST_CONTEXT("{}", value ? "empty" : "unset");
        test::ScopedEnv user(user_variable, value);
        std::string name;
#ifdef _WIN32
        ZEXPECT(!user_name(name));
        ZEXPECT(!name.empty());
#else
        // A user the database does not list, as a container's may be.
        auto* entry = ::getpwuid(::getuid());
        if(entry == nullptr) {
            ZEXPECT(user_name(name) == std::errc::no_such_file_or_directory);
            continue;
        }
        ZEXPECT(!user_name(name));
        ZEXPECT(name == entry->pw_name);
#endif
    }
}

ZEST_CASE(local_time_is_of_the_time_zone) {
    {
        ScopedTimeZone zone("UTC0");
        auto local = local_time(billennium);
        ZASSERT(local.has_value());
        ZEXPECT(local->tm_year == 101);
        ZEXPECT(local->tm_mon == 8);
        ZEXPECT(local->tm_mday == 9);
        ZEXPECT(local->tm_hour == 1);
        ZEXPECT(local->tm_min == 46);
        ZEXPECT(local->tm_sec == 40);
        ZEXPECT(local->tm_wday == 0);
        ZEXPECT(local->tm_yday == 251);
    }
    // Eight hours east of UTC, as TZ spells it on POSIX and in Windows' C
    // runtime alike.
    ScopedTimeZone zone("CST-8");
    auto local = local_time(billennium + std::chrono::milliseconds(999));
    ZASSERT(local.has_value());
    ZEXPECT(local->tm_hour == 9);
    ZEXPECT(local->tm_sec == 40);
}

#ifndef _WIN32
// New York's, in daylight saving time in September: 4 hours behind UTC.
// Windows' C runtime marks it so, but keeps the 5 hours of standard time.
ZEST_CASE(local_time_applies_daylight_saving_time) {
    ScopedTimeZone zone("EST5EDT");
    auto local = local_time(billennium);
    ZASSERT(local.has_value());
    ZEXPECT(local->tm_mday == 8);
    ZEXPECT(local->tm_hour == 21);
    ZEXPECT(local->tm_isdst > 0);
}
#endif

#ifdef _WIN32
ZEST_CASE(local_time_before_1970_fails_on_windows) {
    auto local = local_time(std::chrono::system_clock::time_point(std::chrono::seconds(-1)));
    ZASSERT(!local.has_value());
    ZEXPECT(local.error() == std::errc::invalid_argument);
}
#else
// In the second it is in, not in the one nearer 1970.
ZEST_CASE(local_time_takes_a_time_before_1970) {
    ScopedTimeZone zone("UTC0");
    auto local =
        local_time(std::chrono::system_clock::time_point(std::chrono::milliseconds(-1500)));
    ZASSERT(local.has_value());
    ZEXPECT(local->tm_year == 69);
    ZEXPECT(local->tm_hour == 23);
    ZEXPECT(local->tm_sec == 58);
}
#endif

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

#ifdef _WIN32
/// This process's power throttling as Windows reads it back.
std::optional<PROCESS_POWER_THROTTLING_STATE> power_throttling() {
    PROCESS_POWER_THROTTLING_STATE state{
        .Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION,
        .ControlMask = 0,
        .StateMask = 0,
    };
    if(!::GetProcessInformation(::GetCurrentProcess(),
                                ProcessPowerThrottling,
                                &state,
                                sizeof(state))) {
        return std::nullopt;
    }
    return state;
}

ZEST_CASE(set_power_throttling_sets_this_processs_throttling) {
    auto original = power_throttling();
    ScopeExit restore([&] {
        // As no process sets it: Windows decides.
        auto state = original.value_or(PROCESS_POWER_THROTTLING_STATE{
            .Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION,
            .ControlMask = 0,
            .StateMask = 0,
        });
        ::SetProcessInformation(::GetCurrentProcess(),
                                ProcessPowerThrottling,
                                &state,
                                sizeof(state));
    });
    constexpr ULONG speed = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    ZEXPECT(!set_power_throttling(false));
    auto never = power_throttling();
    ZEXPECT(!set_power_throttling(true));
    auto always = power_throttling();
    // Windows 10 sets it without reading it back.
    if(!original) {
        return;
    }
    ZASSERT(never.has_value());
    ZEXPECT((never->ControlMask & speed) == speed);
    ZEXPECT((never->StateMask & speed) == 0U);
    ZASSERT(always.has_value());
    ZEXPECT((always->ControlMask & speed) == speed);
    ZEXPECT((always->StateMask & speed) == speed);
}
#else
ZEST_CASE(set_power_throttling_off_windows_fails) {
    ZEXPECT(set_power_throttling(false) == std::errc::not_supported);
}
#endif

};  // ZEST_SUITE(sys_info)

}  // namespace

}  // namespace kotatsu::sys
