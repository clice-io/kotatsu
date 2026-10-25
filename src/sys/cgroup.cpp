#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

#include "proc.h"
#include "kota/sys/detail/memory_model.h"

namespace kota::sys::detail::cgroup {

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

/// Where `mounts`, a /proc/<pid>/mountinfo text, mounts the v1 hierarchy of
/// `controller`, or with an empty controller the v2 one.
std::optional<std::string> mount_point(std::string_view mounts, std::string_view controller) {
    // "<id> <parent> <device> <root> <point> <options> [<tag>...] - <type>
    // <source> <super options>", a v1 hierarchy's controllers among the
    // super options ("rw,cpu,cpuacct"). A path's blanks and backslashes are
    // octal escapes ("\040").
    for(auto range: std::views::split(mounts, '\n')) {
        std::string_view line(range.begin(), range.end());
        auto separator = line.find(" - ");
        if(separator == std::string_view::npos) {
            continue;
        }
        auto filesystem = line.substr(separator + 3);
        auto type = word(filesystem, 0);
        if(controller.empty()
               ? type != "cgroup2"
               : type != "cgroup" || !has_controller(word(filesystem, 2), controller)) {
            continue;
        }
        auto escaped = word(line, 4);
        std::string point;
        for(std::size_t i = 0; i < escaped.size(); ++i) {
            if(escaped[i] != '\\') {
                point += escaped[i];
                continue;
            }
            auto digits = escaped.substr(i + 1, 3);
            unsigned char code = 0;
            std::from_chars(digits.data(), digits.data() + digits.size(), code, 8);
            point += static_cast<char>(code);
            i += digits.size();
        }
        return point;
    }
    return std::nullopt;
}

/// A process's cgroup in a hierarchy: the directory of the hierarchy's root,
/// and the cgroup's own.
struct Place {
    std::string top;
    std::string dir;
};

/// The cgroup of the process whose /proc/<pid>/cgroup reads `membership`
/// in the v1 hierarchy of `controller`, or with an empty controller in the
/// v2 one; unset when it is in none, or `mounts` mounts none.
std::optional<Place> place(std::string_view mounts,
                           std::string_view membership,
                           std::string_view controller) {
    auto joined = find(membership, controller);
    if(!joined) {
        return std::nullopt;
    }
    auto top = mount_point(mounts, controller);
    if(!top) {
        return std::nullopt;
    }
    return Place{.top = *top, .dir = *top + std::string(joined->path)};
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

std::optional<MemoryLimit> memory(std::string_view mounts,
                                  std::string_view membership,
                                  Reader read) {
    std::optional<MemoryLimit> tightest;
    auto add = [&](std::uint64_t limit, std::uint64_t available) {
        if(!tightest) {
            tightest = MemoryLimit{.limit = limit, .available = available};
        } else {
            tightest->limit = std::min(tightest->limit, limit);
            tightest->available = std::min(tightest->available, available);
        }
    };

    if(auto v1 = place(mounts, membership, "memory")) {
        // A container without a cgroup namespace has its own cgroup mounted
        // at the hierarchy's root, under the path the host knows it by.
        if(!read(v1->dir + "/memory.usage_in_bytes")) {
            v1->dir = v1->top;
        }
        each_level(v1->dir, v1->top, [&](const std::string& level) {
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

    if(auto v2 = place(mounts, membership, "")) {
        each_level(v2->dir, v2->top, [&](const std::string& level) {
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

std::optional<double> cpu_quota(std::string_view mounts, std::string_view membership, Reader read) {
    std::optional<double> tightest;
    auto add = [&](std::uint64_t quota, std::uint64_t period) {
        auto cpus = static_cast<double>(quota) / static_cast<double>(period);
        tightest = std::min(tightest.value_or(cpus), cpus);
    };

    if(auto v1 = place(mounts, membership, "cpu")) {
        if(!read(v1->dir + "/cpu.cfs_period_us")) {
            v1->dir = v1->top;
        }
        each_level(v1->dir, v1->top, [&](const std::string& level) {
            // -1 when the level has none.
            auto quota = number(read, level + "/cpu.cfs_quota_us");
            auto period = number(read, level + "/cpu.cfs_period_us");
            if(quota && period) {
                add(*quota, *period);
            }
        });
        return tightest;
    }

    if(auto v2 = place(mounts, membership, "")) {
        each_level(v2->dir, v2->top, [&](const std::string& level) {
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

}  // namespace kota::sys::detail::cgroup
