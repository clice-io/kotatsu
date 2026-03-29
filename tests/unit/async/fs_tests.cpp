#include <fcntl.h>
#include <filesystem>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#endif

#include "loop_fixture.h"
#include "../common/fd_helpers.h"
#include "eventide/zest/zest.h"

namespace eventide {

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

task<int, error> fs_roundtrip(event_loop& loop) {
    auto dir_template = (std::filesystem::temp_directory_path() / "eventide-XXXXXX").string();
    std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();
    if(dir.empty()) {
        co_await fail(error::invalid_argument);
    }

    std::string file = (std::filesystem::path(dir) / "sample.txt").string();
    int fd = open_fd(file);
    if(fd < 0) {
        co_await fail(error::io_error);
    }

    constexpr std::string_view payload = "eventide-fs";
    if(write_fd(fd, payload.data(), payload.size()) != static_cast<ssize_t>(payload.size())) {
        close_fd(fd);
        co_await fail(error::io_error);
    }
    close_fd(fd);

    co_await fs::stat(file, loop).or_fail();

    auto dir_handle = co_await fs::opendir(dir, loop).or_fail();
    auto entries = co_await fs::readdir(dir_handle, loop);
    if(!entries.has_value()) {
        co_await fs::closedir(dir_handle, loop);
        co_await fail(entries.error());
    }

    bool found = false;
    for(const auto& ent: *entries) {
        if(ent.name == "sample.txt") {
            found = true;
            break;
        }
    }

    co_await fs::closedir(dir_handle, loop).or_fail();
    co_await fs::unlink(file, loop).or_fail();
    co_await fs::rmdir(dir, loop).or_fail();

    co_return found ? 1 : 0;
}

task<int, error> mkstemp_roundtrip(event_loop& loop) {
    auto file_template = (std::filesystem::temp_directory_path() / "eventide-file-XXXXXX").string();
    auto file_info = co_await fs::mkstemp(file_template, loop).or_fail();
    const int fd = file_info.fd;
    std::string path = std::move(file_info.path);
    if(fd >= 0) {
        close_fd(fd);
    }

    if(path.empty()) {
        co_await fail(error::invalid_argument);
    }

    co_await fs::access(path, 0, loop).or_fail();
    co_await fs::unlink(path, loop).or_fail();

    co_return 1;
}

}  // namespace

