#include "kota/zest/detail/snapshot.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <print>
#include <string>

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

std::optional<std::string> read_snap(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if(!file) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), {});
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

bool check_impl(const fs::path& snap_path, std::string_view value, std::source_location loc) {
    auto existing = read_snap(snap_path);

    if(!existing) {
        if(!write_snap(snap_path, value)) {
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
        write_snap(snap_path, value);
        std::println("[snapshot] updated {}", snap_path.string());
        return false;
    }

    auto new_path = fs::path(snap_path.string() + ".new");
    write_snap(new_path, value);

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
            std::println("           use ASSERT_SNAPSHOT(value, \"name\") for additional snapshots");
            std::println("           at {}:{}", loc.file_name(), loc.line());
            return true;
        }
        ctx.unnamed_used = true;
        auto filename = std::format("{}__{}.snap", ctx.suite_name, ctx.test_name);
        return check_impl(snap_dir() / filename, value, loc);
    }

    return check_impl(snap_dir() / std::format("{}.snap", name), value, loc);
}

}  // namespace kota::zest
