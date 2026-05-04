#include "kota/zest/detail/snapshot.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <print>
#include <string>
#include <vector>

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
    return fs::path(context().source_file).parent_path() / "snapshots";
}

std::string normalize_newlines(std::string_view s) {
    std::string result(s);
    std::erase(result, '\r');
    return result;
}

bool check_impl(const fs::path& snap_path,
                std::string_view value,
                std::string_view input_file,
                std::source_location loc) {
    auto existing = read_snap(snap_path);
    auto source = fs::path(loc.file_name()).filename().string();
    auto normalized = normalize_newlines(value);

    if(!existing) {
        auto formatted = format_snap(source, input_file, normalized);
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
        auto formatted = format_snap(source, input_file, normalized, existing->created_at);
        if(!write_snap(snap_path, formatted)) {
            std::println("[snapshot] failed to write {}", snap_path.string());
            return true;
        }
        std::println("[snapshot] updated {}", snap_path.string());
        return false;
    }

    auto new_path = fs::path(snap_path.string() + ".new");
    auto formatted = format_snap(source, input_file, normalized);
    bool wrote_new = write_snap(new_path, formatted);

    std::println("[snapshot] mismatch: {}", snap_path.string());
    if(wrote_new) {
        std::println("           new result: {}", new_path.string());
    } else {
        std::println("           failed to write new result file");
    }
    std::println("           run with --update-snapshots to accept");
    std::println("           at {}:{}", loc.file_name(), loc.line());
    return true;
}

}  // namespace

void reset_snapshot_context(std::string_view suite, std::string_view test, std::string_view file) {
    auto& ctx = context();
    ctx.suite_name = suite;
    ctx.test_name = test;

    fs::path p(file);
#ifdef KOTA_ZEST_BUILD_ROOT
    if(!file.empty() && p.is_relative()) {
        p = (fs::path(KOTA_ZEST_BUILD_ROOT) / p).lexically_normal();
    }
#endif
    ctx.source_file = p.string();
    ctx.unnamed_used = false;
}

void set_update_snapshots(bool enabled) {
    update_snapshots_flag.store(enabled, std::memory_order_release);
}

bool check_snapshot(std::string_view value, std::string_view name, std::source_location loc) {
    auto& ctx = context();

    if(ctx.source_file.empty()) {
        std::println("[snapshot] error: no snapshot context (used outside TEST_CASE?)");
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    auto stem = fs::path(ctx.source_file).stem().string();

    if(name.empty()) {
        if(ctx.unnamed_used) {
            std::println("[snapshot] error: duplicate unnamed snapshot in same TEST_CASE");
            std::println(
                "           use ASSERT_SNAPSHOT(value, \"name\") for additional snapshots");
            std::println("           at {}:{}", loc.file_name(), loc.line());
            return true;
        }
        ctx.unnamed_used = true;
        auto filename = std::format("{}__{}__{}.snap", stem, ctx.suite_name, ctx.test_name);
        return check_impl(snap_dir() / filename, value, "", loc);
    }

    if(name.find_first_of("/\\:*?\"<>|") != std::string_view::npos) {
        std::println("[snapshot] error: snapshot name contains unsafe characters: `{}`", name);
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    auto filename = std::format("{}__{}__{}__{}.snap", stem, ctx.suite_name, ctx.test_name, name);
    return check_impl(snap_dir() / filename, value, "", loc);
}

bool check_snapshot_glob(std::string_view pattern,
                         const std::function<std::string(std::string_view)>& transform,
                         std::source_location loc) {
    auto& ctx = context();

    if(ctx.source_file.empty()) {
        std::println("[snapshot] error: no snapshot context (used outside TEST_CASE?)");
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

    auto base_dir = fs::path(ctx.source_file).parent_path();
    std::error_code ec;
    auto iter = fs::recursive_directory_iterator(base_dir, ec);
    if(ec) {
        std::println("[snapshot] error: cannot iterate `{}`", base_dir.string());
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    auto snap_base = snap_dir();

    std::vector<fs::path> matched;
    for(auto& entry: iter) {
        if(entry.is_directory() && entry.path() == snap_base) {
            iter.disable_recursion_pending();
            continue;
        }
        if(!entry.is_regular_file()) {
            continue;
        }
        std::error_code rel_ec;
        auto rel = fs::relative(entry.path(), base_dir, rel_ec);
        if(rel_ec) {
            continue;
        }
        auto rel_str = rel.generic_string();
        if(glob->match(rel_str)) {
            matched.push_back(std::move(rel));
        }
    }

    std::ranges::sort(matched);

    if(matched.empty()) {
        std::println("[snapshot] error: no files matched pattern `{}`", pattern);
        std::println("           base dir: {}", base_dir.string());
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    bool failed = false;
    for(auto& rel: matched) {
        auto full_path = (base_dir / rel).string();
        auto value = transform(full_path);
        auto snap_path = snap_base / (rel.generic_string() + ".snap");
        if(check_impl(snap_path, value, rel.generic_string(), loc)) {
            failed = true;
        }
    }
    return failed;
}

}  // namespace kota::zest
