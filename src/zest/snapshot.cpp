#include "kota/zest/detail/snapshot.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
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

struct SnapshotContext {
    std::string suite_name;
    std::string test_name;
    std::string source_file;
    bool unnamed_used = false;
};

namespace {

std::atomic<bool> g_update_snapshots{false};

SnapshotContext& context() {
    thread_local SnapshotContext ctx;
    return ctx;
}

std::optional<std::string> read_snap_body(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if(!file) {
        return std::nullopt;
    }
    auto raw = std::string(std::istreambuf_iterator<char>(file), {});

    constexpr std::string_view separator = "---\n";
    if(!raw.starts_with(separator)) {
        return raw;
    }
    auto end = raw.find(separator, separator.size());
    if(end == std::string::npos) {
        return raw;
    }
    auto body_start = end + separator.size();
    auto body = raw.substr(body_start);
    if(body.ends_with('\n')) {
        body.pop_back();
    }
    return body;
}

std::string format_snap(std::string_view source,
                        std::string_view input_file,
                        std::string_view content) {
    std::string result = "---\n";
    result += std::format("source: {}\n", source);
    if(!input_file.empty()) {
        result += std::format("input_file: {}\n", input_file);
    }
    result += "---\n";
    result += content;
    if(!content.empty() && !content.ends_with('\n')) {
        result += '\n';
    }
    return result;
}

bool write_snap(const fs::path& path, std::string_view content) {
    fs::create_directories(path.parent_path());
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

bool check_impl(const fs::path& snap_path,
                std::string_view value,
                std::string_view input_file,
                std::source_location loc) {
    auto existing = read_snap_body(snap_path);

    auto source = fs::path(loc.file_name()).filename().string();
    auto formatted = format_snap(source, input_file, value);

    if(!existing) {
        if(!write_snap(snap_path, formatted)) {
            std::println("[snapshot] failed to write {}", snap_path.string());
            return true;
        }
        std::println("[snapshot] created {}", snap_path.string());
        return false;
    }

    if(*existing == value) {
        return false;
    }

    if(g_update_snapshots.load(std::memory_order_relaxed)) {
        write_snap(snap_path, formatted);
        std::println("[snapshot] updated {}", snap_path.string());
        return false;
    }

    auto new_path = fs::path(snap_path.string() + ".new");
    write_snap(new_path, formatted);

    std::println("[snapshot] mismatch: {}", snap_path.string());
    std::println("           new result: {}", new_path.string());
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
    g_update_snapshots.store(enabled, std::memory_order_relaxed);
}

bool check_snapshot(std::string_view value, std::string_view name, std::source_location loc) {
    auto& ctx = context();

    if(ctx.source_file.empty()) {
        std::println("[snapshot] error: no snapshot context (used outside TEST_CASE?)");
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return true;
    }

    if(name.empty()) {
        if(ctx.unnamed_used) {
            std::println("[snapshot] error: duplicate ASSERT_SNAPSHOT in same TEST_CASE");
            std::println(
                "           use ASSERT_SNAPSHOT(value, \"name\") for additional snapshots");
            std::println("           at {}:{}", loc.file_name(), loc.line());
            return true;
        }
        ctx.unnamed_used = true;
        auto filename = std::format("{}__{}.snap", ctx.suite_name, ctx.test_name);
        return check_impl(snap_dir() / filename, value, "", loc);
    }

    return check_impl(snap_dir() / std::format("{}.snap", name), value, "", loc);
}

bool check_snapshot_glob(std::string_view pattern,
                         std::function<std::string(std::string_view)> transform,
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

    std::vector<fs::path> matched;
    for(auto& entry: iter) {
        if(!entry.is_regular_file()) {
            continue;
        }
        auto rel = fs::relative(entry.path(), base_dir, ec);
        if(ec) {
            continue;
        }
        auto rel_str = rel.generic_string();
        if(glob->match(rel_str)) {
            matched.push_back(std::move(rel));
        }
    }

    std::ranges::sort(matched);

    if(matched.empty()) {
        std::println("[snapshot] warning: no files matched pattern `{}`", pattern);
        std::println("           base dir: {}", base_dir.string());
        std::println("           at {}:{}", loc.file_name(), loc.line());
        return false;
    }

    bool failed = false;
    auto snap_base = snap_dir();
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