TEST_SUITE(fs_request_io, loop_fixture) {

TEST_CASE(basic_roundtrip) {
    auto worker = fs_roundtrip(loop);
    schedule_all(worker);

    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(mkstemp_and_access) {
    auto worker = mkstemp_roundtrip(loop);
    schedule_all(worker);

    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(async_open_read_write_close) {
    auto worker = [](event_loop& loop) -> task<int, error> {
        auto dir_template =
            (std::filesystem::temp_directory_path() / "eventide-rw-XXXXXX").string();
        std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();
        std::string file = (std::filesystem::path(dir) / "rw_test.txt").string();

        int fd = co_await fs::open(file, O_CREAT | O_WRONLY | O_TRUNC, 0644, loop).or_fail();

        constexpr std::string_view payload = "hello-async-io";
        auto written =
            co_await fs::write(fd, std::span<const char>(payload.data(), payload.size()), -1, loop)
                .or_fail();
        if(written != payload.size()) {
            co_await fail(error::io_error);
        }

        co_await fs::close(fd, loop).or_fail();

        fd = co_await fs::open(file, O_RDONLY, 0, loop).or_fail();

        char buf[64]{};
        auto nread = co_await fs::read(fd, std::span<char>(buf, sizeof(buf)), -1, loop).or_fail();
        co_await fs::close(fd, loop).or_fail();

        co_await fs::unlink(file, loop).or_fail();
        co_await fs::rmdir(dir, loop).or_fail();

        std::string_view got(buf, nread);
        co_return got == payload ? 1 : 0;
    }(loop);

    schedule_all(worker);

    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

#ifndef _WIN32

TEST_CASE(symlink_readlink_realpath) {
    auto worker = [](event_loop& loop) -> task<int, error> {
        auto dir_template =
            (std::filesystem::temp_directory_path() / "eventide-sym-XXXXXX").string();
        std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();
        std::string target = (std::filesystem::path(dir) / "target.txt").string();
        std::string link_path = (std::filesystem::path(dir) / "link.txt").string();

        // Create the target file.
        int fd = open_fd(target);
        if(fd < 0) {
            co_await fail(error::io_error);
        }
        close_fd(fd);

        co_await fs::symlink(target, link_path, 0, loop).or_fail();

        auto read_target = co_await fs::readlink(link_path, loop).or_fail();
        if(read_target != target) {
            co_await fail(error::invalid_argument);
        }

        auto resolved = co_await fs::realpath(link_path, loop).or_fail();
        auto expected = std::filesystem::canonical(target).string();
        if(resolved != expected) {
            co_await fail(error::invalid_argument);
        }

        // lstat should report a symlink.
        auto st = co_await fs::lstat(link_path, loop).or_fail();
        bool is_link = (st.mode & S_IFMT) == S_IFLNK;

        co_await fs::unlink(link_path, loop).or_fail();
        co_await fs::unlink(target, loop).or_fail();
        co_await fs::rmdir(dir, loop).or_fail();

        co_return is_link ? 1 : 0;
    }(loop);

    schedule_all(worker);

    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(chown_fchown_lchown) {
    auto worker = [](event_loop& loop) -> task<int, error> {
        auto dir_template =
            (std::filesystem::temp_directory_path() / "eventide-chown-XXXXXX").string();
        std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();
        std::string file = (std::filesystem::path(dir) / "owned.txt").string();

        int fd = open_fd(file);
        if(fd < 0) {
            co_await fail(error::io_error);
        }
        close_fd(fd);

        // Get current owner.
        auto st = co_await fs::stat(file, loop).or_fail();
        auto uid = static_cast<std::uint32_t>(st.uid);
        auto gid = static_cast<std::uint32_t>(st.gid);

        // chown to same owner (should succeed without root).
        co_await fs::chown(file, uid, gid, loop).or_fail();

        // fchown via fd.
        int ofd = co_await fs::open(file, O_RDONLY, 0, loop).or_fail();
        co_await fs::fchown(ofd, uid, gid, loop).or_fail();
        co_await fs::close(ofd, loop).or_fail();

        // Create symlink and lchown.
        std::string link_path = (std::filesystem::path(dir) / "link.txt").string();
        co_await fs::symlink(file, link_path, 0, loop).or_fail();
        co_await fs::lchown(link_path, uid, gid, loop).or_fail();

        co_await fs::unlink(link_path, loop).or_fail();
        co_await fs::unlink(file, loop).or_fail();
        co_await fs::rmdir(dir, loop).or_fail();

        co_return 1;
    }(loop);

    schedule_all(worker);

    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST_CASE(fchmod) {
    auto worker = [](event_loop& loop) -> task<int, error> {
        auto dir_template =
            (std::filesystem::temp_directory_path() / "eventide-fchmod-XXXXXX").string();
        std::string dir = co_await fs::mkdtemp(dir_template, loop).or_fail();
        std::string file = (std::filesystem::path(dir) / "perm.txt").string();

        int fd = co_await fs::open(file, O_CREAT | O_WRONLY, 0644, loop).or_fail();
        co_await fs::fchmod(fd, 0600, loop).or_fail();
        co_await fs::close(fd, loop).or_fail();

        auto st = co_await fs::stat(file, loop).or_fail();
        bool mode_ok = (st.mode & 0777) == 0600;

        co_await fs::unlink(file, loop).or_fail();
        co_await fs::rmdir(dir, loop).or_fail();

        co_return mode_ok ? 1 : 0;
    }(loop);

    schedule_all(worker);

    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

#endif  // !_WIN32

TEST_CASE(statfs_basic) {
    auto worker = [](event_loop& loop) -> task<int, error> {
        auto statfs_path = std::filesystem::temp_directory_path().string();
        auto stats = co_await fs::statfs(statfs_path, loop).or_fail();
        // Block size should be nonzero on any real filesystem.
        co_return stats.bsize > 0 ? 1 : 0;
    }(loop);

    schedule_all(worker);

    auto result = worker.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

};  // TEST_SUITE(fs_request_io)

}  // namespace eventide
