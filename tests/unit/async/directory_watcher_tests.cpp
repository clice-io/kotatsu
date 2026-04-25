#include <fcntl.h>
#include <filesystem>
#include <string>

#include "loop_fixture.h"
#include "../support/fd_helpers.h"
#include "kota/zest/zest.h"
#include "kota/async/io/directory_watcher.h"

namespace kota {

using test::close_fd;
using test::write_fd;

namespace {

#ifdef _WIN32
inline int open_fd(const std::string& path) {
    int fd = -1;
    if(_sopen_s(&fd,
                path.c_str(),
                _O_CREAT | _O_WRONLY | _O_TRUNC | _O_BINARY,
                _SH_DENYNO,
                _S_IREAD | _S_IWRITE) != 0) {
        return -1;
    }
    return fd;
}
#else
inline int open_fd(const std::string& path) {
    return ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
}
#endif

task<int, error> watch_file_create(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher =
        directory_watcher::create(dir.c_str(),
                                  directory_watcher::options{std::chrono::milliseconds{50}},
                                  loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await sleep(100, loop);

    std::string file = (std::filesystem::path(dir) / "test.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await watcher->next().or_fail();

    bool found_create = false;
    for(const auto& c: changes) {
        if(c.type == directory_watcher::effect::create) {
            found_create = true;
        }
    }

    watcher->close();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found_create ? 1 : 0;
}

task<int, error> watch_file_modify(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string file = (std::filesystem::path(dir) / "modify.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher =
        directory_watcher::create(dir.c_str(),
                                  directory_watcher::options{std::chrono::milliseconds{50}},
                                  loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await sleep(100, loop);

    fd = co_await fs::open(file, O_WRONLY, 0, loop).or_fail();
    constexpr std::string_view payload = "hello";
    co_await fs::write(fd, std::span<const char>(payload.data(), payload.size()), -1, loop)
        .or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto changes = co_await watcher->next().or_fail();

    bool found_modify = false;
    for(const auto& c: changes) {
        if(c.type == directory_watcher::effect::modify) {
            found_modify = true;
        }
    }

    watcher->close();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found_modify ? 1 : 0;
}

task<int, error> watch_file_delete(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    std::string file = (std::filesystem::path(dir) / "delete.txt").string();
    int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();
    co_await fs::close(fd, loop).or_fail();

    auto watcher =
        directory_watcher::create(dir.c_str(),
                                  directory_watcher::options{std::chrono::milliseconds{50}},
                                  loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    co_await sleep(100, loop);

    co_await fs::unlink(file, loop).or_fail();

    auto changes = co_await watcher->next().or_fail();

    bool found_destroy = false;
    for(const auto& c: changes) {
        if(c.type == directory_watcher::effect::destroy) {
            found_destroy = true;
        }
    }

    watcher->close();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found_destroy ? 1 : 0;
}

task<int, error> watch_nonexistent_path(event_loop& loop) {
    auto result = directory_watcher::create("/tmp/kotatsu-nonexistent-path-xyz", {}, loop);
    co_return result.has_value() ? 0 : 1;
}

task<int, error> watch_close_then_next(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "kotatsu-dw-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();

    auto watcher =
        directory_watcher::create(dir.c_str(),
                                  directory_watcher::options{std::chrono::milliseconds{50}},
                                  loop);
    if(!watcher.has_value()) {
        co_await fail(watcher.error());
    }

    watcher->close();

    auto result = co_await watcher->next();
    bool got_error = result.has_error();

    co_await fs::rmdir(dir, loop).or_fail();

    co_return got_error ? 1 : 0;
}

}  // namespace

TEST_SUITE(directory_watcher_io, loop_fixture) {

TEST_CASE(detect_file_creation) {
    auto worker = watch_file_create(loop);
    schedule_all(worker);

    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(detect_file_modification) {
    auto worker = watch_file_modify(loop);
    schedule_all(worker);

    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(detect_file_deletion) {
    auto worker = watch_file_delete(loop);
    schedule_all(worker);

    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(error_on_nonexistent_path) {
    auto worker = watch_nonexistent_path(loop);
    schedule_all(worker);

    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(close_then_next_returns_error) {
    auto worker = watch_close_then_next(loop);
    schedule_all(worker);

    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

};  // TEST_SUITE(directory_watcher_io)

}  // namespace kota
