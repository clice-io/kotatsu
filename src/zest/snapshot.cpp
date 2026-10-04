#include "kota/zest/snapshot/snapshot.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <print>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "execution.h"
#include "kota/support/glob_pattern.h"

namespace kota::zest {

namespace fs = std::filesystem;

namespace {

struct SnapshotContext {
    std::string suite_name;
    std::string test_name;
    bool unnamed_used = false;
};

std::atomic<bool> update_snapshots_flag{false};
std::string global_snapshot_dir;

/// The running test's, whichever thread checks a snapshot.
std::mutex context_mutex;
SnapshotContext snapshot_context;

std::mutex accessed_mutex;
std::set<std::string> accessed_snap_paths;
std::set<std::string> accessed_snap_dirs;

void record_access(const fs::path& snap_path) {
    auto path = snap_path.lexically_normal().string();
    if(auto notice = snapshot_notice.load()) {
        notice(path);
    }
    std::lock_guard lock(accessed_mutex);
    accessed_snap_paths.insert(std::move(path));
    auto dir = snap_path.parent_path().lexically_normal();
    auto root = fs::path(global_snapshot_dir).lexically_normal();
    while(!dir.empty() && dir != root && dir.has_relative_path()) {
        accessed_snap_dirs.insert(dir.string());
        dir = dir.parent_path();
    }
    accessed_snap_dirs.insert(root.string());
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

/// The lines that differ, as many as a report has room for.
std::string diff(std::string_view expected, std::string_view actual) {
    auto old_lines = split_lines(expected);
    auto new_lines = split_lines(actual);

    auto max_lines = (std::max)(old_lines.size(), new_lines.size());
    constexpr std::size_t max_diff_lines = 30;

    std::string text;
    std::size_t printed = 0;
    std::size_t i = 0;
    while(i < max_lines && printed < max_diff_lines) {
        bool have_old = i < old_lines.size();
        bool have_new = i < new_lines.size();

        if(have_old && have_new && old_lines[i] == new_lines[i]) {
            ++i;
            continue;
        }

        if(have_old) {
            text += std::format("\033[31m-  {}\033[0m\n", old_lines[i]);
            ++printed;
        }
        if(have_new) {
            text += std::format("\033[32m+  {}\033[0m\n", new_lines[i]);
            ++printed;
        }
        ++i;
    }

    if(i < max_lines) {
        text += std::format("... ({} more differing lines)\n", max_lines - i);
    }
    return text;
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

/// Checks `value` against the file `snap_path`; returns the report if the
/// check fails.
std::optional<std::string> check_impl(const fs::path& snap_path,
                                      std::string_view value,
                                      std::string_view input_file,
                                      std::source_location loc) {
    // The test's threads may check one file at once.
    static std::mutex files_mutex;
    std::lock_guard lock(files_mutex);
    migrate_snap_extension(snap_path);
    record_access(snap_path);

    auto existing = read_snap(snap_path);
    auto source = fs::path(loc.file_name()).filename().string();
    auto normalized = normalize_newlines(value);

    if(!existing) {
        if(!write_snap(snap_path, format_snap(source, input_file, normalized))) {
            return std::format("cannot write {}", snap_path.string());
        }
        std::println("[snapshot] created {}", snap_path.string());
        return std::nullopt;
    }

    if(existing->body == normalized) {
        std::error_code ec;
        fs::remove(fs::path(snap_path.string() + ".new"), ec);
        return std::nullopt;
    }

    if(update_snapshots_flag.load(std::memory_order_acquire)) {
        auto formatted = format_snap(source, input_file, normalized, existing->created_at);
        if(!write_snap(snap_path, formatted)) {
            return std::format("cannot write {}", snap_path.string());
        }
        std::println("[snapshot] updated {}", snap_path.string());
        return std::nullopt;
    }

    auto new_path = fs::path(snap_path.string() + ".new");
    auto report = std::format("mismatch: {}\n", snap_path.string());
    if(write_snap(new_path, format_snap(source, input_file, normalized))) {
        report += std::format("new result: {}\n", new_path.string());
    } else {
        report += std::format("cannot write the new result to {}\n", new_path.string());
    }
    report += diff(existing->body, normalized);
    report += "run with --update-snapshots to accept";
    return report;
}

/// A check that held if it reported nothing.
Match matched(std::optional<std::string> report) {
    return Match{
        .held = !report,
        .explain = [report = std::move(report)] { return report.value_or(""); },
    };
}

/// Where the running test's snapshots go, or why they cannot.
std::expected<SnapshotContext, std::string> current_context() {
    if(global_snapshot_dir.empty()) {
        return std::unexpected("no snapshot directory: run with --snapshot-dir");
    }
    std::lock_guard lock(context_mutex);
    if(snapshot_context.suite_name.empty()) {
        return std::unexpected("no running test to take the snapshot of");
    }
    return snapshot_context;
}

}  // namespace

namespace detail {

void reset_snapshot_context(std::string_view suite, std::string_view test) {
    std::lock_guard lock(context_mutex);
    snapshot_context = {
        .suite_name = std::string(suite),
        .test_name = std::string(test),
    };
}

bool set_update_snapshots(bool enabled) {
    return update_snapshots_flag.exchange(enabled, std::memory_order_acq_rel);
}

void set_snapshot_dir(std::string_view dir) {
    global_snapshot_dir = dir;
}

Match check_snapshot(std::string_view text, std::string_view name, std::source_location location) {
    auto context = current_context();
    if(!context) {
        return matched(std::move(context.error()));
    }
    auto suite_dir = snap_dir() / context->suite_name;

    if(name.empty()) {
        {
            std::lock_guard lock(context_mutex);
            if(std::exchange(snapshot_context.unnamed_used, true)) {
                return matched(
                    R"(a test has one unnamed snapshot: name this one, as in snapshot(value, "name"))");
            }
        }
        return matched(
            check_impl(suite_dir / (context->test_name + ".snap.yml"), text, "", location));
    }

    if(name.find_first_of(R"(/\:*?"<>|)") != std::string_view::npos) {
        return matched(
            std::format("the snapshot name `{}` holds a character file names cannot", name));
    }
    return matched(check_impl(suite_dir / context->test_name / (std::string(name) + ".snap.yml"),
                              text,
                              "",
                              location));
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

}  // namespace detail

Match snapshot_glob(std::string_view dir,
                    std::string_view pattern,
                    const std::function<std::string(std::string_view)>& transform,
                    std::source_location location) {
    auto context = current_context();
    if(!context) {
        return matched(std::move(context.error()));
    }

    auto glob = GlobPattern::create(pattern);
    if(!glob) {
        return matched(std::format("invalid glob pattern `{}`: {}", pattern, glob.error().message));
    }

    auto scan_dir = fs::path(dir);
    std::error_code ec;
    auto iter = fs::recursive_directory_iterator(scan_dir, ec);
    if(ec) {
        return matched(std::format("cannot list `{}`: {}", scan_dir.string(), ec.message()));
    }

    std::vector<fs::path> files;
    for(auto& entry: iter) {
        if(entry.is_directory() &&
           entry.path().lexically_normal() == snap_dir().lexically_normal()) {
            iter.disable_recursion_pending();
            continue;
        }
        if(!entry.is_regular_file()) {
            continue;
        }
        // Lexically: fs::relative resolves symlinks, so a file linked from
        // elsewhere (as in Bazel's runfiles) would be named by its target.
        auto rel = entry.path().lexically_relative(scan_dir);
        if(glob->match(rel.generic_string())) {
            files.emplace_back(rel);
        }
    }
    if(files.empty()) {
        return matched(std::format("no file under `{}` matches `{}`", scan_dir.string(), pattern));
    }
    std::ranges::sort(files);

    auto test_dir = snap_dir() / context->suite_name / context->test_name;
    std::optional<std::string> report;
    for(auto& rel: files) {
        auto value = transform((scan_dir / rel).string());
        auto snap_path = test_dir / (rel.generic_string() + ".snap.yml");
        if(auto failed = check_impl(snap_path, value, rel.generic_string(), location)) {
            report = report ? std::format("{}\n{}", *report, *failed) : std::move(*failed);
        }
    }
    return matched(std::move(report));
}

void record_snapshot_access(std::string_view path) {
    record_access(fs::path(path));
}

}  // namespace kota::zest
