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

int pid() noexcept {
    return static_cast<int>(uv::os_getpid());
}

memory_info memory() {
    memory_info info;
    info.total = uv::get_total_memory();
    info.free = uv::get_free_memory();
    info.available = uv::get_available_memory();
    info.constrained = uv::get_constrained_memory();
    return info;
}

result<std::size_t> resident_memory() {
    std::size_t rss = 0;
    if(auto err = uv::resident_set_memory(rss)) {
        return outcome_error(err);
    }
    return rss;
}

result<std::vector<cpu_core>> cpu_cores() {
    uv_cpu_info_t* infos = nullptr;
    int count = 0;
    if(auto err = uv::cpu_info(infos, count)) {
        return outcome_error(err);
    }

    struct guard {
        uv_cpu_info_t* p;
        int n;

        ~guard() {
            uv::free_cpu_info(p, n);
        }
    } cleanup{infos, count};

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
    return result;
}

unsigned int parallelism() {
    return uv::available_parallelism();
}

result<uname_info> uname() {
    uv_utsname_t buf{};
    if(auto err = uv::os_uname(buf)) {
        return outcome_error(err);
    }
    return uname_info{buf.sysname, buf.release, buf.version, buf.machine};
}

/// Helper: call a libuv string-returning function with stack buffer,
/// retry with heap allocation on UV_ENOBUFS.
template <typename Fn>
static result<std::string> read_uv_string(Fn&& fn, std::size_t initial_size) {
    std::string buf(initial_size, '\0');
    std::size_t size = buf.size();
    auto err = fn(buf.data(), size);
    if(err == error::no_buffer_space_available) {
        buf.resize(size);
        size = buf.size();
        err = fn(buf.data(), size);
    }
    if(err) {
        return outcome_error(err);
    }
    buf.resize(size);
    return buf;
}

result<std::string> hostname() {
    return read_uv_string(
        [](char* buf, std::size_t& size) { return uv::os_gethostname(buf, size); },
        256);
}

result<std::chrono::duration<double>> uptime() {
    double value = 0;
    if(auto err = uv::uptime(value)) {
        return outcome_error(err);
    }
    return std::chrono::duration<double>(value);
}

result<std::string> home_directory() {
    return read_uv_string([](char* buf, std::size_t& size) { return uv::os_homedir(buf, size); },
                          1024);
}

result<std::string> temp_directory() {
    return read_uv_string([](char* buf, std::size_t& size) { return uv::os_tmpdir(buf, size); },
                          1024);
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
    bool is_self = (pid == 0);
    if(is_self) {
        pid = sys::pid();
    }

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

    // CPU times and faults from /proc/<pid>/stat.
    // Format: "pid (comm) state ppid(4) ... minflt(10) cminflt(11)
    //          majflt(12) cmajflt(13) utime(14) stime(15) ..."
    {
        auto content = fs::sync::read_to_string(proc_path + "/stat");
        if(content) {
            auto rp = content->rfind(')');
            if(rp != std::string::npos && rp + 1 < content->size()) {
                const char* cur = content->data() + rp + 1;
                const char* end = content->data() + content->size();

                skip_field(cur, end);  // state (3)
                for(int i = 0; i < 6 && cur < end; ++i) {
                    next_ulong(cur, end);  // ppid(4)..flags(9)
                }

                stat.minor_faults = next_ulong(cur, end);  // minflt (10)
                next_ulong(cur, end);                      // cminflt (11)
                stat.major_faults = next_ulong(cur, end);  // majflt (12)
                next_ulong(cur, end);                      // cmajflt (13)

                unsigned long utime_ticks = next_ulong(cur, end);  // (14)
                unsigned long stime_ticks = next_ulong(cur, end);  // (15)

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

    if(is_self) {
        uv_rusage_t ru{};
        if(!uv::getrusage(ru)) {
            stat.max_rss = static_cast<std::size_t>(ru.ru_maxrss) * 1024;  // KB → bytes
            stat.voluntary_context_switches = ru.ru_nvcsw;
            stat.involuntary_context_switches = ru.ru_nivcsw;
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
    stat.major_faults = pti.pti_pageins;
    stat.minor_faults = pti.pti_faults - pti.pti_pageins;
    stat.voluntary_context_switches = pti.pti_csw;

    if(is_self) {
        uv_rusage_t ru{};
        if(!uv::getrusage(ru)) {
            stat.max_rss = ru.ru_maxrss;  // bytes on macOS
            stat.involuntary_context_switches = ru.ru_nivcsw;
        }
    }

#elif defined(_WIN32)
    HANDLE h;
    if(is_self) {
        h = GetCurrentProcess();
    } else {
        h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                        FALSE,
                        static_cast<DWORD>(pid));
        if(!h) {
            return outcome_error(error::no_such_process);
        }
    }

    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if(GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) {
        stat.rss = pmc.WorkingSetSize;
        stat.vsize = pmc.PagefileUsage;
        stat.max_rss = pmc.PeakWorkingSetSize;
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

    if(!is_self) {
        CloseHandle(h);
    }
#else
    return outcome_error(error::function_not_implemented);
#endif

    return stat;
}

}  // namespace kota::sys
