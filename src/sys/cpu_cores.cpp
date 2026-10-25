#include <cstdint>
#include <format>
#include <ranges>
#include <string>
#include <vector>

#include "os.h"
#include "proc.h"
#include "kota/sys/info.h"

#ifdef _WIN32
#include "kota/support/utf16.h"
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

#ifdef _WIN32
// Declared by no SDK header; ntdll has it from Windows 7 on.
extern "C" LONG NTAPI NtQuerySystemInformationEx(int information_class,
                                                 void* input,
                                                 ULONG input_size,
                                                 void* information,
                                                 ULONG information_size,
                                                 ULONG* returned_size);
#endif

namespace kota::sys {

#if defined(_WIN32) || defined(__linux__)

namespace {

#ifdef _WIN32

/// A value of the registry key of logical CPU `index`.
bool processor_value(DWORD index, const wchar_t* name, DWORD type, void* value, DWORD size) {
    auto key = std::format(LR"(HARDWARE\DESCRIPTION\System\CentralProcessor\{})", index);
    return ::RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), name, type, nullptr, value, &size) ==
           ERROR_SUCCESS;
}

#else

/// The value of `key` in a block of /proc/cpuinfo ("model name\t: Intel...").
std::optional<std::string_view> cpuinfo_value(std::string_view block, std::string_view key) {
    for(auto range: std::views::split(block, '\n')) {
        std::string_view line(range.begin(), range.end());
        if(!line.starts_with(key)) {
            continue;
        }
        auto rest = string_ref(line.substr(key.size())).ltrim(" \t");
        if(rest.starts_with(':')) {
            return string_ref(rest.substr(1)).trim();
        }
    }
    return std::nullopt;
}

#endif

}  // namespace

#endif

std::expected<std::vector<CpuCore>, std::error_code> cpu_cores() {
    std::vector<CpuCore> cores;
#ifdef _WIN32
    // SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION, in 100 ns units; the kernel
    // time holds the idle time.
    struct Times {
        LARGE_INTEGER idle;
        LARGE_INTEGER kernel;
        LARGE_INTEGER user;
        LARGE_INTEGER dpc;
        LARGE_INTEGER interrupt;
        ULONG interrupts;
    };

    constexpr int processor_performance = 8;
    auto milliseconds = [](LARGE_INTEGER time) {
        return std::chrono::milliseconds(time.QuadPart / 10'000);
    };
    for(WORD group = 0, groups = ::GetActiveProcessorGroupCount(); group < groups; ++group) {
        std::vector<Times> times(::GetActiveProcessorCount(group));
        auto size = static_cast<ULONG>(times.size() * sizeof(Times));
        // Given room for every processor of the group, it cannot fail.
        ::NtQuerySystemInformationEx(processor_performance,
                                     &group,
                                     sizeof(group),
                                     times.data(),
                                     size,
                                     nullptr);
        for(const auto& time: times) {
            auto index = static_cast<DWORD>(cores.size());
            CpuCore core;
            core.times = {
                .user = milliseconds(time.user),
                .nice = {},
                .system = milliseconds(time.kernel) - milliseconds(time.idle),
                .idle = milliseconds(time.idle),
                .irq = milliseconds(time.interrupt),
            };
            wchar_t name[256];
            if(processor_value(index, L"ProcessorNameString", RRF_RT_REG_SZ, name, sizeof(name))) {
                core.model = string_ref(from_utf16(std::wstring_view(name))).trim();
            }
            DWORD speed = 0;
            if(processor_value(index, L"~MHz", RRF_RT_REG_DWORD, &speed, sizeof(speed))) {
                core.speed_mhz = static_cast<int>(speed);
            }
            cores.push_back(std::move(core));
        }
    }
#elif defined(__APPLE__)
    natural_t count = 0;
    processor_info_array_t info = nullptr;
    mach_msg_type_number_t info_count = 0;
    // Of this very host, it cannot fail.
    ::host_processor_info(::mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &count, &info, &info_count);
    char model[256] = {};
    auto model_size = sizeof(model);
    ::sysctlbyname("machdep.cpu.brand_string", model, &model_size, nullptr, 0);
    // Intel Macs only.
    std::uint64_t frequency = 0;
    auto frequency_size = sizeof(frequency);
    ::sysctlbyname("hw.cpufrequency", &frequency, &frequency_size, nullptr, 0);
    auto tick = std::chrono::milliseconds(1000) / ::sysconf(_SC_CLK_TCK);
    auto* ticks = reinterpret_cast<processor_cpu_load_info_data_t*>(info);
    for(natural_t i = 0; i < count; ++i) {
        cores.push_back({
            .model = model,
            .speed_mhz = static_cast<int>(frequency / 1'000'000),
            .times =
                {
                        .user = ticks[i].cpu_ticks[CPU_STATE_USER] * tick,
                        .nice = ticks[i].cpu_ticks[CPU_STATE_NICE] * tick,
                        .system = ticks[i].cpu_ticks[CPU_STATE_SYSTEM] * tick,
                        .idle = ticks[i].cpu_ticks[CPU_STATE_IDLE] * tick,
                        .irq = {},
                        },
        });
    }
    ::vm_deallocate(::mach_task_self(),
                    reinterpret_cast<vm_address_t>(info),
                    info_count * sizeof(integer_t));
#else
    auto stat = fs::read_file("/proc/stat");
    if(!stat) {
        return std::unexpected(stat.error());
    }
    // Blocks of "key\t: value" lines, one per online CPU.
    auto cpuinfo = fs::read_file("/proc/cpuinfo").value_or("");
    std::vector<std::string_view> blocks;
    for(auto block: std::views::split(std::string_view(cpuinfo), std::string_view("\n\n"))) {
        blocks.emplace_back(block.begin(), block.end());
    }
    auto ticks = static_cast<std::uint64_t>(::sysconf(_SC_CLK_TCK));
    auto milliseconds = [&](std::string_view line, std::size_t index) {
        return std::chrono::milliseconds(leading_number(word(line, index)).value_or(0) * 1000 /
                                         ticks);
    };
    // "cpu<N> user nice system idle iowait irq softirq ...", in clock ticks.
    for(auto range: std::views::split(std::string_view(*stat), '\n')) {
        std::string_view line(range.begin(), range.end());
        // Not "cpu ", the sum of them all.
        if(!line.starts_with("cpu") || line.size() < 4 || line[3] < '0' || line[3] > '9') {
            continue;
        }
        auto cpu = leading_number(line.substr(3));
        CpuCore core;
        core.times = {
            .user = milliseconds(line, 1),
            .nice = milliseconds(line, 2),
            .system = milliseconds(line, 3),
            .idle = milliseconds(line, 4),
            .irq = milliseconds(line, 6),
        };
        auto block = std::ranges::find_if(blocks, [&](std::string_view block) {
            auto processor = cpuinfo_value(block, "processor");
            return processor && leading_number(*processor) == cpu;
        });
        if(block != blocks.end()) {
            // Kernels for ARM may name no model.
            core.model = cpuinfo_value(*block, "model name").value_or("");
            core.speed_mhz = static_cast<int>(
                leading_number(cpuinfo_value(*block, "cpu MHz").value_or("")).value_or(0));
        }
        auto frequency = fs::read_file(
            std::format("/sys/devices/system/cpu/cpu{}/cpufreq/scaling_cur_freq", *cpu));
        if(frequency) {
            // In kHz.
            core.speed_mhz = static_cast<int>(leading_number(*frequency).value_or(0) / 1000);
        }
        cores.push_back(std::move(core));
    }
#endif
    return cores;
}

}  // namespace kota::sys
