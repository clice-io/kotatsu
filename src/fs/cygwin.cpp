#include "kotatsu/fs/cygwin.h"

#include <algorithm>
#include <cstddef>
#include <ranges>

#include "kotatsu/support/small_string.h"
#include "kotatsu/support/small_vector.h"
#include "kotatsu/support/string_ref.h"
#include "kotatsu/support/utf16.h"
#include "kotatsu/fs/file.h"
#include "kotatsu/fs/status.h"

namespace kotatsu::fs::cygwin {

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
        // Cygwin may list a directory with backslashes.
        auto& mount = table.mounts.emplace_back(point, line.substr(0, on));
        path::to_slash(mount.second, path::Style::Windows);
    }
    std::ranges::stable_sort(table.mounts, std::ranges::greater{}, [](const auto& mount) {
        return mount.first.size();
    });
    if(!std::ranges::any_of(table.mounts, [](const auto& mount) { return mount.first == "/"; })) {
        return std::nullopt;
    }
    return table;
}

void MountTable::to_windows(buffer_ref out, std::string_view posix) const {
    // `//server/share` names a share, as on Windows, which no mount covers.
    if(posix.starts_with("//") && !posix.starts_with("///")) {
        out.assign(posix);
        path::normalize(out, path::DotDot::Fold, path::Style::Windows);
        path::to_slash(out, path::Style::Windows);
        return;
    }
    small_string<256> folded(posix);
    path::normalize(folded, path::DotDot::Fold, path::Style::Posix);
    for(const auto& [point, directory]: mounts) {
        // A drive mounts as `C:`, at a point the runtime matches in either
        // case (`/c`, `/C`), and names its root only as `C:/`.
        bool drive = directory.size() == 2 && directory[1] == ':';
        std::string_view rest = folded;
        auto head = rest.substr(0, point.size());
        if(!(drive ? string_ref(head).equals_insensitive(point) : head == point)) {
            continue;
        }
        rest.remove_prefix(head.size());
        if(point != "/" && !rest.empty() && !rest.starts_with('/')) {
            continue;
        }
        // Normalized, it has one separator here at most.
        if(rest.starts_with('/')) {
            rest.remove_prefix(1);
        }
        out.assign(directory);
        if(!rest.empty() || drive) {
            if(!directory.ends_with('/')) {
                out.push_back('/');
            }
            out.append(rest);
        }
        return;
    }
    out.assign(folded);
}

std::vector<std::string> runtime_dirs(std::string_view program, string_list_ref search_path) {
    std::vector<std::string> dirs;
    small_string<256> runtime;
    auto holds = [&](std::string_view dir, std::string_view name) {
        runtime.assign(dir);
        path::push(runtime, name);
        return is_file(runtime);
    };
    auto add = [&](std::string_view dir) {
        if(!dir.empty() && (holds(dir, "cygwin1.dll") || holds(dir, "msys-2.0.dll"))) {
            dirs.emplace_back(dir);
        }
    };
    // A program named without a directory is in the current one.
    auto own = path::parent(program);
    add(own.empty() ? "." : own);
    for(auto dir: search_path) {
        add(dir);
    }
    return dirs;
}

bool read_symlink(buffer_ref out, std::string_view path) {
    constexpr std::string_view cookie = "!<symlink>";
    // A symlink file holds a path, at most Cygwin's longest (4096 bytes) in
    // UTF-16 behind a BOM; a program is far larger.
    constexpr std::size_t largest = cookie.size() + 2 + 2 * 4096;
    auto found = status(path);
    if(!found || found->type != FileType::Regular || found->size > largest) {
        return false;
    }
    small_string<512> bytes;
    if(read_file(bytes, path) || !std::string_view(bytes).starts_with(cookie)) {
        return false;
    }
    std::string_view target = bytes;
    target.remove_prefix(cookie.size());
    // The runtime's own `ln -s` writes the target in UTF-16 behind a BOM,
    // the package installer in plain bytes.
    if(target.starts_with("\xFF\xFE")) {
        target.remove_prefix(2);
        small_vector<char16_t, 256> wide;
        for(std::size_t i = 0; i + 1 < target.size(); i += 2) {
            wide.push_back(static_cast<char16_t>(static_cast<unsigned char>(target[i]) |
                                                 static_cast<unsigned char>(target[i + 1]) << 8));
        }
        from_utf16(out, std::u16string_view(wide.data(), wide.size()));
    } else {
        out.assign(target);
    }
    auto text = out.view();
    while(text.ends_with('\0')) {
        text.remove_suffix(1);
    }
    out.resize(text.size());
    return true;
}

}  // namespace kotatsu::fs::cygwin
