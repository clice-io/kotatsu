#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <thread>
#include <utility>

#include "async/harness/os.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

// Files are set up and read back with test::write_text and test::read_text,
// not kota::async.

/// Waits, blocking this thread, until `dir` holds an entry; false if none
/// came within ten seconds. It polls, as nothing signals that a pool thread
/// has made one.
bool wait_for_an_entry(const std::filesystem::path& dir) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while(std::filesystem::is_empty(dir)) {
        if(std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

ZEST_SUITE(async_io_fs, zest::LoopFixture) {

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
        int out = co_await fs::async::open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644).or_fail();
        seen.written = co_await fs::async::write(out, std::string_view("kotatsu-fs")).or_fail();
        co_await fs::async::close(out).or_fail();

        int in = co_await fs::async::open(path, O_RDONLY).or_fail();
        std::array<char, 64> buffer{};
        auto count = co_await fs::async::read(in, buffer).or_fail();
        seen.read.assign(buffer.data(), count);
        seen.read_at_end = co_await fs::async::read(in, buffer).or_fail();
        co_await fs::async::close(in).or_fail();
        co_return seen;
    };

    auto [result] = run(roundtrip());
    ZASSERT(result.has_value());
    ZEXPECT(result->written == 10U);
    ZEXPECT(result->read == "kotatsu-fs");
    ZEXPECT(result->read_at_end == 0U);
    ZEXPECT(test::read_text(path) == "kotatsu-fs");
}

ZEST_CASE(read_and_write_at_an_offset) {
    test::TempDir dir;
    auto path = dir.file("data.txt");
    test::write_text(path, "hello world");
    auto patch = [&]() -> task<std::string, error> {
        int fd = co_await fs::async::open(path, O_RDWR).or_fail();
        co_await fs::async::write(fd, std::string_view("WORLD"), 6).or_fail();
        std::array<char, 5> buffer{};
        auto count = co_await fs::async::read(fd, buffer, 6).or_fail();
        co_await fs::async::close(fd).or_fail();
        co_return std::string(buffer.data(), count);
    };

    auto [result] = run(patch());
    ZASSERT(result.has_value());
    ZEXPECT(*result == "WORLD");
    ZEXPECT(test::read_text(path) == "hello WORLD");
}

ZEST_CASE(open_of_a_missing_file_fails) {
    test::TempDir dir;
    auto [result] = run(fs::async::open(dir.file("missing"), O_RDONLY, 0, loop));
    ZASSERT(result.has_error());
    ZEXPECT(result.error() == error::no_such_file_or_directory);
}

ZEST_CASE(stat_and_fstat_describe_a_file) {
    test::TempDir dir;
    auto path = dir.file("data.txt");
    test::write_text(path, "12345");
    auto describe = [&]() -> task<std::pair<fs::async::file_stats, fs::async::file_stats>, error> {
        auto by_path = co_await fs::async::stat(path).or_fail();
        int fd = co_await fs::async::open(path, O_RDONLY).or_fail();
        auto by_fd = co_await fs::async::fstat(fd).or_fail();
        co_await fs::async::close(fd).or_fail();
        co_return std::pair{by_path, by_fd};
    };

    auto [result] = run(describe());
    ZASSERT(result.has_value());
    auto& [by_path, by_fd] = *result;
    ZEXPECT(by_path.size == 5U);
    ZEXPECT(by_path.nlink == 1U);
    ZEXPECT(by_fd.size == 5U);
    ZEXPECT(by_fd.ino == by_path.ino);
#ifndef _WIN32
    ZEXPECT((by_path.mode & S_IFMT) == S_IFREG);
#endif
}

ZEST_CASE(stat_of_a_missing_file_fails) {
    test::TempDir dir;
    auto [result] = run(fs::async::stat(dir.file("missing"), loop));
    ZASSERT(result.has_error());
    ZEXPECT(result.error() == error::no_such_file_or_directory);
}

ZEST_CASE(unlink_removes_a_file) {
    test::TempDir dir;
    test::write_text(dir.file("data.txt"), "x");

    auto [result] = run(fs::async::unlink(dir.file("data.txt"), loop));
    ZEXPECT(result.has_value());
    ZEXPECT(!std::filesystem::exists(dir.path / "data.txt"));
}

