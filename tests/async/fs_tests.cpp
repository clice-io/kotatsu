#include <fcntl.h>
#include <filesystem>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#endif

#include "../common/fd_helpers.h"
#include "eventide/zest/zest.h"
#include "eventide/async/fs.h"
#include "eventide/async/loop.h"
#include "eventide/async/task.h"

namespace eventide {

using test::close_fd;
using test::write_fd;

namespace {

#ifdef _WIN32
inline int open_fd(const std::string& path) {
    return _open(path.c_str(), _O_CREAT | _O_WRONLY | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
}
#else
inline int open_fd(const std::string& path) {
    return ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
}
#endif

task<int, error> fs_roundtrip(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "eventide-XXXXXX").string();
    auto dir_res = co_await fs::mkdtemp(dir_template, loop);
    if(!dir_res.has_value()) {
        co_return outcome_error(dir_res.error());
    }

    std::string dir = *dir_res;
    if(dir.empty()) {
        co_return outcome_error(error::invalid_argument);
    }

    std::string file = (std::filesystem::path(dir) / "sample.txt").string();
    int fd = open_fd(file);
    if(fd < 0) {
        co_return outcome_error(error::io_error);
    }

    constexpr std::string_view payload = "eventide-fs";
    if(write_fd(fd, payload.data(), payload.size()) != static_cast<ssize_t>(payload.size())) {
        close_fd(fd);
        co_return outcome_error(error::io_error);
    }
    close_fd(fd);

    auto stat_res = co_await fs::stat(file, loop);
    if(!stat_res.has_value()) {
        co_return outcome_error(stat_res.error());
    }

    auto dir_res2 = co_await fs::opendir(dir, loop);
    if(!dir_res2.has_value()) {
        co_return outcome_error(dir_res2.error());
    }

    auto entries = co_await fs::readdir(*dir_res2, loop);
    if(!entries.has_value()) {
        co_await fs::closedir(*dir_res2, loop);
        co_return outcome_error(entries.error());
    }

    bool found = false;
    for(const auto& ent: *entries) {
        if(ent.name == "sample.txt") {
            found = true;
            break;
        }
    }

    auto close_res = co_await fs::closedir(*dir_res2, loop);
    if(!close_res) {
        co_return outcome_error(close_res.error());
    }

    auto unlink_res = co_await fs::unlink(file, loop);
    if(!unlink_res) {
        co_return outcome_error(unlink_res.error());
    }

    auto rmdir_res = co_await fs::rmdir(dir, loop);
    if(!rmdir_res) {
        co_return outcome_error(rmdir_res.error());
    }

    co_return found ? 1 : 0;
}

task<int, error> mkstemp_roundtrip(event_loop& loop) {
    auto file_template = (std::filesystem::temp_directory_path() / "eventide-file-XXXXXX").string();
    auto file_res = co_await fs::mkstemp(file_template, loop);
    if(!file_res.has_value()) {
        co_return outcome_error(file_res.error());
    }

    const int fd = file_res->fd;
    std::string path = file_res->path;
    if(fd >= 0) {
        close_fd(fd);
    }

    if(path.empty()) {
        co_return outcome_error(error::invalid_argument);
    }

    auto access_res = co_await fs::access(path, 0, loop);
    if(!access_res) {
        co_return outcome_error(access_res.error());
    }

    auto unlink_res = co_await fs::unlink(path, loop);
    if(!unlink_res) {
        co_return outcome_error(unlink_res.error());
    }

    co_return 1;
}

}  // namespace

TEST_SUITE(fs_request_io) {

TEST_CASE(basic_roundtrip) {
    event_loop loop;

    auto worker = fs_roundtrip(loop);
    loop.schedule(worker);
    loop.run();

    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(mkstemp_and_access) {
    event_loop loop;

    auto worker = mkstemp_roundtrip(loop);
    loop.schedule(worker);
    loop.run();

    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

};  // TEST_SUITE(fs_request_io)

}  // namespace eventide
