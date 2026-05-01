#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <string>

#include "fs_event_fixture.h"
#include "loop_fixture.h"
#include "kota/zest/zest.h"

namespace kota {

namespace {

task<int, error> watch_file_create(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string file = (std::filesystem::path(dir) / "test.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = has_effect(changes, fs_event::effect::create);

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_file_modify(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string file = (std::filesystem::path(dir) / "modify.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    fd = co_await fs::open(file, O_WRONLY, 0, loop).or_fail();
    constexpr std::string_view payload = "hello";
    co_await fs::write(fd, std::span<const char>(payload.data(), payload.size()), -1, loop)
        .or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = has_effect(changes, fs_event::effect::modify);

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_file_delete(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string file = (std::filesystem::path(dir) / "delete.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    co_await fs::unlink(file, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = has_effect(changes, fs_event::effect::destroy);

    watcher->stop();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_file_rename(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string src = (std::filesystem::path(dir) / "before.txt").string();
    std::string dst = (std::filesystem::path(dir) / "after.txt").string();

    int fd = co_await fs::open(src, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    co_await fs::rename(src, dst, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found_relevant = has_effect(changes, fs_event::effect::rename) ||
                          has_effect(changes, fs_event::effect::create) ||
                          has_effect(changes, fs_event::effect::destroy);

    watcher->stop();
    co_await fs::unlink(dst, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found_relevant ? 1 : 0;
}

task<int, error> watch_nonexistent_path(event_loop& loop) {
    auto result = fs_event::create("/nonexistent/dir/that/does/not/exist", {}, loop);
    if(result.has_value()) {
        co_return 0;
    }

    co_return result.error() == error::no_such_file_or_directory ? 1 : 0;
}

task<int, error> watch_close_then_next(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    watcher->stop();

    auto result = co_await watcher->next();

    co_await fs::rmdir(dir, loop).or_fail();

    if(!result.has_error()) {
        co_return 0;
    }

    co_return result.error() == error::invalid_argument ? 1 : 0;
}

task<int, error> watch_stop_during_next(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    // Schedule stop() after a short delay. The key invariant is that
    // next() must return (not deadlock). It may return buffered events
    // (if the platform delivered some before stop) or operation_aborted.
    auto stopper = [&]() -> task<void, error> {
        co_await sleep(100, loop);
        watcher->stop();
    };
    auto stop_task = stopper();
    loop.schedule(stop_task);

    auto result = co_await next_or_timeout(*watcher, loop, 5000);

    co_await fs::rmdir(dir, loop).or_fail();

    if(result.has_error()) {
        co_return result.error() == error::operation_aborted ? 1 : 0;
    }
    co_return 1;
}

task<int, error> watch_stop_during_debounce(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{2000}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string file = (std::filesystem::path(dir) / "debounce_stop.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    // Wait a bit for events to arrive and next() to enter debounce phase,
    // then stop while debounce_timer.wait() is suspended.
    auto stopper = [&]() -> task<void, error> {
        co_await sleep(200, loop);
        watcher->stop();
    };
    auto stop_task = stopper();
    loop.schedule(stop_task);

    auto result = co_await next_or_timeout(*watcher, loop, 5000);

    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    if(result.has_error()) {
        co_return result.error() == error::operation_aborted ? 1 : 0;
    }
    co_return 1;
}

task<int, error> watch_multiple_next_calls(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string file1 = (std::filesystem::path(dir) / "a.txt").string();
    int fd = co_await fs::open(file1, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto batch1 = co_await next_or_timeout(*watcher, loop).or_fail();
    if(batch1.empty()) {
        co_return 0;
    }

    std::string file2 = (std::filesystem::path(dir) / "b.txt").string();
    fd = co_await fs::open(file2, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto batch2 = co_await next_or_timeout(*watcher, loop).or_fail();
    if(batch2.empty()) {
        co_return 0;
    }

    watcher->stop();
    co_await fs::unlink(file1, loop).or_fail();
    co_await fs::unlink(file2, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return 1;
}

task<int, error> watch_debounce_batching(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{200}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string file1 = (std::filesystem::path(dir) / "x.txt").string();
    std::string file2 = (std::filesystem::path(dir) / "y.txt").string();
    std::string file3 = (std::filesystem::path(dir) / "z.txt").string();

    int fd = co_await fs::open(file1, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();
    fd = co_await fs::open(file2, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();
    fd = co_await fs::open(file3, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    watcher->stop();
    co_await fs::unlink(file1, loop).or_fail();
    co_await fs::unlink(file2, loop).or_fail();
    co_await fs::unlink(file3, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return changes.size() >= 3 ? 1 : 0;
}

task<int, error> watch_subdirectory_changes(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string subdir = (std::filesystem::path(dir) / "sub").string();
    co_await fs::mkdir(subdir, 0755, loop).or_fail();

    co_await wait_for_watcher_ready(loop);

    std::string file = (std::filesystem::path(subdir) / "nested.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found_subdir_event = std::ranges::any_of(changes, [](const auto& c) {
        return c.path.find("sub") != std::string::npos;
    });

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(subdir, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found_subdir_event ? 1 : 0;
}

task<int, error> watch_non_recursive(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string subdir = (std::filesystem::path(dir) / "child").string();
    co_await fs::mkdir(subdir, 0755, loop).or_fail();

    auto watcher =
        fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}, false}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    // Create file in subdir first (should be excluded)
    std::string deep_file = (std::filesystem::path(subdir) / "deep.txt").string();
    int fd = co_await fs::open(deep_file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    co_await sleep(200, loop);

    // Create file at top level (should be visible)
    std::string file = (std::filesystem::path(dir) / "top.txt").string();
    fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found_top = std::ranges::any_of(changes, [](const auto& c) {
        return c.path.find("top.txt") != std::string::npos;
    });
    bool found_deep = std::ranges::any_of(changes, [](const auto& c) {
        return c.path.find("deep.txt") != std::string::npos;
    });

    watcher->stop();
    co_await fs::unlink(deep_file, loop).or_fail();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(subdir, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return (found_top && !found_deep) ? 1 : 0;
}

task<int, error> watch_move_assignment(event_loop& loop) {
    auto dir_template1 = (std::filesystem::temp_directory_path() / "kotatsu-dw1-XXXXXX").string();
    auto dir_template2 = (std::filesystem::temp_directory_path() / "kotatsu-dw2-XXXXXX").string();
    std::string dir1 = co_await fs::mkdtemp(dir_template1, loop).or_fail();
    std::string dir2 = co_await fs::mkdtemp(dir_template2, loop).or_fail();

    auto watcher_a = fs_event::create(dir1, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher_a.has_value()) {
        co_await fail(watcher_a.error());
    }

    auto watcher_b = fs_event::create(dir2, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher_b.has_value()) {
        co_await fail(watcher_b.error());
    }

    *watcher_a = std::move(*watcher_b);

    co_await wait_for_watcher_ready(loop);

    std::string file = (std::filesystem::path(dir2) / "moved.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher_a, loop).or_fail();

    bool found = has_effect(changes, fs_event::effect::create);

    watcher_a->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir1, loop).or_fail();
    co_await fs::rmdir(dir2, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_destructor_cleanup(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    {
        auto watcher =
            fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
        if(!watcher.has_value()) {
            co_await fail(watcher.error());
        }
    }

    co_await fs::rmdir(dir, loop).or_fail();

    co_return 1;
}

task<int, error> watch_double_close(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    watcher->stop();
    watcher->stop();

    co_await fs::rmdir(dir, loop).or_fail();

    co_return 1;
}

task<int, error> watch_default_options(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, {}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    watcher->stop();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return 1;
}

task<int, error> watch_rename_populates_old_path(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string src = (std::filesystem::path(dir) / "old_name.txt").string();
    std::string dst = (std::filesystem::path(dir) / "new_name.txt").string();

    int fd = co_await fs::open(src, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    co_await fs::rename(src, dst, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found_rename = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::rename &&
               c.path.find("new_name.txt") != std::string::npos;
    });
    bool found_old_path = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::rename &&
               c.path.find("new_name.txt") != std::string::npos &&
               c.old_path.find("old_name.txt") != std::string::npos;
    });

    watcher->stop();
    co_await fs::unlink(dst, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return (found_rename && found_old_path) ? 1 : 0;
}

task<int, error> watch_rename_existing_file(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string src = (std::filesystem::path(dir) / "existing.txt").string();
    int fd = co_await fs::open(src, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    co_await sleep(100, loop);

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string dst = (std::filesystem::path(dir) / "renamed.txt").string();
    co_await fs::rename(src, dst, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = has_effect(changes, fs_event::effect::rename) ||
                 has_effect(changes, fs_event::effect::create) ||
                 has_effect(changes, fs_event::effect::destroy);

    watcher->stop();
    co_await fs::unlink(dst, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_directory_creation(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string sub = (std::filesystem::path(dir) / "newdir").string();
    co_await fs::mkdir(sub, 0755, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::create && c.path.find("newdir") != std::string::npos;
    });

    watcher->stop();
    co_await fs::rmdir(sub, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_directory_rename(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string sub1 = (std::filesystem::path(dir) / "before_dir").string();
    co_await fs::mkdir(sub1, 0755, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string sub2 = (std::filesystem::path(dir) / "after_dir").string();
    co_await fs::rename(sub1, sub2, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found_relevant = has_effect(changes, fs_event::effect::rename) ||
                          has_effect(changes, fs_event::effect::create) ||
                          has_effect(changes, fs_event::effect::destroy);

    watcher->stop();
    co_await fs::rmdir(sub2, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found_relevant ? 1 : 0;
}

task<int, error> watch_directory_deletion(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string sub = (std::filesystem::path(dir) / "todelete").string();
    co_await fs::mkdir(sub, 0755, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    co_await fs::rmdir(sub, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::destroy && c.path.find("todelete") != std::string::npos;
    });

    watcher->stop();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_subfile_create(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string sub = (std::filesystem::path(dir) / "subdir").string();
    co_await fs::mkdir(sub, 0755, loop).or_fail();

    // Consume the mkdir event batch
    co_await next_or_timeout(*watcher, loop).or_fail();
    co_await sleep(100, loop);

    std::string file = (std::filesystem::path(sub) / "child.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::create && c.path.find("child.txt") != std::string::npos;
    });

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(sub, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_subfile_modify(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string sub = (std::filesystem::path(dir) / "subdir").string();
    co_await fs::mkdir(sub, 0755, loop).or_fail();
    std::string file = (std::filesystem::path(sub) / "edit.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    fd = co_await fs::open(file, O_WRONLY, 0, loop).or_fail();
    constexpr std::string_view payload = "updated";
    co_await fs::write(fd, std::span<const char>(payload.data(), payload.size()), -1, loop)
        .or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::modify && c.path.find("edit.txt") != std::string::npos;
    });

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(sub, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_subfile_rename(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string sub = (std::filesystem::path(dir) / "subdir").string();
    co_await fs::mkdir(sub, 0755, loop).or_fail();
    std::string src = (std::filesystem::path(sub) / "a.txt").string();
    int fd = co_await fs::open(src, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string dst = (std::filesystem::path(sub) / "b.txt").string();
    co_await fs::rename(src, dst, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = std::ranges::any_of(changes, [](const auto& c) {
        return (c.type == fs_event::effect::rename || c.type == fs_event::effect::create ||
                c.type == fs_event::effect::destroy) &&
               c.path.find("subdir") != std::string::npos;
    });

    watcher->stop();
    co_await fs::unlink(dst, loop).or_fail();
    co_await fs::rmdir(sub, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_subfile_delete(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string sub = (std::filesystem::path(dir) / "subdir").string();
    co_await fs::mkdir(sub, 0755, loop).or_fail();
    std::string file = (std::filesystem::path(sub) / "gone.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    co_await fs::unlink(file, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::destroy && c.path.find("gone.txt") != std::string::npos;
    });

    watcher->stop();
    co_await fs::rmdir(sub, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_nested_subdir_create(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string sub1 = (std::filesystem::path(dir) / "level1").string();
    co_await fs::mkdir(sub1, 0755, loop).or_fail();
    co_await next_or_timeout(*watcher, loop).or_fail();
    co_await sleep(100, loop);

    std::string sub2 = (std::filesystem::path(sub1) / "level2").string();
    co_await fs::mkdir(sub2, 0755, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::create && c.path.find("level2") != std::string::npos;
    });

    watcher->stop();
    co_await fs::rmdir(sub2, loop).or_fail();
    co_await fs::rmdir(sub1, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_deep_nested_file(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string d1 = (std::filesystem::path(dir) / "a").string();
    co_await fs::mkdir(d1, 0755, loop).or_fail();
    co_await next_or_timeout(*watcher, loop).or_fail();
    co_await sleep(100, loop);

    std::string d2 = (std::filesystem::path(d1) / "b").string();
    co_await fs::mkdir(d2, 0755, loop).or_fail();
    co_await next_or_timeout(*watcher, loop).or_fail();
    co_await sleep(100, loop);

    std::string file = (std::filesystem::path(d2) / "deep.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::create && c.path.find("deep.txt") != std::string::npos;
    });

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(d2, loop).or_fail();
    co_await fs::rmdir(d1, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_subdir_rename(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string sub1 = (std::filesystem::path(dir) / "orig").string();
    co_await fs::mkdir(sub1, 0755, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string sub2 = (std::filesystem::path(dir) / "moved").string();
    co_await fs::rename(sub1, sub2, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found_relevant = has_effect(changes, fs_event::effect::rename) ||
                          has_effect(changes, fs_event::effect::create) ||
                          has_effect(changes, fs_event::effect::destroy);

    watcher->stop();
    co_await fs::rmdir(sub2, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found_relevant ? 1 : 0;
}

task<int, error> watch_renamed_dir_still_tracked(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string sub = (std::filesystem::path(dir) / "trackme").string();
    co_await fs::mkdir(sub, 0755, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string sub2 = (std::filesystem::path(dir) / "tracked").string();
    co_await fs::rename(sub, sub2, loop).or_fail();

    // Consume rename events
    co_await next_or_timeout(*watcher, loop).or_fail();
    co_await wait_for_watcher_ready(loop);

    std::string file = (std::filesystem::path(sub2) / "after_rename.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = std::ranges::any_of(changes, [](const auto& c) {
        return c.path.find("after_rename.txt") != std::string::npos;
    });

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(sub2, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_error_on_bad_parent(event_loop& loop) {
    auto result = fs_event::create("/nonexistent/parent/file.txt", {}, loop);
    co_return result.has_error() ? 1 : 0;
}

task<int, error> watch_multiple_watchers_same_dir(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto w1 = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    auto w2 = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);

    if(!w1.has_value() || !w2.has_value()) {
        co_await fail(error::unknown_error);
    }

    co_await wait_for_watcher_ready(loop);

    std::string file = (std::filesystem::path(dir) / "shared.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes1 = co_await next_or_timeout(*w1, loop).or_fail();
    auto changes2 = co_await next_or_timeout(*w2, loop).or_fail();

    bool w1_saw = has_effect(changes1, fs_event::effect::create);
    bool w2_saw = has_effect(changes2, fs_event::effect::create);

    w1->stop();
    w2->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return (w1_saw && w2_saw) ? 1 : 0;
}

task<int, error> watch_multiple_watchers_different_dirs(event_loop& loop) {
    auto t1 = (std::filesystem::temp_directory_path() / "kotatsu-dw1-XXXXXX").string();
    auto t2 = (std::filesystem::temp_directory_path() / "kotatsu-dw2-XXXXXX").string();
    std::string dir1 = co_await fs::mkdtemp(t1, loop).or_fail();
    std::string dir2 = co_await fs::mkdtemp(t2, loop).or_fail();

    auto w1 = fs_event::create(dir1, fs_event::options{std::chrono::milliseconds{50}}, loop);
    auto w2 = fs_event::create(dir2, fs_event::options{std::chrono::milliseconds{50}}, loop);

    if(!w1.has_value() || !w2.has_value()) {
        co_await fail(error::unknown_error);
    }

    co_await wait_for_watcher_ready(loop);

    std::string f1 = (std::filesystem::path(dir1) / "one.txt").string();
    std::string f2 = (std::filesystem::path(dir2) / "two.txt").string();
    int fd = co_await fs::open(f1, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();
    fd = co_await fs::open(f2, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto c1 = co_await next_or_timeout(*w1, loop).or_fail();
    auto c2 = co_await next_or_timeout(*w2, loop).or_fail();

    bool w1_saw = std::ranges::any_of(c1, [](const auto& c) {
        return c.path.find("one.txt") != std::string::npos;
    });
    bool w2_saw = std::ranges::any_of(c2, [](const auto& c) {
        return c.path.find("two.txt") != std::string::npos;
    });

    w1->stop();
    w2->stop();
    co_await fs::unlink(f1, loop).or_fail();
    co_await fs::unlink(f2, loop).or_fail();
    co_await fs::rmdir(dir1, loop).or_fail();
    co_await fs::rmdir(dir2, loop).or_fail();

    co_return (w1_saw && w2_saw) ? 1 : 0;
}

task<int, error> watch_rapid_create_delete(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{100}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string f1 = (std::filesystem::path(dir) / "keep.txt").string();
    std::string f2 = (std::filesystem::path(dir) / "ephemeral.txt").string();

    int fd = co_await fs::open(f1, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();
    fd = co_await fs::open(f2, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();
    co_await fs::unlink(f2, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool saw_keep_create = std::ranges::any_of(changes, [](const auto& c) {
        return c.path.find("keep.txt") != std::string::npos && c.type == fs_event::effect::create;
    });

    watcher->stop();
    co_await fs::unlink(f1, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return saw_keep_create ? 1 : 0;
}

task<int, error> watch_rapid_multiple_writes(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string file = (std::filesystem::path(dir) / "multi.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{100}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    for(int i = 0; i < 5; ++i) {
        fd = co_await fs::open(file, O_WRONLY, 0, loop).or_fail();
        std::string payload = "write" + std::to_string(i);
        co_await fs::write(fd, std::span<const char>(payload.data(), payload.size()), -1, loop)
            .or_fail();
        co_await fs::close(fd, loop).or_fail();
    }

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool saw_modify = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::modify && c.path.find("multi.txt") != std::string::npos;
    });

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return saw_modify ? 1 : 0;
}

task<int, error> watch_attribute_change(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string file = (std::filesystem::path(dir) / "attrs.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    co_await fs::chmod(file, 0444, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = std::ranges::any_of(changes, [](const auto& c) {
        return c.path.find("attrs.txt") != std::string::npos;
    });

    watcher->stop();
    co_await fs::chmod(file, 0644, loop).or_fail();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_atomic_replace(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string target = (std::filesystem::path(dir) / "target.txt").string();
    int fd = co_await fs::open(target, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    constexpr std::string_view original = "original";
    co_await fs::write(fd, std::span<const char>(original.data(), original.size()), -1, loop)
        .or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string tmp = (std::filesystem::path(dir) / "target.txt.tmp").string();
    fd = co_await fs::open(tmp, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    constexpr std::string_view updated = "updated";
    co_await fs::write(fd, std::span<const char>(updated.data(), updated.size()), -1, loop)
        .or_fail();
    co_await fs::close(fd, loop).or_fail();
    co_await fs::rename(tmp, target, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = std::ranges::any_of(changes, [](const auto& c) {
        return c.path.find("target.txt") != std::string::npos &&
               (c.type == fs_event::effect::create || c.type == fs_event::effect::modify);
    });

    watcher->stop();
    co_await fs::unlink(target, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_toctou_dir_and_file(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{100}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string sub = (std::filesystem::path(dir) / "racedir").string();
    co_await fs::mkdir(sub, 0755, loop).or_fail();
    std::string file = (std::filesystem::path(sub) / "quick.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    bool found = false;
    for(int attempt = 0; attempt < 3 && !found; ++attempt) {
        auto result = co_await next_or_timeout(*watcher, loop, 3000);
        if(result.has_error())
            break;
        found = std::ranges::any_of(*result, [](const auto& c) {
            return c.path.find("quick.txt") != std::string::npos;
        });
    }

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(sub, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_unicode_filename(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string file = (std::filesystem::path(dir) / "\xe6\x96\x87\xe4\xbb\xb6.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::create &&
               c.path.find("\xe6\x96\x87\xe4\xbb\xb6") != std::string::npos;
    });

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

// Windows symlink creation requires SeCreateSymbolicLinkPrivilege, which
// is unavailable in unprivileged CI runners.
#if !defined(_WIN32)
task<int, error> watch_symlink_create_delete(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string real = (std::filesystem::path(dir) / "real.txt").string();
    int fd = co_await fs::open(real, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string link = (std::filesystem::path(dir) / "link.txt").string();
    co_await fs::symlink(real, link, 0, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found_create = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::create && c.path.find("link.txt") != std::string::npos;
    });

    co_await fs::unlink(link, loop).or_fail();

    auto changes2 = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found_delete = std::ranges::any_of(changes2, [](const auto& c) {
        return c.type == fs_event::effect::destroy && c.path.find("link.txt") != std::string::npos;
    });

    watcher->stop();
    co_await fs::unlink(real, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return (found_create && found_delete) ? 1 : 0;
}
#endif

task<int, error> watch_large_burst(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{200}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    constexpr int count = 50;
    std::vector<std::string> files;
    files.reserve(count);
    for(int i = 0; i < count; ++i) {
        std::string file =
            (std::filesystem::path(dir) / ("burst_" + std::to_string(i) + ".txt")).string();
        int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
        co_await fs::close(fd, loop).or_fail();
        files.push_back(std::move(file));
    }

    int total_creates = 0;
    for(int attempt = 0; attempt < 5; ++attempt) {
        auto result = co_await next_or_timeout(*watcher, loop, 3000);
        if(result.has_error())
            break;
        total_creates += static_cast<int>(std::ranges::count_if(*result, [](const auto& c) {
            return c.type == fs_event::effect::create;
        }));
    }

    watcher->stop();
    for(auto& f: files) {
        co_await fs::unlink(f, loop).or_fail();
    }
    co_await fs::rmdir(dir, loop).or_fail();

    co_return total_creates >= (count * 48 / 50) ? 1 : 0;
}

task<int, error> watch_debounce_coalesces(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string file = (std::filesystem::path(dir) / "coalesce.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{200}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    for(int i = 0; i < 10; ++i) {
        std::string f =
            (std::filesystem::path(dir) / ("coalesce_" + std::to_string(i) + ".txt")).string();
        fd = co_await fs::open(f, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
        co_await fs::close(fd, loop).or_fail();
    }

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    auto create_count = static_cast<int>(std::ranges::count_if(changes, [](const auto& c) {
        return c.type == fs_event::effect::create;
    }));

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    for(int i = 0; i < 10; ++i) {
        std::string f =
            (std::filesystem::path(dir) / ("coalesce_" + std::to_string(i) + ".txt")).string();
        co_await fs::unlink(f, loop).or_fail();
    }
    co_await fs::rmdir(dir, loop).or_fail();

    co_return create_count >= 9 ? 1 : 0;
}

task<int, error> watch_root_dir_deleted(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();
    auto canonical_dir = std::filesystem::canonical(dir).string();
#if defined(_WIN32)
    std::replace(canonical_dir.begin(), canonical_dir.end(), '\\', '/');
#endif

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    co_await fs::rmdir(dir, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = std::ranges::any_of(changes, [&](const auto& c) {
        return c.type == fs_event::effect::destroy && c.path == canonical_dir;
    });

    watcher->stop();
    co_return found ? 1 : 0;
}

task<int, error> watch_subdir_delete_with_files(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string sub = (std::filesystem::path(dir) / "mydir").string();
    co_await fs::mkdir(sub, 0755, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string file = (std::filesystem::path(sub) / "child.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    co_await next_or_timeout(*watcher, loop).or_fail();
    co_await sleep(200, loop);

    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(sub, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found_file_delete = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::destroy && c.path.find("child.txt") != std::string::npos;
    });
    bool found_dir_delete = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::destroy && c.path.find("mydir") != std::string::npos &&
               c.path.find("child.txt") == std::string::npos;
    });

    watcher->stop();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return (found_file_delete && found_dir_delete) ? 1 : 0;
}

// See watch_symlink_create_delete for why Windows is skipped.
#if !defined(_WIN32)
task<int, error> watch_symlink_rename(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string real = (std::filesystem::path(dir) / "real.txt").string();
    int fd = co_await fs::open(real, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    std::string link1 = (std::filesystem::path(dir) / "link1.txt").string();
    co_await fs::symlink(real, link1, 0, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string link2 = (std::filesystem::path(dir) / "link2.txt").string();
    co_await fs::rename(link1, link2, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found_rename = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::rename && c.path.find("link2.txt") != std::string::npos;
    });
    bool found_destroy = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::destroy && c.path.find("link1.txt") != std::string::npos;
    });
    bool found_create = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::create && c.path.find("link2.txt") != std::string::npos;
    });

    watcher->stop();
    co_await fs::unlink(link2, loop).or_fail();
    co_await fs::unlink(real, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return (found_rename || (found_destroy && found_create)) ? 1 : 0;
}

task<int, error> watch_symlink_update(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string real = (std::filesystem::path(dir) / "real.txt").string();
    int fd = co_await fs::open(real, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    constexpr std::string_view initial = "hello";
    co_await fs::write(fd, std::span<const char>(initial.data(), initial.size()), -1, loop)
        .or_fail();
    co_await fs::close(fd, loop).or_fail();

    std::string link = (std::filesystem::path(dir) / "link.txt").string();
    co_await fs::symlink(real, link, 0, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    fd = co_await fs::open(link, O_WRONLY, 0, loop).or_fail();
    constexpr std::string_view updated = "world";
    co_await fs::write(fd, std::span<const char>(updated.data(), updated.size()), 0, loop)
        .or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found_modify = has_effect(changes, fs_event::effect::modify);

    watcher->stop();
    co_await fs::unlink(link, loop).or_fail();
    co_await fs::unlink(real, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found_modify ? 1 : 0;
}

task<int, error> watch_folder_symlink(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string sub = (std::filesystem::path(dir) / "realdir").string();
    co_await fs::mkdir(sub, 0755, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string link = (std::filesystem::path(dir) / "linkdir").string();
    co_await fs::symlink(sub, link, 0, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found_create = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::create && c.path.find("linkdir") != std::string::npos;
    });

    co_await fs::unlink(link, loop).or_fail();

    auto changes2 = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found_delete = std::ranges::any_of(changes2, [](const auto& c) {
        return c.type == fs_event::effect::destroy && c.path.find("linkdir") != std::string::npos;
    });

    watcher->stop();
    co_await fs::rmdir(sub, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return (found_create && found_delete) ? 1 : 0;
}
#endif

task<int, error> watch_rapid_create_update(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{200}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    std::string file = (std::filesystem::path(dir) / "rapid.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    constexpr std::string_view v1 = "hello";
    co_await fs::write(fd, std::span<const char>(v1.data(), v1.size()), -1, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    fd = co_await fs::open(file, O_WRONLY, 0, loop).or_fail();
    constexpr std::string_view v2 = "updated";
    co_await fs::write(fd, std::span<const char>(v2.data(), v2.size()), 0, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool saw_create = std::ranges::any_of(changes, [](const auto& c) {
        return c.path.find("rapid.txt") != std::string::npos && c.type == fs_event::effect::create;
    });

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return saw_create ? 1 : 0;
}

task<int, error> watch_rapid_update_delete(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string file = (std::filesystem::path(dir) / "doomed.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    constexpr std::string_view initial = "hello";
    co_await fs::write(fd, std::span<const char>(initial.data(), initial.size()), -1, loop)
        .or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{200}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    fd = co_await fs::open(file, O_WRONLY, 0, loop).or_fail();
    constexpr std::string_view updated = "updated";
    co_await fs::write(fd, std::span<const char>(updated.data(), updated.size()), 0, loop)
        .or_fail();
    co_await fs::close(fd, loop).or_fail();
    co_await fs::unlink(file, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool saw_destroy = std::ranges::any_of(changes, [](const auto& c) {
        return c.path.find("doomed.txt") != std::string::npos &&
               c.type == fs_event::effect::destroy;
    });

    watcher->stop();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return saw_destroy ? 1 : 0;
}

task<int, error> watch_rapid_delete_create(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string file = (std::filesystem::path(dir) / "phoenix.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    constexpr std::string_view v1 = "v1";
    co_await fs::write(fd, std::span<const char>(v1.data(), v1.size()), -1, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{200}}, loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await wait_for_watcher_ready(loop);

    co_await fs::unlink(file, loop).or_fail();
    fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    constexpr std::string_view v2 = "v2";
    co_await fs::write(fd, std::span<const char>(v2.data(), v2.size()), -1, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool saw_relevant = std::ranges::any_of(changes, [](const auto& c) {
        return c.path.find("phoenix.txt") != std::string::npos;
    });

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return saw_relevant ? 1 : 0;
}

task<int, error> watch_case_only_rename(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string lower = (std::filesystem::path(dir) / "hello.txt").string();
    std::string upper = (std::filesystem::path(dir) / "HELLO.TXT").string();

    int fd = co_await fs::open(lower, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    constexpr std::string_view data = "hello";
    co_await fs::write(fd, std::span<const char>(data.data(), data.size()), -1, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value())
        co_await fail(watcher.error());

    co_await wait_for_watcher_ready(loop);

    co_await fs::rename(lower, upper, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool saw_event = has_effect(changes, fs_event::effect::create) ||
                     has_effect(changes, fs_event::effect::destroy) ||
                     has_effect(changes, fs_event::effect::rename);

    watcher->stop();
    co_await fs::unlink(upper, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return saw_event ? 1 : 0;
}

task<int, error> watch_nested_dir_rename(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string sub = (std::filesystem::path(dir) / "parent").string();
    std::string subsub = (std::filesystem::path(dir) / "parent" / "child").string();
    co_await fs::mkdir(sub, 0755, loop).or_fail();
    co_await fs::mkdir(subsub, 0755, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value())
        co_await fail(watcher.error());

    co_await wait_for_watcher_ready(loop);

    std::string sub2 = (std::filesystem::path(dir) / "parent2").string();
    std::string subsub2 = (std::filesystem::path(dir) / "parent2" / "child2").string();
    co_await fs::rename(sub, sub2, loop).or_fail();
    co_await fs::rename(sub2 + "/child", subsub2, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool saw_parent = std::ranges::any_of(changes, [](const auto& c) {
        return c.path.find("parent2") != std::string::npos &&
               c.path.find("child") == std::string::npos;
    });
    bool saw_child = std::ranges::any_of(changes, [](const auto& c) {
        return c.path.find("child2") != std::string::npos;
    });

    watcher->stop();
    co_await fs::rmdir(subsub2, loop).or_fail();
    co_await fs::rmdir(sub2, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return (saw_parent && saw_child) ? 1 : 0;
}

task<int, error> watch_create_rename_coalesce(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{200}}, loop);
    if(!watcher.has_value())
        co_await fail(watcher.error());

    co_await wait_for_watcher_ready(loop);

    std::string f1 = (std::filesystem::path(dir) / "temp.txt").string();
    std::string f2 = (std::filesystem::path(dir) / "final.txt").string();

    int fd = co_await fs::open(f1, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    constexpr std::string_view data = "hello";
    co_await fs::write(fd, std::span<const char>(data.data(), data.size()), -1, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();
    co_await fs::rename(f1, f2, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool saw_final = std::ranges::any_of(changes, [](const auto& c) {
        return c.path.find("final.txt") != std::string::npos &&
               (c.type == fs_event::effect::create || c.type == fs_event::effect::rename);
    });

    watcher->stop();
    co_await fs::unlink(f2, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return saw_final ? 1 : 0;
}

task<int, error> watch_chain_rename_coalesce(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{200}}, loop);
    if(!watcher.has_value())
        co_await fail(watcher.error());

    co_await wait_for_watcher_ready(loop);

    std::string f1 = (std::filesystem::path(dir) / "step1.txt").string();
    std::string f2 = (std::filesystem::path(dir) / "step2.txt").string();
    std::string f3 = (std::filesystem::path(dir) / "step3.txt").string();
    std::string f4 = (std::filesystem::path(dir) / "step4.txt").string();

    int fd = co_await fs::open(f1, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    constexpr std::string_view data = "chain";
    co_await fs::write(fd, std::span<const char>(data.data(), data.size()), -1, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();
    co_await fs::rename(f1, f2, loop).or_fail();
    co_await fs::rename(f2, f3, loop).or_fail();
    co_await fs::rename(f3, f4, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool saw_final = std::ranges::any_of(changes, [](const auto& c) {
        return c.path.find("step4.txt") != std::string::npos &&
               (c.type == fs_event::effect::create || c.type == fs_event::effect::rename);
    });

    watcher->stop();
    co_await fs::unlink(f4, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return saw_final ? 1 : 0;
}

task<int, error> watch_special_char_filename(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value())
        co_await fail(watcher.error());

    co_await wait_for_watcher_ready(loop);

    std::string file = (std::filesystem::path(dir) / "file with spaces & (parens).txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = std::ranges::any_of(changes, [](const auto& c) {
        return c.type == fs_event::effect::create && c.path.find("spaces") != std::string::npos;
    });

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> watch_rapid_stop_create_cycle(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    for(int i = 0; i < 5; ++i) {
        auto watcher =
            fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
        if(!watcher.has_value())
            co_await fail(watcher.error());
        watcher->stop();
    }

    auto watcher = fs_event::create(dir, fs_event::options{std::chrono::milliseconds{50}}, loop);
    if(!watcher.has_value())
        co_await fail(watcher.error());

    co_await wait_for_watcher_ready(loop);

    std::string file = (std::filesystem::path(dir) / "final.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await next_or_timeout(*watcher, loop).or_fail();

    bool found = has_effect(changes, fs_event::effect::create);

    watcher->stop();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

}  // namespace

TEST_SUITE(fs_event_dir_io, loop_fixture) {

TEST_CASE(detect_file_creation) {
    auto worker = watch_file_create(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(detect_file_modification) {
    auto worker = watch_file_modify(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(detect_file_deletion) {
    auto worker = watch_file_delete(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(detect_file_rename) {
    auto worker = watch_file_rename(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(error_on_nonexistent_path) {
    auto worker = watch_nonexistent_path(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(close_then_next_returns_error) {
    auto worker = watch_close_then_next(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(stop_during_next_returns_aborted) {
    auto worker = watch_stop_during_next(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(stop_during_debounce_returns) {
    auto worker = watch_stop_during_debounce(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(multiple_next_calls) {
    auto worker = watch_multiple_next_calls(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(debounce_batches_events) {
    auto worker = watch_debounce_batching(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(recursive_subdirectory_events) {
    auto worker = watch_subdirectory_changes(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(non_recursive_watches_top_level) {
    auto worker = watch_non_recursive(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(move_assignment_closes_old_watcher) {
    auto worker = watch_move_assignment(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(destructor_cleans_up_without_close) {
    auto worker = watch_destructor_cleanup(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(double_close_is_safe) {
    auto worker = watch_double_close(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(create_with_default_options) {
    auto worker = watch_default_options(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(rename_populates_old_path) {
    auto worker = watch_rename_populates_old_path(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(rename_existing_file) {
    auto worker = watch_rename_existing_file(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(directory_creation) {
    auto worker = watch_directory_creation(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(directory_rename) {
    auto worker = watch_directory_rename(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(directory_deletion) {
    auto worker = watch_directory_deletion(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(subfile_create) {
    auto worker = watch_subfile_create(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(subfile_modify) {
    auto worker = watch_subfile_modify(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(subfile_rename) {
    auto worker = watch_subfile_rename(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(subfile_delete) {
    auto worker = watch_subfile_delete(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(nested_subdir_create) {
    auto worker = watch_nested_subdir_create(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(deep_nested_file) {
    auto worker = watch_deep_nested_file(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(subdir_rename) {
    auto worker = watch_subdir_rename(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(renamed_dir_still_tracked) {
    auto worker = watch_renamed_dir_still_tracked(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(error_on_bad_parent) {
    auto worker = watch_error_on_bad_parent(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(multiple_watchers_same_dir) {
    auto worker = watch_multiple_watchers_same_dir(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(multiple_watchers_different_dirs) {
    auto worker = watch_multiple_watchers_different_dirs(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(rapid_create_delete) {
    auto worker = watch_rapid_create_delete(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(rapid_multiple_writes) {
    auto worker = watch_rapid_multiple_writes(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(attribute_change) {
    auto worker = watch_attribute_change(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(atomic_replace) {
    auto worker = watch_atomic_replace(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(toctou_dir_and_file) {
    auto worker = watch_toctou_dir_and_file(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(unicode_filename) {
    auto worker = watch_unicode_filename(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(symlink_create_delete) {
#if defined(_WIN32)
    kota::zest::skip();
#else
    auto worker = watch_symlink_create_delete(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
#endif
}

TEST_CASE(large_burst) {
    auto worker = watch_large_burst(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(debounce_coalesces) {
    auto worker = watch_debounce_coalesces(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(root_dir_deleted) {
    auto worker = watch_root_dir_deleted(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(subdir_delete_with_files) {
    auto worker = watch_subdir_delete_with_files(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(symlink_rename) {
#if defined(_WIN32)
    kota::zest::skip();
#else
    auto worker = watch_symlink_rename(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
#endif
}

TEST_CASE(symlink_update) {
#if defined(_WIN32)
    kota::zest::skip();
#else
    auto worker = watch_symlink_update(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
#endif
}

TEST_CASE(folder_symlink) {
#if defined(_WIN32)
    kota::zest::skip();
#else
    auto worker = watch_folder_symlink(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
#endif
}

TEST_CASE(rapid_create_update) {
    auto worker = watch_rapid_create_update(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(rapid_update_delete) {
    auto worker = watch_rapid_update_delete(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(rapid_delete_create) {
    auto worker = watch_rapid_delete_create(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(case_only_rename) {
    auto worker = watch_case_only_rename(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(nested_dir_rename) {
    auto worker = watch_nested_dir_rename(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(create_rename_coalesce) {
    auto worker = watch_create_rename_coalesce(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(chain_rename_coalesce) {
    auto worker = watch_chain_rename_coalesce(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(special_char_filename) {
    auto worker = watch_special_char_filename(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(rapid_stop_create_cycle) {
    auto worker = watch_rapid_stop_create_cycle(loop);
    schedule_all(worker);

    auto result = worker.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

};  // TEST_SUITE(fs_event_dir_io)

}  // namespace kota
