#include "kota/fs/cygwin.h"

#include <algorithm>
#include <cstddef>
#include <ranges>

#include "kota/fs/file.h"
#include "kota/fs/status.h"
#include "kota/support/utf16.h"

namespace kota::fs::cygwin {

namespace {

bool equals_insensitive(std::string_view a, std::string_view b) {
    auto lower = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    return a.size() == b.size() &&
           std::ranges::equal(a, b, [&](char x, char y) { return lower(x) == lower(y); });
}

}  // namespace

bool is_posix_path(std::string_view path, path::Style style) {
    return path.starts_with('/') && !path::is_absolute(path, style);
}

std::optional<MountTable> MountTable::parse(std::string_view listing) {
    MountTable table;
    for(auto part: std::views::split(listing, '\n')) {
        std::string_view line(part.begin(), part.end());
        if(line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        auto on = line.find(" on /");
        auto type = line.rfind(" type ");
        if(on == std::string_view::npos || type == std::string_view::npos || type < on) {
            continue;
        }
        auto point = line.substr(on + 4, type - on - 4);
        table.mounts.emplace_back(std::string(point), std::string(line.substr(0, on)));
    }
    std::ranges::stable_sort(table.mounts, std::ranges::greater{}, [](const auto& mount) {
        return mount.first.size();
    });
    if(!std::ranges::any_of(table.mounts, [](const auto& mount) { return mount.first == "/"; })) {
        return std::nullopt;
    }
    return table;
}

std::string MountTable::to_windows(std::string_view posix) const {
    auto folded = path::normalize(posix, path::DotDot::Fold, path::Style::Posix);
    for(const auto& [point, directory]: mounts) {
        // A drive mounts as `C:`, at a point the runtime matches in either
        // case (`/c`, `/C`), and names its root only as `C:/`.
        bool drive = directory.size() == 2 && directory[1] == ':';
        std::string_view rest = folded;
        auto head = rest.substr(0, point.size());
        if(!(drive ? equals_insensitive(head, point) : head == point)) {
            continue;
        }
        rest.remove_prefix(head.size());
        if(point != "/" && !rest.empty() && !rest.starts_with('/')) {
            continue;
        }
        while(rest.starts_with('/')) {
            rest.remove_prefix(1);
        }
        std::string windows = directory;
        if(!rest.empty() || drive) {
            if(!windows.ends_with('/')) {
                windows += '/';
            }
            windows += rest;
        }
        return windows;
    }
    return folded;
}

std::vector<std::string> runtime_dirs(std::string_view program,
                                      std::span<const std::string> search_path) {
    std::vector<std::string> dirs;
    auto add = [&](std::string_view dir) {
        if(!dir.empty() &&
           (is_file(path::join(dir, "cygwin1.dll")) || is_file(path::join(dir, "msys-2.0.dll")))) {
            dirs.emplace_back(dir);
        }
    };
    add(path::parent(program));
    for(const auto& dir: search_path) {
        add(dir);
    }
    return dirs;
}

std::optional<std::string> read_symlink(std::string_view path) {
    constexpr std::string_view cookie = "!<symlink>";
    // A symlink file holds a path; a program is far larger.
    auto found = status(path);
    if(!found || found->type != FileType::Regular || found->size > 4096) {
        return std::nullopt;
    }
    auto bytes = read_file(path);
    if(!bytes || !std::string_view(*bytes).starts_with(cookie)) {
        return std::nullopt;
    }
    std::string_view target = *bytes;
    target.remove_prefix(cookie.size());
    std::string text;
    // The runtime's own `ln -s` writes the target in UTF-16 behind a BOM,
    // the package installer in plain bytes.
    if(target.starts_with("\xFF\xFE")) {
        target.remove_prefix(2);
        std::u16string wide;
        for(std::size_t i = 0; i + 1 < target.size(); i += 2) {
            wide.push_back(static_cast<char16_t>(static_cast<unsigned char>(target[i]) |
                                                 static_cast<unsigned char>(target[i + 1]) << 8));
        }
        text = from_utf16(wide);
    } else {
        text = target;
    }
    while(text.ends_with('\0')) {
        text.pop_back();
    }
    return text;
}

}  // namespace kota::fs::cygwin
