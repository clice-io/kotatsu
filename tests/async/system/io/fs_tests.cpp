#include <array>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <utility>

#include "async/harness/loop_fixture.h"
#include "async/harness/os.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

// Files are set up and read back with test::write_file and test::read_file,
// not kota::async.

ZEST_SUITE(async_io_fs, test::LoopFixture) {

ZEST_CASE(write_then_read_back) {
    test::TempDir dir;
    auto path = dir.file("data.txt");

    struct Seen {
        std::size_t written = 0;
        std::string read;
        std::size_t read_at_end = 1;
    };

    auto roundtrip = [&]() -> task<Seen, error> {
        Seen seen;
        int out = co_await fs::open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644).or_fail();
        seen.written = co_await fs::write(out, std::string_view("kotatsu-fs")).or_fail();
        co_await fs::close(out).or_fail();

        int in = co_await fs::open(path, O_RDONLY).or_fail();
        std::array<char, 64> buffer{};
        auto count = co_await fs::read(in, buffer).or_fail();
        seen.read.assign(buffer.data(), count);
        seen.read_at_end = co_await fs::read(in, buffer).or_fail();
        co_await fs::close(in).or_fail();
        co_return seen;
    };

    auto [result] = run(roundtrip());
    ASSERT(result.has_value());
    EXPECT(result->written == 10U);
    EXPECT(result->read == "kotatsu-fs");
    EXPECT(result->read_at_end == 0U);
    EXPECT(test::read_file(path) == "kotatsu-fs");
}

ZEST_CASE(read_and_write_at_an_offset) {
    test::TempDir dir;
    auto path = dir.file("data.txt");
    test::write_file(path, "hello world");
    auto patch = [&]() -> task<std::string, error> {
        int fd = co_await fs::open(path, O_RDWR).or_fail();
        co_await fs::write(fd, std::string_view("WORLD"), 6).or_fail();
        std::array<char, 5> buffer{};
        auto count = co_await fs::read(fd, buffer, 6).or_fail();
        co_await fs::close(fd).or_fail();
        co_return std::string(buffer.data(), count);
    };

    auto [result] = run(patch());
    ASSERT(result.has_value());
    EXPECT(*result == "WORLD");
    EXPECT(test::read_file(path) == "hello WORLD");
}

