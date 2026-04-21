#include "kota/async/io/system.h"

#include "../libuv.h"

#if defined(__linux__)
#include <charconv>
#include <unistd.h>

#include "kota/async/io/fs.h"
#elif defined(__APPLE__)
#include <libproc.h>
#include <mach/mach.h>
#elif defined(_WIN32)
#include <psapi.h>
#endif

namespace kota::sys {

memory_info memory() {
    memory_info info;
    info.total = ::uv_get_total_memory();
    info.free = ::uv_get_free_memory();
    info.available = ::uv_get_available_memory();
    info.constrained = ::uv_get_constrained_memory();
    return info;
}

result<std::size_t> resident_memory() {
    std::size_t rss = 0;
    if(auto err = uv::resident_set_memory(rss)) {
        return outcome_error(err);
    }
    return rss;
}

result<resource_usage> resources() {
    uv_rusage_t ru{};
    if(auto err = uv::getrusage(ru)) {
        return outcome_error(err);
    }

    resource_usage usage;
    usage.user_time =
        std::chrono::seconds(ru.ru_utime.tv_sec) + std::chrono::microseconds(ru.ru_utime.tv_usec);
    usage.system_time =
        std::chrono::seconds(ru.ru_stime.tv_sec) + std::chrono::microseconds(ru.ru_stime.tv_usec);
    usage.max_rss = ru.ru_maxrss;
    usage.minor_faults = ru.ru_minflt;
    usage.major_faults = ru.ru_majflt;
    usage.voluntary_context_switches = ru.ru_nvcsw;
    usage.involuntary_context_switches = ru.ru_nivcsw;
    return usage;
}

result<std::vector<cpu_core>> cpu_cores() {
    uv_cpu_info_t* infos = nullptr;
    int count = 0;
    if(auto err = uv::cpu_info(infos, count)) {
        return outcome_error(err);
    }

    std::vector<cpu_core> result;
    result.reserve(static_cast<std::size_t>(count));
    for(int i = 0; i < count; ++i) {
        auto& src = infos[i];
        cpu_core core;
        core.model = src.model ? src.model : "";
        core.speed_mhz = src.speed;
        core.times.user = std::chrono::milliseconds(src.cpu_times.user);
        core.times.nice = std::chrono::milliseconds(src.cpu_times.nice);
        core.times.sys = std::chrono::milliseconds(src.cpu_times.sys);
        core.times.idle = std::chrono::milliseconds(src.cpu_times.idle);
        core.times.irq = std::chrono::milliseconds(src.cpu_times.irq);
        result.push_back(std::move(core));
    }
    uv::free_cpu_info(infos, count);
    return result;
}

unsigned int parallelism() {
    return ::uv_available_parallelism();
}

result<uname_info> uname() {
    uv_utsname_t buf{};
    if(auto err = uv::os_uname(buf)) {
        return outcome_error(err);
    }
    return uname_info{buf.sysname, buf.release, buf.version, buf.machine};
}

result<std::string> hostname() {
    char buf[256]{};
    std::size_t size = sizeof(buf);
    if(auto err = uv::os_gethostname(buf, size)) {
        return outcome_error(err);
    }
    return std::string(buf, size);
}

result<std::chrono::duration<double>> uptime() {
    double value = 0;
    if(auto err = uv::uptime(value)) {
        return outcome_error(err);
    }
    return std::chrono::duration<double>(value);
}

result<std::string> home_directory() {
    char buf[1024]{};
    std::size_t size = sizeof(buf);
    if(auto err = uv::os_homedir(buf, size)) {
        return outcome_error(err);
    }
    return std::string(buf, size);
}

result<std::string> temp_directory() {
    char buf[1024]{};
    std::size_t size = sizeof(buf);
    if(auto err = uv::os_tmpdir(buf, size)) {
        return outcome_error(err);
    }
    return std::string(buf, size);
}

result<int> priority(int pid) {
    int value = 0;
    if(auto err = uv::os_getpriority(static_cast<uv_pid_t>(pid), value)) {
        return outcome_error(err);
    }
    return value;
}

error set_priority(int value, int pid) {
    return uv::os_setpriority(static_cast<uv_pid_t>(pid), value);
}

result<process_stat> process(int pid) {
    process_stat stat{};
    stat.pid = pid;

#if defined(__linux__)
    auto proc_path = "/proc/" + std::to_string(pid);

    auto skip_field = [](const char*& cur, const char* end) {
        while(cur < end && *cur == ' ') {
            ++cur;
        }
        while(cur < end && *cur != ' ') {
            ++cur;
        }
    };

    auto next_ulong = [](const char*& cur, const char* end) -> unsigned long {
        while(cur < end && *cur == ' ') {
            ++cur;
        }
        unsigned long val = 0;
        auto [ptr, ec] = std::from_chars(cur, end, val);
        cur = ptr;
        return (ec == std::errc{}) ? val : 0;
    };

    // Memory from /proc/<pid>/statm: "vsize_pages rss_pages ..." (all in pages).
    {
        auto content = fs::sync::read_to_string(proc_path + "/statm");
        if(!content) {
            return outcome_error(error::no_such_process);
        }

        const char* cur = content->data();
        const char* end = cur + content->size();

        unsigned long vsize_pages = next_ulong(cur, end);
        unsigned long rss_pages = next_ulong(cur, end);

        auto page_size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
        stat.vsize = vsize_pages * page_size;
        stat.rss = rss_pages * page_size;
    }

    // CPU times from /proc/<pid>/stat.
    // Format: "pid (comm) state field4 ... field14(utime) field15(stime) ..."
    {
        auto content = fs::sync::read_to_string(proc_path + "/stat");
        if(content) {
            auto rp = content->rfind(')');
            if(rp != std::string::npos && rp + 1 < content->size()) {
                const char* cur = content->data() + rp + 1;
                const char* end = content->data() + content->size();

                // Fields after ')': state(3) ppid(4) ... utime(14) stime(15).
                skip_field(cur, end);  // state
                for(int i = 0; i < 10 && cur < end; ++i) {
                    next_ulong(cur, end);
                }

                unsigned long utime_ticks = next_ulong(cur, end);
                unsigned long stime_ticks = next_ulong(cur, end);

                long hz = sysconf(_SC_CLK_TCK);
                if(hz <= 0) {
                    hz = 100;
                }
                auto uhz = static_cast<std::uint64_t>(hz);
                stat.user_time = std::chrono::microseconds(utime_ticks * 1'000'000ULL / uhz);
                stat.system_time = std::chrono::microseconds(stime_ticks * 1'000'000ULL / uhz);
            }
        }
    }

#elif defined(__APPLE__)
    struct proc_taskinfo pti{};
    int ret = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &pti, sizeof(pti));
    if(ret <= 0) {
        return outcome_error(error::no_such_process);
    }

    stat.rss = pti.pti_resident_size;
    stat.vsize = pti.pti_virtual_size;
    stat.user_time = std::chrono::microseconds(pti.pti_total_user / 1000);
    stat.system_time = std::chrono::microseconds(pti.pti_total_system / 1000);

#elif defined(_WIN32)
    HANDLE h =
        OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, static_cast<DWORD>(pid));
    if(!h) {
        return outcome_error(error::no_such_process);
    }

    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if(GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) {
        stat.rss = pmc.WorkingSetSize;
        stat.vsize = pmc.PagefileUsage;
    }

    FILETIME creation, exit_time, kernel_time, user_time;
    if(GetProcessTimes(h, &creation, &exit_time, &kernel_time, &user_time)) {
        auto to_us = [](FILETIME ft) -> std::uint64_t {
            std::uint64_t t =
                (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
            return t / 10;  // 100-ns intervals -> microseconds
        };
        stat.user_time = std::chrono::microseconds(to_us(user_time));
        stat.system_time = std::chrono::microseconds(to_us(kernel_time));
    }

    CloseHandle(h);
#else
    return outcome_error(error::function_not_implemented);
#endif

    return stat;
}

}  // namespace kota::sys
