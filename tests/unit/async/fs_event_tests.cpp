#include <fcntl.h>
#include <filesystem>
#include <string>
#include <variant>

#include "loop_fixture.h"
#include "kota/zest/zest.h"
#include "kota/async/io/fs_event.h"

namespace kota {

namespace {

// Returns empty vector on timeout (not an error) — see fs_event_dir_tests.cpp.
task<std::vector<fs_event::change>, error> next_or_timeout(fs_event& w,
                                                           event_loop& loop,
                                                           int timeout_ms = 10000) {
    auto do_timeout = [&]() -> task<std::vector<fs_event::change>, error> {
        co_await sleep(timeout_ms, loop);
        co_return std::vector<fs_event::change>{};
    };

    auto result = co_await when_any(w.next(), do_timeout());
    if(result.has_error()) {
        co_await fail(result.error());
    }

    co_return std::visit([](auto&& v) -> std::vector<fs_event::change> { return std::move(v); },
                         std::move(*result));
}

bool has_effect(const std::vector<fs_event::change>& changes, fs_event::effect eff) {
    for(const auto& c: changes) {
        if(c.type == eff)
            return true;
    }
    return false;
}

// ── detect modification of an existing file ────────────────────────

task<int, error> fse_detect_modify(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-fe-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string file = (std::filesystem::path(dir) / "target.json").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::write(fd, std::span<const char>("initial", 7), -1, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(file, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value())
        co_await fail(watcher.error());

    co_await sleep(500, loop);

    fd = co_await fs::open(file, O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::write(fd, std::span<const char>("updated", 7), -1, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return has_effect(changes, fs_event::effect::modify) ? 1 : 0;
}

// ── detect file creation (file does not exist initially) ───────────

task<int, error> fse_detect_create(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-fe-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string file = (std::filesystem::path(dir) / "new_file.json").string();

    auto watcher = fs_event::create(file, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value())
        co_await fail(watcher.error());

    co_await sleep(500, loop);

    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::write(fd, std::span<const char>("hello", 5), -1, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return has_effect(changes, fs_event::effect::create) ? 1 : 0;
}

// ── detect file deletion ───────────────────────────────────────────

task<int, error> fse_detect_destroy(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-fe-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string file = (std::filesystem::path(dir) / "doomed.json").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(file, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value())
        co_await fail(watcher.error());

    co_await sleep(500, loop);

    co_await fs::unlink(file, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    watcher->stop();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return has_effect(changes, fs_event::effect::destroy) ? 1 : 0;
}

// ── ignores sibling file changes ───────────────────────────────────

task<int, error> fse_ignores_sibling(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-fe-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string target = (std::filesystem::path(dir) / "target.json").string();
    std::string sibling = (std::filesystem::path(dir) / "sibling.txt").string();

    int fd = co_await fs::open(target, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(target, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value())
        co_await fail(watcher.error());

    co_await sleep(500, loop);

    fd = co_await fs::open(sibling, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::write(fd, std::span<const char>("noise", 5), -1, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    co_await sleep(200, loop);

    fd = co_await fs::open(target, O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::write(fd, std::span<const char>("signal", 6), -1, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    watcher->stop();
    co_await fs::unlink(target, loop).or_fail();
    co_await fs::unlink(sibling, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    if(!has_effect(changes, fs_event::effect::modify))
        co_return 0;

    for(const auto& c: changes) {
        if(c.path.find("sibling") != std::string::npos)
            co_return 0;
    }

    co_return 1;
}

// ── atomic replace (write tmp + rename) ────────────────────────────

task<int, error> fse_atomic_replace(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-fe-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string file = (std::filesystem::path(dir) / "config.json").string();
    std::string tmp = (std::filesystem::path(dir) / "config.json.tmp").string();

    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::write(fd, std::span<const char>("v1", 2), -1, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(file, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value())
        co_await fail(watcher.error());

    co_await sleep(500, loop);

    fd = co_await fs::open(tmp, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::write(fd, std::span<const char>("v2", 2), -1, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();
    co_await fs::rename(tmp, file, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return (has_effect(changes, fs_event::effect::create) ||
               has_effect(changes, fs_event::effect::rename))
        ? 1
        : 0;
}

// ── error on nonexistent parent directory ──────────────────────────

task<int, error> fse_error_bad_parent(event_loop& loop) {
    auto result = fs_event::create("/nonexistent/path/file.txt", {}, loop);
    co_return result.has_error() ? 1 : 0;
}

// ── stop then next returns error ───────────────────────────────────

task<int, error> fse_stop_then_next(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-fe-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string file = (std::filesystem::path(dir) / "test.json").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(file, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value())
        co_await fail(watcher.error());

    watcher->stop();
    auto result = co_await watcher->next();

    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return result.has_error() ? 1 : 0;
}

}  // namespace

TEST_SUITE(fs_event_io, loop_fixture) {

TEST_CASE(detect_modify) {
    auto worker = fse_detect_modify(loop);
    schedule_all(worker);
    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(detect_create) {
    auto worker = fse_detect_create(loop);
    schedule_all(worker);
    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(detect_destroy) {
    auto worker = fse_detect_destroy(loop);
    schedule_all(worker);
    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(ignores_sibling) {
    auto worker = fse_ignores_sibling(loop);
    schedule_all(worker);
    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(atomic_replace) {
    auto worker = fse_atomic_replace(loop);
    schedule_all(worker);
    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(error_bad_parent) {
    auto worker = fse_error_bad_parent(loop);
    schedule_all(worker);
    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(stop_then_next) {
    auto worker = fse_stop_then_next(loop);
    schedule_all(worker);
    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

};  // TEST_SUITE(fs_event_io)

}  // namespace kota
