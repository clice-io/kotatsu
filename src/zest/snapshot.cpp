#include "kota/zest/snapshot/snapshot.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <print>
#include <set>
#include <string>
#include <vector>

#include "execution.h"
#include "kota/support/glob_pattern.h"

namespace kota::zest {

namespace fs = std::filesystem;

namespace {

struct SnapshotContext {
    std::string suite_name;
    std::string test_name;
    std::string source_file;
    bool unnamed_used = false;
};

std::atomic<bool> update_snapshots_flag{false};
std::string global_snapshot_dir;

std::mutex accessed_mutex;
std::set<std::string> accessed_snap_paths;
std::set<std::string> accessed_snap_dirs;

void record_access(const fs::path& snap_path) {
    std::lock_guard lock(accessed_mutex);
    accessed_snap_paths.insert(snap_path.lexically_normal().string());
    auto dir = snap_path.parent_path().lexically_normal();
    auto root = fs::path(global_snapshot_dir).lexically_normal();
    while(!dir.empty() && dir != root && dir.has_relative_path()) {
        accessed_snap_dirs.insert(dir.string());
        dir = dir.parent_path();
    }
    accessed_snap_dirs.insert(root.string());
}

SnapshotContext& context() {
    thread_local SnapshotContext ctx;
    return ctx;
}

struct SnapData {
    std::string body;
    std::string created_at;
};

std::optional<SnapData> read_snap(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if(!file) {
        return std::nullopt;
    }
    auto raw = std::string(std::istreambuf_iterator<char>(file), {});
    std::erase(raw, '\r');

    constexpr std::string_view separator = "---\n";
    if(!raw.starts_with(separator)) {
        return SnapData{.body = std::move(raw), .created_at = {}};
    }
    auto end = raw.find(separator, separator.size());
    if(end == std::string::npos) {
        return SnapData{.body = std::move(raw), .created_at = {}};
    }

    auto frontmatter = std::string_view(raw).substr(separator.size(), end - separator.size());
    std::string created_at;
    constexpr std::string_view ca_prefix = "created_at: ";
    auto pos = frontmatter.find(ca_prefix);
    if(pos != std::string_view::npos) {
        auto val_start = pos + ca_prefix.size();
        auto val_end = frontmatter.find('\n', val_start);
        created_at = std::string(frontmatter.substr(val_start, val_end - val_start));
    }

    auto body_start = end + separator.size();
    auto body = raw.substr(body_start);
    if(body.ends_with('\n')) {
        body.pop_back();
    }
    return SnapData{.body = std::move(body), .created_at = std::move(created_at)};
}

std::string format_snap(std::string_view source,
                        std::string_view input_file,
                        std::string_view expression,
                        std::string_view content,
                        std::string_view created_at = {}) {
    std::string date_str;
    if(created_at.empty()) {
        auto now = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
        auto ymd = std::chrono::year_month_day{now};
        date_str = std::format("{:%Y-%m-%d}", ymd);
    } else {
        date_str = created_at;
    }

    std::string result = "---\n";
    result += std::format("source: {}\n", source);
    result += std::format("created_at: {}\n", date_str);
    if(!expression.empty()) {
        if(expression.find_first_of(":#{}[]|>&*!?,'\"") != std::string_view::npos) {
            std::string escaped(expression);
            std::string::size_type pos = 0;
            while((pos = escaped.find('\'', pos)) != std::string::npos) {
                escaped.replace(pos, 1, "''");
                pos += 2;
            }
            result += std::format("expression: '{}'\n", escaped);
        } else {
            result += std::format("expression: {}\n", expression);
        }
    }
    if(!input_file.empty()) {
        result += std::format("input_file: {}\n", input_file);
    }
    result += "---\n";
    result += content;
    result += '\n';
    return result;
}

bool write_snap(const fs::path& path, std::string_view content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if(ec) {
        return false;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if(!file) {
        return false;
    }
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return file.good();
}

fs::path snap_dir() {
    return fs::path(global_snapshot_dir);
}

fs::path snap_suite_dir() {
    return snap_dir() / context().suite_name;
}

fs::path snap_test_dir() {
    auto& ctx = context();
    return snap_dir() / ctx.suite_name / ctx.test_name;
}

std::string normalize_newlines(std::string_view s) {
    std::string result(s);
    std::erase(result, '\r');
    return result;
}

std::vector<std::string_view> split_lines(std::string_view s) {
    std::vector<std::string_view> lines;
    std::size_t pos = 0;
    while(pos < s.size()) {
        auto nl = s.find('\n', pos);
        if(nl == std::string_view::npos) {
            lines.push_back(s.substr(pos));
            break;
        }
        lines.push_back(s.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return lines;
}

void print_diff(std::string_view expected, std::string_view actual) {
    auto old_lines = split_lines(expected);
    auto new_lines = split_lines(actual);

    auto max_lines = (std::max)(old_lines.size(), new_lines.size());
    constexpr std::size_t max_diff_lines = 30;

    std::size_t printed = 0;
    std::size_t i = 0;
    while(i < max_lines && printed < max_diff_lines) {
        bool have_old = i < old_lines.size();
        bool have_new = i < new_lines.size();

        if(have_old && have_new && old_lines[i] == new_lines[i]) {
            ++i;
            continue;
        }

        if(have_old && have_new) {
            std::println("           \033[31m-  {}\033[0m", old_lines[i]);
            std::println("           \033[32m+  {}\033[0m", new_lines[i]);
            printed += 2;
        } else if(have_old) {
            std::println("           \033[31m-  {}\033[0m", old_lines[i]);
            ++printed;
        } else {
            std::println("           \033[32m+  {}\033[0m", new_lines[i]);
            ++printed;
        }
        ++i;
    }

    if(i < max_lines) {
        std::println("           ... ({} more differing lines)", max_lines - i);
    }
}

void migrate_snap_extension(const fs::path& target) {
    std::error_code ec;
    if(fs::exists(target, ec)) {
        return;
    }

    auto parent = target.parent_path();
    // target: foo.snap.yml → base: foo
    auto base = target.stem().stem().string();

    for(auto ext: {".yml", ".snap"}) {
        auto old_path = parent / (base + ext);
        if(fs::exists(old_path, ec)) {
            fs::rename(old_path, target, ec);
            if(!ec) {
                std::println("[snapshot] migrated {} -> {}",
                             old_path.filename().string(),
                             target.filename().string());
            }
            return;
        }
    }
}

bool check_impl(const fs::path& snap_path,
                std::string_view value,
                std::string_view input_file,
                std::string_view expression,
                std::source_location loc) {
    migrate_snap_extension(snap_path);
    record_access(snap_path);

    auto existing = read_snap(snap_path);
    auto source = fs::path(loc.file_name()).filename().string();
    auto normalized = normalize_newlines(value);

    if(!existing) {
        auto formatted = format_snap(source, input_file, expression, normalized);
        if(!write_snap(snap_path, formatted)) {
            std::println("[snapshot] failed to write {}", snap_path.string());
            return true;
        }
        std::println("[snapshot] created {}", snap_path.string());
        return false;
    }

    if(existing->body == normalized) {
        std::error_code ec;
        fs::remove(fs::path(snap_path.string() + ".new"), ec);
        return false;
    }

    if(update_snapshots_flag.load(std::memory_order_acquire)) {
        auto formatted =
            format_snap(source, input_file, expression, normalized, existing->created_at);
        if(!write_snap(snap_path, formatted)) {
            std::println("[snapshot] failed to write {}", snap_path.string());
            return true;
        }
        std::println("[snapshot] updated {}", snap_path.string());
        return false;
    }

    auto new_path = fs::path(snap_path.string() + ".new");
    auto formatted = format_snap(source, input_file, expression, normalized);
    bool wrote_new = write_snap(new_path, formatted);

    std::println("[snapshot] mismatch: {}", snap_path.string());
    if(wrote_new) {
        std::println("           new result: {}", new_path.string());
    } else {
        std::println("           failed to write new result file");
    }
    print_diff(existing->body, normalized);
    std::println("           run with --update-snapshots to accept");
    std::println("           at {}:{}", loc.file_name(), loc.line());
    return true;
}

}  // namespace

void reset_snapshot_context(std::string_view suite, std::string_view test, std::string_view file) {
    auto& ctx = context();
    ctx.suite_name = suite;
    ctx.test_name = test;
    ctx.source_file = file;
    ctx.unnamed_used = false;
}

void set_update_snapshots(bool enabled) {
    update_snapshots_flag.store(enabled, std::memory_order_release);
}

void set_snapshot_dir(std::string_view dir) {
    global_snapshot_dir = dir;
}

bool check_snapshot(std::string_view value, std::string_view name, std::source_location loc) {
    auto& ctx = context();

    if(global_snapshot_dir.empty()) {
        std::println("[snapshot] error: no snapshot directory configured (use --snapshot-dir)");
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    if(ctx.suite_name.empty()) {
        std::println("[snapshot] error: no snapshot context (used outside ZEST_CASE?)");
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    if(name.empty()) {
        if(ctx.unnamed_used) {
            std::println("[snapshot] error: duplicate unnamed snapshot in same ZEST_CASE");
            std::println(
                "           use ASSERT_SNAPSHOT(value, \"name\") for additional snapshots");
            std::println("           at {}:{}", loc.file_name(), loc.line());
            return true;
        }
        ctx.unnamed_used = true;
        auto path = snap_suite_dir() / (ctx.test_name + ".snap.yml");
        return check_impl(path, value, "", "", loc);
    }

    if(name.find_first_of("/\\:*?\"<>|") != std::string_view::npos) {
        std::println("[snapshot] error: snapshot name contains unsafe characters: `{}`", name);
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    auto path = snap_test_dir() / (std::string(name) + ".snap.yml");
    return check_impl(path, value, "", "", loc);
}

bool check_snapshot_expr(std::string_view value,
                         std::string_view expression,
                         std::string_view name,
                         std::source_location loc) {
    auto& ctx = context();

    if(global_snapshot_dir.empty()) {
        std::println("[snapshot] error: no snapshot directory configured (use --snapshot-dir)");
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    if(ctx.suite_name.empty()) {
        std::println("[snapshot] error: no snapshot context (used outside ZEST_CASE?)");
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    if(name.empty()) {
        if(ctx.unnamed_used) {
            std::println("[snapshot] error: duplicate unnamed snapshot in same ZEST_CASE");
            std::println(
                "           use ASSERT_SNAPSHOT(value, \"name\") for additional snapshots");
            std::println("           at {}:{}", loc.file_name(), loc.line());
            return true;
        }
        ctx.unnamed_used = true;
        auto path = snap_suite_dir() / (ctx.test_name + ".snap.yml");
        return check_impl(path, value, "", expression, loc);
    }

    if(name.find_first_of("/\\:*?\"<>|") != std::string_view::npos) {
        std::println("[snapshot] error: snapshot name contains unsafe characters: `{}`", name);
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    auto path = snap_test_dir() / (std::string(name) + ".snap.yml");
    return check_impl(path, value, "", expression, loc);
}

bool check_snapshot_glob(std::string_view base_dir_str,
                         std::string_view pattern,
                         const std::function<std::string(std::string_view)>& transform,
                         std::source_location loc) {
    auto& ctx = context();

    if(global_snapshot_dir.empty()) {
        std::println("[snapshot] error: no snapshot directory configured (use --snapshot-dir)");
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    if(ctx.suite_name.empty()) {
        std::println("[snapshot] error: no snapshot context (used outside ZEST_CASE?)");
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    auto glob = GlobPattern::create(pattern);
    if(!glob) {
        std::println("[snapshot] error: invalid glob pattern `{}`", pattern);
        std::println("           {}", glob.error().message);
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    auto scan_dir = fs::path(base_dir_str);

    std::error_code ec;
    auto iter = fs::recursive_directory_iterator(scan_dir, ec);
    if(ec) {
        std::println("[snapshot] error: cannot iterate `{}`", scan_dir.string());
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    auto snap_base = snap_test_dir();

    std::vector<fs::path> matched;
    for(auto& entry: iter) {
        if(entry.is_directory() &&
           entry.path().lexically_normal() == snap_dir().lexically_normal()) {
            iter.disable_recursion_pending();
            continue;
        }
        if(!entry.is_regular_file()) {
            continue;
        }
        std::error_code rel_ec;
        auto rel = fs::relative(entry.path(), scan_dir, rel_ec);
        if(rel_ec) {
            continue;
        }
        if(glob->match(rel.generic_string())) {
            matched.emplace_back(rel);
        }
    }

    std::ranges::sort(matched);

    if(matched.empty()) {
        std::println("[snapshot] error: no files matched pattern `{}`", pattern);
        std::println("           scan dir: {}", scan_dir.string());
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    bool failed = false;
    for(auto& rel: matched) {
        auto full_path = (scan_dir / rel).string();
        auto value = transform(full_path);
        auto snap_path = snap_base / (rel.generic_string() + ".snap.yml");
        if(check_impl(snap_path, value, rel.generic_string(), "", loc)) {
            failed = true;
        }
    }
    return failed;
}

std::vector<std::string> take_accessed_snapshots() {
    std::lock_guard lock(accessed_mutex);
    std::vector<std::string> paths(accessed_snap_paths.begin(), accessed_snap_paths.end());
    accessed_snap_paths.clear();
    return paths;
}

void record_snapshot_access(std::string_view path) {
    record_access(fs::path(path));
}

std::size_t cleanup_unused_snapshots() {
    std::lock_guard lock(accessed_mutex);

    std::size_t removed = 0;
    for(auto& dir_str: accessed_snap_dirs) {
        fs::path dir(dir_str);
        std::error_code ec;
        if(!fs::is_directory(dir, ec)) {
            continue;
        }
        for(auto& entry: fs::recursive_directory_iterator(dir, ec)) {
            if(!entry.is_regular_file()) {
                continue;
            }
            auto normalized = entry.path().lexically_normal().string();
            if(accessed_snap_paths.contains(normalized)) {
                continue;
            }
            auto ext = entry.path().extension();
            auto stem_ext = entry.path().stem().extension();
            bool is_snapshot = (ext == ".yml" && stem_ext == ".snap") || ext == ".snap";
            if(!is_snapshot) {
                continue;
            }
            std::println("[snapshot] removing orphan: {}", entry.path().filename().string());
            fs::remove(entry.path(), ec);
            if(ec) {
                std::println("[snapshot] warning: failed to remove {}: {}",
                             entry.path().string(),
                             ec.message());
            } else {
                ++removed;
            }
        }
    }
    return removed;
}

}  // namespace kota::zest