ZEST_CASE(unlink_of_a_missing_file_fails) {
    test::TempDir dir;
    auto [result] = run(fs::async::unlink(dir.file("missing"), loop));
    ZASSERT(result.has_error());
    ZEXPECT(result.error() == error::no_such_file_or_directory);
}

ZEST_CASE(rename_moves_a_file) {
    test::TempDir dir;
    test::write_text(dir.file("from.txt"), "moved");

    auto [result] = run(fs::async::rename(dir.file("from.txt"), dir.file("to.txt"), loop));
    ZEXPECT(result.has_value());
    ZEXPECT(!std::filesystem::exists(dir.path / "from.txt"));
    ZEXPECT(test::read_text(dir.file("to.txt")) == "moved");
}

ZEST_CASE(rename_of_a_missing_file_fails) {
    test::TempDir dir;
    auto [result] = run(fs::async::rename(dir.file("missing"), dir.file("other"), loop));
    ZASSERT(result.has_error());
    ZEXPECT(result.error() == error::no_such_file_or_directory);
}

// A clone falls back to a copy where the filesystem cannot share blocks.
ZEST_CASE(copyfile_copies_the_contents) {
    test::TempDir dir;
    test::write_text(dir.file("source.txt"), "copied");

    auto [copied, cloned] = run(
        fs::async::copyfile(dir.file("source.txt"), dir.file("copy.txt"), {}, loop),
        fs::async::copyfile(dir.file("source.txt"), dir.file("clone.txt"), {.clone = true}, loop));
    ZEXPECT(copied.has_value());
    ZEXPECT(test::read_text(dir.file("copy.txt")) == "copied");
    ZEXPECT(cloned.has_value());
    ZEXPECT(test::read_text(dir.file("clone.txt")) == "copied");
}

ZEST_CASE(copyfile_onto_a_file_with_excl_fails) {
    test::TempDir dir;
    test::write_text(dir.file("source.txt"), "copied");
    test::write_text(dir.file("taken.txt"), "keep");

    auto [result] = run(
        fs::async::copyfile(dir.file("source.txt"), dir.file("taken.txt"), {.excl = true}, loop));
    ZASSERT(result.has_error());
    ZEXPECT(result.error() == error::file_already_exists);
    ZEXPECT(test::read_text(dir.file("taken.txt")) == "keep");
}

ZEST_CASE(mkstemp_creates_and_opens_a_new_file) {
    test::TempDir dir;
    auto make = [&]() -> task<fs::async::mkstemp_result, error> {
        auto created = co_await fs::async::mkstemp(dir.file("file-XXXXXX")).or_fail();
        co_await fs::async::close(created.fd).or_fail();
        co_return created;
    };

    auto [created] = run(make());
    ZASSERT(created.has_value());
    ZEXPECT(created->fd >= 0);
    ZEXPECT(std::filesystem::is_regular_file(created->path));
    ZEXPECT(zest::starts_with(created->path, dir.file("file-")));
}

ZEST_CASE(mkstemp_without_a_template_fails) {
    test::TempDir dir;
    auto [result] = run(fs::async::mkstemp(dir.file("no-template"), loop));
    ZASSERT(result.has_error());
    ZEXPECT(result.error() == error::invalid_argument);
}

ZEST_CASE(ftruncate_fsync_and_fdatasync_act_on_a_descriptor) {
    test::TempDir dir;
    auto path = dir.file("data.txt");
    test::write_text(path, "0123456789");
    auto shrink = [&]() -> task<void, error> {
        int fd = co_await fs::async::open(path, O_RDWR).or_fail();
        co_await fs::async::ftruncate(fd, 4).or_fail();
        co_await fs::async::fsync(fd).or_fail();
        co_await fs::async::fdatasync(fd).or_fail();
        co_await fs::async::close(fd).or_fail();
    };

    auto [result] = run(shrink());
    ZEXPECT(result.has_value());
    ZEXPECT(test::read_text(path) == "0123");
}

