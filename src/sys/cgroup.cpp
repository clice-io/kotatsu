#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

#include "proc.h"
#include "kotatsu/sys/detail/memory_model.h"

namespace kotatsu::sys::detail::cgroup {

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

/// A path of mountinfo, whose blanks and backslashes are octal escapes
/// ("\040", "\134").
std::string unescape(std::string_view escaped) {
    std::string path;
    for(std::size_t i = 0; i < escaped.size(); ++i) {
        if(escaped[i] != '\\') {
            path += escaped[i];
            continue;
        }
        auto digits = escaped.substr(i + 1, 3);
        unsigned char code = 0;
        std::from_chars(digits.data(), digits.data() + digits.size(), code, 8);
        path += static_cast<char>(code);
        i += digits.size();
    }
    return path;
}

/// A process's cgroup in a hierarchy: the directory of the mount that holds
/// it, and the cgroup's own.
struct Place {
    std::string top;
    std::string dir;
};

/// Where `mounts`, a /proc/<pid>/mountinfo text, has cgroup `path` of the v1
/// hierarchy of `controller`, or with an empty controller of the v2 one: in
/// the first mount of it whose root, the cgroup it shows at its point, is
/// `path` or above it, as Rust's std finds a cgroup.
std::optional<Place> mounted(std::string_view mounts,
                             std::string_view controller,
                             std::string_view path) {
    // "<id> <parent> <device> <root> <point> <options> [<tag>...] - <type>
    // <source> <super options>", a v1 hierarchy's controllers among the
    // super options ("rw,cpu,cpuacct").
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
        // The root as `path` is written, the hierarchy's own empty.
        auto root = unescape(word(line, 3));
        std::string_view above = string_ref(root).rtrim('/');
        if(!path.starts_with(above) || (path.size() > above.size() && path[above.size()] != '/')) {
            continue;
        }
        auto point = unescape(word(line, 4));
        auto dir = point + std::string(path.substr(above.size()));
        return Place{.top = std::move(point), .dir = std::move(dir)};
    }
    return std::nullopt;
}

/// The cgroup of the process whose /proc/<pid>/cgroup reads `membership`
/// in the v1 hierarchy of `controller`, or with an empty controller in the
/// v2 one; unset when it is in none, or `mounts` has none that holds it.
std::optional<Place> place(std::string_view mounts,
                           std::string_view membership,
                           std::string_view controller) {
    auto joined = find(membership, controller);
    if(!joined) {
        return std::nullopt;
    }
    return mounted(mounts, controller, joined->path);
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

}  // namespace kotatsu::sys::detail::cgroup
