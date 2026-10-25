#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

#include "proc.h"
#include "kota/sys/memory_model.h"

namespace kota::sys::cgroup {

namespace {

/// A line of /proc/<pid>/cgroup: "<id>:<controllers>:<path>".
struct Membership {
    std::string_view controllers;
    std::string_view path;
};

std::optional<Membership> parse(std::string_view line) {
    auto first = line.find(':');
    auto second = line.find(':', first + 1);
    if(first == std::string_view::npos || second == std::string_view::npos) {
        return std::nullopt;
    }
    return Membership{
        .controllers = line.substr(first + 1, second - first - 1),
        .path = string_ref(line.substr(second + 1)).rtrim('/'),
    };
}

bool has_controller(std::string_view controllers, std::string_view name) {
    while(!controllers.empty()) {
        auto end = controllers.find(',');
        if(controllers.substr(0, end) == name) {
            return true;
        }
        controllers.remove_prefix(end == std::string_view::npos ? controllers.size() : end + 1);
    }
    return false;
}

/// The membership of the v1 hierarchy of `controller` in `membership`, or with
/// an empty controller the v2 one.
std::optional<Membership> find(std::string_view membership, std::string_view controller) {
    for(auto line: std::views::split(membership, '\n')) {
        auto parsed = parse(std::string_view(line.begin(), line.end()));
        if(!parsed) {
            continue;
        }
        if(controller.empty() ? parsed->controllers.empty()
                              : has_controller(parsed->controllers, controller)) {
            return parsed;
        }
    }
    return std::nullopt;
}

/// A number file; unset for "max", -1 or a file that cannot be read.
std::optional<std::uint64_t> number(Reader read, const std::string& path) {
    auto text = read(path);
    if(!text) {
        return std::nullopt;
    }
    return leading_number(*text);
}

/// Calls `visit` on `dir` and each directory above it, up to `top`.
template <typename Visit>
void each_level(std::string dir, std::string_view top, Visit visit) {
    while(true) {
        visit(dir);
        if(dir.size() <= top.size()) {
            return;
        }
        dir.resize(dir.rfind('/'));
    }
}

/// What a cgroup can still take: its limit less its usage, plus the file
/// cache no process maps. Usage may pass a memory.high: the cache pays for
/// the excess first.
std::uint64_t
    left(std::uint64_t limit, std::uint64_t usage, std::uint64_t file, std::uint64_t mapped) {
    auto reclaimable = file > mapped ? file - mapped : 0;
    return limit + reclaimable > usage ? limit + reclaimable - usage : 0;
}

/// A limit past every machine's memory: v1's way to say none.
constexpr std::uint64_t unlimited = std::uint64_t(1) << 62;

}  // namespace

std::optional<MemoryLimit> memory(std::string_view root, std::string_view membership, Reader read) {
    std::optional<MemoryLimit> tightest;
    auto add = [&](std::uint64_t limit, std::uint64_t available) {
        if(!tightest) {
            tightest = MemoryLimit{.limit = limit, .available = available};
        } else {
            tightest->limit = std::min(tightest->limit, limit);
            tightest->available = std::min(tightest->available, available);
        }
    };

    if(auto v1 = find(membership, "memory")) {
        auto top = std::string(root) + "/memory";
        auto dir = top + std::string(v1->path);
        // A container without a cgroup namespace has its own cgroup mounted
        // at the hierarchy's root, under the path the host knows it by.
        if(!read(dir + "/memory.usage_in_bytes")) {
            dir = top;
        }
        each_level(dir, top, [&](const std::string& level) {
            auto limit = number(read, level + "/memory.limit_in_bytes");
            if(!limit || *limit >= unlimited) {
                return;
            }
            auto stat = read(level + "/memory.stat").value_or("");
            add(*limit,
                left(*limit,
                     number(read, level + "/memory.usage_in_bytes").value_or(0),
                     field(stat, "total_active_file ").value_or(0) +
                         field(stat, "total_inactive_file ").value_or(0),
                     field(stat, "total_mapped_file ").value_or(0)));
        });
        return tightest;
    }

    if(auto v2 = find(membership, "")) {
        each_level(std::string(root) + std::string(v2->path), root, [&](const std::string& level) {
            auto max = number(read, level + "/memory.max");
            auto high = number(read, level + "/memory.high");
            if(!max && !high) {
                return;
            }
            auto limit = std::min(max.value_or(UINT64_MAX), high.value_or(UINT64_MAX));
            auto stat = read(level + "/memory.stat").value_or("");
            add(limit,
                left(limit,
                     number(read, level + "/memory.current").value_or(0),
                     field(stat, "active_file ").value_or(0) +
                         field(stat, "inactive_file ").value_or(0),
                     field(stat, "file_mapped ").value_or(0)));
        });
    }
    return tightest;
}

std::optional<double> cpu_quota(std::string_view root, std::string_view membership, Reader read) {
    std::optional<double> tightest;
    auto add = [&](std::uint64_t quota, std::uint64_t period) {
        auto cpus = static_cast<double>(quota) / static_cast<double>(period);
        tightest = std::min(tightest.value_or(cpus), cpus);
    };

    if(auto v1 = find(membership, "cpu")) {
        auto top = std::string(root) + "/cpu";
        auto dir = top + std::string(v1->path);
        if(!read(dir + "/cpu.cfs_period_us")) {
            dir = top;
        }
        each_level(dir, top, [&](const std::string& level) {
            // -1 when the level has none.
            auto quota = number(read, level + "/cpu.cfs_quota_us");
            auto period = number(read, level + "/cpu.cfs_period_us");
            if(quota && period) {
                add(*quota, *period);
            }
        });
        return tightest;
    }

    if(auto v2 = find(membership, "")) {
        each_level(std::string(root) + std::string(v2->path), root, [&](const std::string& level) {
            // "<quota> <period>", the quota "max" when the level has none.
            auto text = read(level + "/cpu.max");
            if(!text) {
                return;
            }
            std::string_view fields = *text;
            auto quota = leading_number(fields);
            auto period = leading_number(fields.substr(std::min(fields.find(' '), fields.size())));
            if(quota && period) {
                add(*quota, *period);
            }
        });
    }
    return tightest;
}

}  // namespace kota::sys::cgroup