ZEST_CASE(sendfile_copies_between_descriptors) {
    test::TempDir dir;
    test::write_text(dir.file("source.txt"), "sent-bytes");
    auto send = [&]() -> task<std::int64_t, error> {
        int in = co_await fs::async::open(dir.file("source.txt"), O_RDONLY).or_fail();
        int out =
            co_await fs::async::open(dir.file("target.txt"), O_CREAT | O_WRONLY, 0644).or_fail();
        auto sent = co_await fs::async::sendfile(out, in, 5, 5).or_fail();
        co_await fs::async::close(in).or_fail();
        co_await fs::async::close(out).or_fail();
        co_return sent;
    };

    auto [result] = run(send());
    ZASSERT(result.has_value());
    ZEXPECT(*result == 5);
    ZEXPECT(test::read_text(dir.file("target.txt")) == "bytes");
}

ZEST_CASE(access_checks_that_a_file_exists) {
    test::TempDir dir;
    test::write_text(dir.file("data.txt"), "x");

    auto [result] = run(fs::async::access(dir.file("data.txt"), 0, loop));
    ZEXPECT(result.has_value());
}

ZEST_CASE(access_to_a_missing_file_fails) {
    test::TempDir dir;
    auto [result] = run(fs::async::access(dir.file("missing"), 0, loop));
    ZASSERT(result.has_error());
    ZEXPECT(result.error() == error::no_such_file_or_directory);
}