ZEST_CASE(open_of_a_missing_file_fails) {
    test::TempDir dir;
    auto [result] = run(fs::open(dir.file("missing"), O_RDONLY, 0, loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::no_such_file_or_directory);
}

ZEST_CASE(stat_and_fstat_describe_a_file) {
    test::TempDir dir;
    auto path = dir.file("data.txt");
    test::write_file(path, "12345");
    auto describe = [&]() -> task<std::pair<fs::file_stats, fs::file_stats>, error> {
        auto by_path = co_await fs::stat(path).or_fail();
        int fd = co_await fs::open(path, O_RDONLY).or_fail();
        auto by_fd = co_await fs::fstat(fd).or_fail();
        co_await fs::close(fd).or_fail();
        co_return std::pair{by_path, by_fd};
    };

    auto [result] = run(describe());
    ASSERT(result.has_value());
    auto& [by_path, by_fd] = *result;
    EXPECT(by_path.size == 5U);
    EXPECT(by_path.nlink == 1U);
    EXPECT(by_fd.size == 5U);
    EXPECT(by_fd.ino == by_path.ino);
#ifndef _WIN32
    EXPECT((by_path.mode & S_IFMT) == S_IFREG);
#endif
}

ZEST_CASE(stat_of_a_missing_file_fails) {
    test::TempDir dir;
    auto [result] = run(fs::stat(dir.file("missing"), loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::no_such_file_or_directory);
}

ZEST_CASE(unlink_removes_a_file) {
    test::TempDir dir;
    test::write_file(dir.path / "data.txt", "x");

    auto [result] = run(fs::unlink(dir.file("data.txt"), loop));
    EXPECT(result.has_value());
    EXPECT(!std::filesystem::exists(dir.path / "data.txt"));
}

ZEST_CASE(unlink_of_a_missing_file_fails) {
    test::TempDir dir;
    auto [result] = run(fs::unlink(dir.file("missing"), loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::no_such_file_or_directory);
}

ZEST_CASE(rename_moves_a_file) {
    test::TempDir dir;
    test::write_file(dir.path / "from.txt", "moved");

    auto [result] = run(fs::rename(dir.file("from.txt"), dir.file("to.txt"), loop));
    EXPECT(result.has_value());
    EXPECT(!std::filesystem::exists(dir.path / "from.txt"));
    EXPECT(test::read_file(dir.path / "to.txt") == "moved");
}

ZEST_CASE(rename_of_a_missing_file_fails) {
    test::TempDir dir;
    auto [result] = run(fs::rename(dir.file("missing"), dir.file("other"), loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::no_such_file_or_directory);
}

// A clone falls back to a copy where the filesystem cannot share blocks.
ZEST_CASE(copyfile_copies_the_contents) {
    test::TempDir dir;
    test::write_file(dir.path / "source.txt", "copied");

    auto [copied, cloned] =
        run(fs::copyfile(dir.file("source.txt"), dir.file("copy.txt"), {}, loop),
            fs::copyfile(dir.file("source.txt"), dir.file("clone.txt"), {.clone = true}, loop));
    EXPECT(copied.has_value());
    EXPECT(test::read_file(dir.path / "copy.txt") == "copied");
    EXPECT(cloned.has_value());
    EXPECT(test::read_file(dir.path / "clone.txt") == "copied");
}

ZEST_CASE(copyfile_onto_a_file_with_excl_fails) {
    test::TempDir dir;
    test::write_file(dir.path / "source.txt", "copied");
    test::write_file(dir.path / "taken.txt", "keep");

    auto [result] =
        run(fs::copyfile(dir.file("source.txt"), dir.file("taken.txt"), {.excl = true}, loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::file_already_exists);
    EXPECT(test::read_file(dir.path / "taken.txt") == "keep");
}

ZEST_CASE(mkstemp_creates_and_opens_a_new_file) {
    test::TempDir dir;
    auto make = [&]() -> task<fs::mkstemp_result, error> {
        auto created = co_await fs::mkstemp(dir.file("file-XXXXXX")).or_fail();
        co_await fs::close(created.fd).or_fail();
        co_return created;
    };

    auto [created] = run(make());
    ASSERT(created.has_value());
    EXPECT(created->fd >= 0);
    EXPECT(std::filesystem::is_regular_file(created->path));
    EXPECT(zest::starts_with(created->path, dir.file("file-")));
}

ZEST_CASE(mkstemp_without_a_template_fails) {
    test::TempDir dir;
    auto [result] = run(fs::mkstemp(dir.file("no-template"), loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::invalid_argument);
}

ZEST_CASE(ftruncate_fsync_and_fdatasync_act_on_a_descriptor) {
    test::TempDir dir;
    auto path = dir.file("data.txt");
    test::write_file(path, "0123456789");
    auto shrink = [&]() -> task<void, error> {
        int fd = co_await fs::open(path, O_RDWR).or_fail();
        co_await fs::ftruncate(fd, 4).or_fail();
        co_await fs::fsync(fd).or_fail();
        co_await fs::fdatasync(fd).or_fail();
        co_await fs::close(fd).or_fail();
    };

    auto [result] = run(shrink());
    EXPECT(result.has_value());
    EXPECT(test::read_file(path) == "0123");
}

ZEST_CASE(sendfile_copies_between_descriptors) {
    test::TempDir dir;
    test::write_file(dir.path / "source.txt", "sent-bytes");
    auto send = [&]() -> task<std::int64_t, error> {
        int in = co_await fs::open(dir.file("source.txt"), O_RDONLY).or_fail();
        int out = co_await fs::open(dir.file("target.txt"), O_CREAT | O_WRONLY, 0644).or_fail();
        auto sent = co_await fs::sendfile(out, in, 5, 5).or_fail();
        co_await fs::close(in).or_fail();
        co_await fs::close(out).or_fail();
        co_return sent;
    };

    auto [result] = run(send());
    ASSERT(result.has_value());
    EXPECT(*result == 5);
    EXPECT(test::read_file(dir.path / "target.txt") == "bytes");
}

ZEST_CASE(access_checks_that_a_file_exists) {
    test::TempDir dir;
    test::write_file(dir.path / "data.txt", "x");

    auto [result] = run(fs::access(dir.file("data.txt"), 0, loop));
    EXPECT(result.has_value());
}

ZEST_CASE(access_to_a_missing_file_fails) {
    test::TempDir dir;
    auto [result] = run(fs::access(dir.file("missing"), 0, loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::no_such_file_or_directory);
}

// futime needs a descriptor that may write attributes, which Windows does
// not give one opened read-only.
ZEST_CASE(utime_and_futime_set_the_times) {
    using std::chrono::seconds;
    test::TempDir dir;
    auto path = dir.file("data.txt");
    test::write_file(path, "x");
    auto touch = [&]() -> task<std::pair<fs::file_stats, fs::file_stats>, error> {
        co_await fs::utime(path, 1'000'000'000.0, 1'000'000'000.0).or_fail();
        auto by_path = co_await fs::stat(path).or_fail();
        int fd = co_await fs::open(path, O_RDWR).or_fail();
        co_await fs::futime(fd, 1'500'000'000.0, 1'500'000'000.0).or_fail();
        co_await fs::close(fd).or_fail();
        auto by_fd = co_await fs::stat(path).or_fail();
        co_return std::pair{by_path, by_fd};
    };

    auto [result] = run(touch());
    ASSERT(result.has_value());
    EXPECT(result->first.mtime == fs::file_time(seconds(1'000'000'000)));
    EXPECT(result->second.mtime == fs::file_time(seconds(1'500'000'000)));
}

ZEST_CASE(link_adds_a_name_for_the_same_file) {
    test::TempDir dir;
    test::write_file(dir.path / "data.txt", "linked");

    auto [result] = run(fs::link(dir.file("data.txt"), dir.file("alias.txt"), loop));
    EXPECT(result.has_value());
    EXPECT(test::read_file(dir.path / "alias.txt") == "linked");
    EXPECT(std::filesystem::hard_link_count(dir.path / "data.txt") == 2U);
}

ZEST_CASE(link_to_a_missing_file_fails) {
    test::TempDir dir;
    auto [result] = run(fs::link(dir.file("missing"), dir.file("other"), loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::no_such_file_or_directory);
}

ZEST_CASE(statfs_reports_the_filesystem) {
    test::TempDir dir;
    auto [result] = run(fs::statfs(dir.path.string(), loop));
    ASSERT(result.has_value());
    EXPECT(result->bsize > 0U);
    EXPECT(result->blocks > 0U);
}

ZEST_CASE(bad_descriptor_fails) {
    std::array<char, 8> buffer{};
    auto [stat_result, read_result] = run(fs::fstat(-1, loop), fs::read(-1, buffer, -1, loop));
    ASSERT(stat_result.has_error());
    EXPECT(stat_result.error() == error::bad_file_descriptor);
    ASSERT(read_result.has_error());
    EXPECT(read_result.error() == error::bad_file_descriptor);
}

// With every pool thread busy the request waits in the queue; cancelling it
// there dequeues it, so the directory is never made.
ZEST_CASE(cancel_while_queued_drops_the_request) {
    test::TempDir dir;
    test::BusyPool pool;
    event busy;
    auto target = [&]() -> task<void, error> {
        co_await busy.wait();
        co_await fs::mkdir(dir.file("never"), 0755).or_fail();
    };
    auto request = target();
    auto* node = request.operator->();
    auto cancel_it = [&]() -> task<> {
        co_await busy.wait();
        node->cancel();
        pool.release();
    };

    auto [held, cancelled, driver] = run(pool.hold(busy), std::move(request), cancel_it());
    EXPECT(held.has_value());
    EXPECT(cancelled.is_cancelled());
    EXPECT(!std::filesystem::exists(dir.path / "never"));
}

#ifndef _WIN32

ZEST_CASE(symlink_is_read_resolved_and_described_as_a_link) {
    test::TempDir dir;
    test::write_file(dir.path / "target.txt", "x");
    auto target = dir.file("target.txt");
    auto link = dir.file("link.txt");

    struct Seen {
        std::string target;
        std::string resolved;
        fs::file_stats link;
        fs::file_stats file;
    };

    auto follow = [&]() -> task<Seen, error> {
        co_await fs::symlink(target, link).or_fail();
        co_await fs::lutime(link, 1'000'000'000.0, 1'000'000'000.0).or_fail();
        co_return Seen{
            .target = co_await fs::readlink(link).or_fail(),
            .resolved = co_await fs::realpath(link).or_fail(),
            .link = co_await fs::lstat(link).or_fail(),
            .file = co_await fs::stat(link).or_fail(),
        };
    };

    auto [result] = run(follow());
    ASSERT(result.has_value());
    EXPECT(result->target == target);
    EXPECT(result->resolved == std::filesystem::canonical(dir.path / "target.txt").string());
    EXPECT((result->link.mode & S_IFMT) == S_IFLNK);
    EXPECT(result->link.mtime == fs::file_time(std::chrono::seconds(1'000'000'000)));
    EXPECT((result->file.mode & S_IFMT) == S_IFREG);
}

ZEST_CASE(readlink_of_a_missing_link_fails) {
    test::TempDir dir;
    auto [result] = run(fs::readlink(dir.file("missing"), loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::no_such_file_or_directory);
}

ZEST_CASE(chmod_and_fchmod_set_the_mode) {
    test::TempDir dir;
    auto path = dir.file("data.txt");
    test::write_file(path, "x");
    auto change = [&]() -> task<std::pair<std::uint64_t, std::uint64_t>, error> {
        co_await fs::chmod(path, 0600).or_fail();
        auto after_chmod = co_await fs::stat(path).or_fail();
        int fd = co_await fs::open(path, O_RDONLY).or_fail();
        co_await fs::fchmod(fd, 0640).or_fail();
        co_await fs::close(fd).or_fail();
        auto after_fchmod = co_await fs::stat(path).or_fail();
        co_return std::pair{after_chmod.mode & 0777, after_fchmod.mode & 0777};
    };

    auto [result] = run(change());
    ASSERT(result.has_value());
    EXPECT(result->first == 0600U);
    EXPECT(result->second == 0640U);
}

// Handing a file to its own owner needs no privilege.
ZEST_CASE(chown_fchown_and_lchown_to_the_owner_succeed) {
    test::TempDir dir;
    auto path = dir.file("data.txt");
    auto link = dir.file("link.txt");
    test::write_file(path, "x");
    auto keep_owner = [&]() -> task<void, error> {
        auto stats = co_await fs::stat(path).or_fail();
        auto uid = static_cast<std::uint32_t>(stats.uid);
        auto gid = static_cast<std::uint32_t>(stats.gid);
        co_await fs::chown(path, uid, gid).or_fail();
        int fd = co_await fs::open(path, O_RDONLY).or_fail();
        co_await fs::fchown(fd, uid, gid).or_fail();
        co_await fs::close(fd).or_fail();
        co_await fs::symlink(path, link).or_fail();
        co_await fs::lchown(link, uid, gid).or_fail();
    };

    auto [result] = run(keep_owner());
    EXPECT(result.has_value());
}

ZEST_CASE(chown_of_a_missing_file_fails) {
    test::TempDir dir;
    auto [result] = run(fs::chown(dir.file("missing"), 0, 0, loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::no_such_file_or_directory);
}

#endif  // !_WIN32

};  // ZEST_SUITE(async_io_fs)

}  // namespace

}  // namespace kota