// futime needs a descriptor that may write attributes, which Windows does
// not give one opened read-only.
ZEST_CASE(utime_and_futime_set_the_times) {
    using std::chrono::seconds;
    test::TempDir dir;
    auto path = dir.file("data.txt");
    test::write_text(path, "x");
    auto touch = [&]() -> task<std::pair<fs::async::file_stats, fs::async::file_stats>, error> {
        co_await fs::async::utime(path, 1'000'000'000.0, 1'000'000'000.0).or_fail();
        auto by_path = co_await fs::async::stat(path).or_fail();
        int fd = co_await fs::async::open(path, O_RDWR).or_fail();
        co_await fs::async::futime(fd, 1'500'000'000.0, 1'500'000'000.0).or_fail();
        co_await fs::async::close(fd).or_fail();
        auto by_fd = co_await fs::async::stat(path).or_fail();
        co_return std::pair{by_path, by_fd};
    };

    auto [result] = run(touch());
    ZASSERT(result.has_value());
    ZEXPECT(result->first.mtime == fs::async::file_time(seconds(1'000'000'000)));
    ZEXPECT(result->second.mtime == fs::async::file_time(seconds(1'500'000'000)));
}

ZEST_CASE(link_adds_a_name_for_the_same_file) {
    test::TempDir dir;
    test::write_text(dir.file("data.txt"), "linked");

    auto [result] = run(fs::async::link(dir.file("data.txt"), dir.file("alias.txt"), loop));
    ZEXPECT(result.has_value());
    ZEXPECT(test::read_text(dir.file("alias.txt")) == "linked");
    ZEXPECT(std::filesystem::hard_link_count(dir.path / "data.txt") == 2U);
}

ZEST_CASE(link_to_a_missing_file_fails) {
    test::TempDir dir;
    auto [result] = run(fs::async::link(dir.file("missing"), dir.file("other"), loop));
    ZASSERT(result.has_error());
    ZEXPECT(result.error() == error::no_such_file_or_directory);
}

ZEST_CASE(statfs_reports_the_filesystem) {
    test::TempDir dir;
    auto [result] = run(fs::async::statfs(dir.root(), loop));
    ZASSERT(result.has_value());
    ZEXPECT(result->bsize > 0U);
    ZEXPECT(result->blocks > 0U);
}

ZEST_CASE(bad_descriptor_fails) {
    std::array<char, 8> buffer{};
    auto [stat_result, read_result] =
        run(fs::async::fstat(-1, loop), fs::async::read(-1, buffer, -1, loop));
    ZASSERT(stat_result.has_error());
    ZEXPECT(stat_result.error() == error::bad_file_descriptor);
    ZASSERT(read_result.has_error());
    ZEXPECT(read_result.error() == error::bad_file_descriptor);
}

// With every pool thread busy the request waits in the queue; cancelling it
// there dequeues it, so the directory is never made. Once the pool is busy,
// the request is queued first and the trigger then ends, which makes
// when_any cancel the request.
ZEST_CASE(cancel_while_queued_drops_the_request) {
    test::TempDir dir;
    test::BusyPool pool;
    event busy;
    auto target = [&]() -> task<void, error> {
        co_await busy.wait();
        co_await fs::async::mkdir(dir.file("never"), 0755).or_fail();
    };
    auto trigger = [&]() -> task<> {
        co_await busy.wait();
    };
    auto cancel_queued = [&]() -> task<std::size_t, error> {
        auto first = co_await or_fail(co_await when_any(target(), trigger()));
        pool.release();
        co_return first.index();
    };

    auto [held, raced] = run(pool.hold(busy), cancel_queued());
    ZEXPECT(held.has_value());
    ZASSERT(raced.has_value());
    ZEXPECT(*raced == 1U);
    ZEXPECT(!std::filesystem::exists(dir.path / "never"));
}

#ifndef _WIN32
// Opening a FIFO to read holds open() on its pool thread until a writer comes.
// The writer's non-blocking open succeeds only once that reader waits, and
// ends the race at once: the cancel comes too late to dequeue the open, which
// then succeeds. The descriptor it made is closed rather than lost, so the
// FIFO is left without a reader, which a second writer's open reports. The
// writer polls, as nothing signals that the pool thread has started the open.
ZEST_CASE(open_cancelled_too_late_closes_what_it_opened) {
    test::TempDir dir;
    const auto fifo = dir.file("fifo");
    ZASSERT(::mkfifo(fifo.c_str(), 0600) == 0);
    int writer = -1;
    auto open_writer = [&]() -> task<> {
        while((writer = ::open(fifo.c_str(), O_WRONLY | O_NONBLOCK)) < 0) {
            co_await sleep(1);
        }
    };
    auto race = [&]() -> task<std::size_t, error> {
        auto first =
            co_await or_fail(co_await when_any(fs::async::open(fifo, O_RDONLY), open_writer()));
        co_return first.index();
    };

    auto [raced] = run(race());
    ZASSERT(raced.has_value());
    ZEXPECT(*raced == 1U);
    ZASSERT(writer >= 0);
    ZEXPECT(::close(writer) == 0);
    const int again = ::open(fifo.c_str(), O_WRONLY | O_NONBLOCK);
    const int reason = errno;
    ZEXPECT(again == -1);
    ZEXPECT(reason == ENXIO);
    if(again >= 0) {
        ::close(again);
    }
}
#endif

// mkstemp makes its file on a pool thread, while the loop thread waits for
// it without letting the loop turn: the race's cancel reaches mkstemp after
// it took effect. It closes the file it made and removes it.
ZEST_CASE(mkstemp_cancelled_too_late_removes_what_it_made) {
    test::TempDir dir;
    bool made = false;
    auto wait_until_made = [&]() -> task<> {
        made = wait_for_an_entry(dir.path);
        co_return;
    };
    auto race = [&]() -> task<std::size_t, error> {
        auto first = co_await or_fail(
            co_await when_any(fs::async::mkstemp(dir.file("file-XXXXXX")), wait_until_made()));
        co_return first.index();
    };

    auto [raced] = run(race());
    ZASSERT(raced.has_value());
    ZEXPECT(*raced == 1U);
    ZEXPECT(made);
    ZEXPECT(std::filesystem::is_empty(dir.path));
}

// As for mkstemp: mkdtemp removes the directory it made.
ZEST_CASE(mkdtemp_cancelled_too_late_removes_what_it_made) {
    test::TempDir dir;
    bool made = false;
    auto wait_until_made = [&]() -> task<> {
        made = wait_for_an_entry(dir.path);
        co_return;
    };
    auto race = [&]() -> task<std::size_t, error> {
        auto first = co_await or_fail(
            co_await when_any(fs::async::mkdtemp(dir.file("dir-XXXXXX")), wait_until_made()));
        co_return first.index();
    };

    auto [raced] = run(race());
    ZASSERT(raced.has_value());
    ZEXPECT(*raced == 1U);
    ZEXPECT(made);
    ZEXPECT(std::filesystem::is_empty(dir.path));
}

#ifndef _WIN32

ZEST_CASE(symlink_is_read_resolved_and_described_as_a_link) {
    test::TempDir dir;
    test::write_text(dir.file("target.txt"), "x");
    auto target = dir.file("target.txt");
    auto link = dir.file("link.txt");

    struct Seen {
        std::string target;
        std::string resolved;
        fs::async::file_stats link;
        fs::async::file_stats file;
    };

    auto follow = [&]() -> task<Seen, error> {
        co_await fs::async::symlink(target, link).or_fail();
        co_await fs::async::lutime(link, 1'000'000'000.0, 1'000'000'000.0).or_fail();
        co_return Seen{
            .target = co_await fs::async::readlink(link).or_fail(),
            .resolved = co_await fs::async::realpath(link).or_fail(),
            .link = co_await fs::async::lstat(link).or_fail(),
            .file = co_await fs::async::stat(link).or_fail(),
        };
    };

    auto [result] = run(follow());
    ZASSERT(result.has_value());
    ZEXPECT(result->target == target);
    ZEXPECT(result->resolved == std::filesystem::canonical(dir.path / "target.txt").string());
    ZEXPECT((result->link.mode & S_IFMT) == S_IFLNK);
    ZEXPECT(result->link.mtime == fs::async::file_time(std::chrono::seconds(1'000'000'000)));
    ZEXPECT((result->file.mode & S_IFMT) == S_IFREG);
}

ZEST_CASE(readlink_of_a_missing_link_fails) {
    test::TempDir dir;
    auto [result] = run(fs::async::readlink(dir.file("missing"), loop));
    ZASSERT(result.has_error());
    ZEXPECT(result.error() == error::no_such_file_or_directory);
}

ZEST_CASE(chmod_and_fchmod_set_the_mode) {
    test::TempDir dir;
    auto path = dir.file("data.txt");
    test::write_text(path, "x");
    auto change = [&]() -> task<std::pair<std::uint64_t, std::uint64_t>, error> {
        co_await fs::async::chmod(path, 0600).or_fail();
        auto after_chmod = co_await fs::async::stat(path).or_fail();
        int fd = co_await fs::async::open(path, O_RDONLY).or_fail();
        co_await fs::async::fchmod(fd, 0640).or_fail();
        co_await fs::async::close(fd).or_fail();
        auto after_fchmod = co_await fs::async::stat(path).or_fail();
        co_return std::pair{after_chmod.mode & 0777, after_fchmod.mode & 0777};
    };

    auto [result] = run(change());
    ZASSERT(result.has_value());
    ZEXPECT(result->first == 0600U);
    ZEXPECT(result->second == 0640U);
}

// Handing a file to its own owner needs no privilege.
ZEST_CASE(chown_fchown_and_lchown_to_the_owner_succeed) {
    test::TempDir dir;
    auto path = dir.file("data.txt");
    auto link = dir.file("link.txt");
    test::write_text(path, "x");
    auto keep_owner = [&]() -> task<void, error> {
        auto stats = co_await fs::async::stat(path).or_fail();
        auto uid = static_cast<std::uint32_t>(stats.uid);
        auto gid = static_cast<std::uint32_t>(stats.gid);
        co_await fs::async::chown(path, uid, gid).or_fail();
        int fd = co_await fs::async::open(path, O_RDONLY).or_fail();
        co_await fs::async::fchown(fd, uid, gid).or_fail();
        co_await fs::async::close(fd).or_fail();
        co_await fs::async::symlink(path, link).or_fail();
        co_await fs::async::lchown(link, uid, gid).or_fail();
    };

    auto [result] = run(keep_owner());
    ZEXPECT(result.has_value());
}

ZEST_CASE(chown_of_a_missing_file_fails) {
    test::TempDir dir;
    auto [result] = run(fs::async::chown(dir.file("missing"), 0, 0, loop));
    ZASSERT(result.has_error());
    ZEXPECT(result.error() == error::no_such_file_or_directory);
}

#endif  // !_WIN32

};  // ZEST_SUITE(async_io_fs)

}  // namespace

}  // namespace kota
