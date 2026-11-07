#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "fs/harness/files.h"
#include "fs/harness/win32.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/fs/file.h"

namespace kotatsu::fs {

namespace {

/// A lock file in a directory of its own, and handles of it opened apart,
/// as two processes would hold them.
struct LockFile {
    LockFile() {
        test::write_text(dir.file("lock"), "text");
    }

    File open() {
        auto file = File::open(dir.file("lock"), {.read = true, .write = true});
        ZASSERT(file.has_value());
        return std::move(*file);
    }

    test::TempDir dir;
};

/// A copy of the handle of `file`, as a child process that inherited it
/// would hold, closed when this goes.
struct HandleCopy {
    explicit HandleCopy(const File& file) {
#ifdef _WIN32
        auto process = ::GetCurrentProcess();
        ZASSERT(::DuplicateHandle(process,
                                  file.native_handle(),
                                  process,
                                  &handle,
                                  0,
                                  FALSE,
                                  DUPLICATE_SAME_ACCESS));
#else
        handle = ::fcntl(file.native_handle(), F_DUPFD_CLOEXEC, 0);
        ZASSERT(handle >= 0);
#endif
    }

    ~HandleCopy() {
#ifdef _WIN32
        ::CloseHandle(handle);
#else
        ::close(handle);
#endif
    }

    HandleCopy(const HandleCopy&) = delete;
    HandleCopy& operator=(const HandleCopy&) = delete;

    File::native_handle_type handle;
};

enum class RangeKind : std::uint8_t {
    Shared,
    Exclusive,
};

/// A byte-range lock of the `length` bytes from `offset` of the lock file
/// that another holder holds while this lives: on Windows another handle,
/// by LockFileEx; on POSIX, where a process never sees its own record locks,
/// a child process, by fcntl, as SQLite and LMDB take them.
struct RangeLock {
    RangeLock(LockFile& lock, RangeKind kind, std::uint32_t offset, std::uint32_t length) :
        file(lock.open()) {
#ifdef _WIN32
        OVERLAPPED at{};
        at.Offset = offset;
        DWORD flags = (kind == RangeKind::Exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0) |
                      LOCKFILE_FAIL_IMMEDIATELY;
        ZASSERT(::LockFileEx(file.native_handle(), flags, 0, length, 0, &at));
#else
        int taken_pipe[2];
        int release_pipe[2];
        ZASSERT(::pipe(taken_pipe) == 0);
        ZASSERT(::pipe(release_pipe) == 0);
        child = ::fork();
        ZASSERT(child >= 0);
        if(child == 0) {
            // Async-signal-safe calls alone, in the fork of a process that
            // may run threads.
            ::close(release_pipe[1]);
            struct flock range{};
            range.l_type = kind == RangeKind::Exclusive ? F_WRLCK : F_RDLCK;
            range.l_whence = SEEK_SET;
            range.l_start = offset;
            range.l_len = length;
            char taken = ::fcntl(file.native_handle(), F_SETLK, &range) == 0 ? 1 : 0;
            [[maybe_unused]] auto told = ::write(taken_pipe[1], &taken, 1);
            // Holds it until the end of the pipe in this process closes, as
            // it does when this goes, or when a failed ZASSERT ends the
            // process: no child stays behind.
            char released;
            [[maybe_unused]] auto waited = ::read(release_pipe[0], &released, 1);
            ::_exit(0);
        }
        ::close(taken_pipe[1]);
        ::close(release_pipe[0]);
        release = release_pipe[1];
        char taken = 0;
        auto told = ::read(taken_pipe[0], &taken, 1);
        ::close(taken_pipe[0]);
        ZASSERT(told == 1);
        ZASSERT(taken == 1);
#endif
    }

    ~RangeLock() {
#ifndef _WIN32
        ::close(release);
        ::waitpid(child, nullptr, 0);
#endif
    }

    RangeLock(const RangeLock&) = delete;
    RangeLock& operator=(const RangeLock&) = delete;

    File file;
#ifndef _WIN32
    pid_t child;
    int release;
#endif
};

constexpr auto every_byte = std::numeric_limits<std::uint64_t>::max();

ZEST_SUITE(fs_file_lock) {

ZEST_CASE(lock_keeps_other_handles_from_locking) {
    LockFile lock;
    auto first = lock.open();
    auto second = lock.open();
    ZEXPECT(!first.lock());
    ZEXPECT(second.try_lock() == false);
    ZEXPECT(second.try_lock_shared() == false);
    ZEXPECT(!first.unlock());
    ZEXPECT(second.try_lock() == true);
    ZEXPECT(first.try_lock_shared() == false);
}

ZEST_CASE(lock_waits_until_the_other_handle_unlocks) {
    LockFile lock;
    auto first = lock.open();
    auto second = lock.open();
    ZEXPECT(!first.lock());
    std::atomic<bool> locked = false;
    std::error_code error;
    std::thread waiter([&] {
        error = second.lock();
        locked = true;
    });
    // A wait, to show the lock does not get through early.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ZEXPECT(!locked.load());
    ZEXPECT(!first.unlock());
    waiter.join();
    ZEXPECT(locked.load());
    ZEXPECT(error == std::error_code());
    ZEXPECT(first.try_lock() == false);
}

ZEST_CASE(a_file_open_to_read_and_append_locks) {
    LockFile lock;
    auto appending = File::open(lock.dir.file("lock"), {.read = true, .append = true});
    ZASSERT(appending.has_value());
    auto other = lock.open();
    ZEXPECT(!appending->lock());
    ZEXPECT(other.try_lock() == false);
    ZEXPECT(!appending->unlock());
    ZEXPECT(appending->try_lock() == true);
    ZEXPECT(other.try_lock() == false);
}

ZEST_CASE(lock_keeps_no_one_from_the_bytes) {
    LockFile lock;
    auto first = lock.open();
    auto second = lock.open();
    ZEXPECT(!first.lock());
    std::array<char, 16> buffer{};
    auto count = second.read(buffer);
    ZASSERT(count.has_value());
    ZEXPECT(std::string_view(buffer.data(), *count) == "text");
    ZEXPECT(!second.write("more"));
    ZEXPECT(test::read_text(lock.dir.file("lock")) == "textmore");
}

ZEST_CASE(lock_shared_lets_others_share) {
    LockFile lock;
    auto first = lock.open();
    auto second = lock.open();
    auto third = lock.open();
    ZEXPECT(!first.lock_shared());
    ZEXPECT(second.try_lock_shared() == true);
    ZEXPECT(third.try_lock() == false);
    ZEXPECT(!first.unlock());
    ZEXPECT(third.try_lock() == false);
    ZEXPECT(!second.unlock());
    ZEXPECT(third.try_lock() == true);
}

ZEST_CASE(a_lock_replaces_the_one_its_handle_holds) {
    LockFile lock;
    auto first = lock.open();
    auto second = lock.open();
    ZEXPECT(!first.lock_shared());
    ZEXPECT(!first.lock());
    ZEXPECT(second.try_lock_shared() == false);
    ZEXPECT(!first.lock_shared());
    ZEXPECT(second.try_lock_shared() == true);
    ZEXPECT(!second.unlock());
    ZEXPECT(first.try_lock() == true);
    ZEXPECT(first.try_lock() == true);
    ZEXPECT(second.try_lock_shared() == false);
}

ZEST_CASE(closing_releases_the_lock) {
    LockFile lock;
    auto first = lock.open();
    auto second = lock.open();
    ZEXPECT(!first.lock());
    ZEXPECT(!first.close());
    ZEXPECT(second.try_lock() == true);
}

ZEST_CASE(closing_releases_the_lock_a_copy_of_the_handle_shares) {
    LockFile lock;
    auto first = lock.open();
    auto second = lock.open();
    ZEXPECT(!first.lock());
    HandleCopy copy(first);
    ZEXPECT(!first.close());
    ZEXPECT(second.try_lock() == true);
}

#ifndef _WIN32
ZEST_CASE(closing_after_unlock_leaves_a_lock_a_copy_of_the_handle_took) {
    // flock() locks belong to what the copies share: the File unlocked,
    // closing it leaves the lock the copy took since.
    LockFile lock;
    auto first = lock.open();
    auto second = lock.open();
    ZEXPECT(!first.lock());
    ZEXPECT(!first.unlock());
    HandleCopy copy(first);
    ZASSERT(::flock(copy.handle, LOCK_EX | LOCK_NB) == 0);
    ZEXPECT(!first.close());
    ZEXPECT(second.try_lock() == false);
}
#endif

ZEST_CASE(dropping_a_file_releases_its_lock) {
    LockFile lock;
    auto second = lock.open();
    std::optional<HandleCopy> copy;
    {
        auto first = lock.open();
        ZEXPECT(!first.lock_shared());
        copy.emplace(first);
    }
    ZEXPECT(second.try_lock() == true);
}

ZEST_CASE(a_moved_file_releases_the_lock_it_was_given) {
    LockFile lock;
    auto first = lock.open();
    auto second = lock.open();
    ZEXPECT(first.try_lock() == true);
    File moved = std::move(first);
    HandleCopy copy(moved);
    ZEXPECT(!moved.close());
    ZEXPECT(second.try_lock() == true);
    ZEXPECT(!second.unlock());
    // Assigned too, over a File of its own.
    auto third = lock.open();
    ZEXPECT(third.try_lock() == true);
    File assigned = lock.open();
    assigned = std::move(third);
    HandleCopy assigned_copy(assigned);
    ZEXPECT(!assigned.close());
    ZEXPECT(second.try_lock() == true);
}

ZEST_CASE(unlock_without_a_lock_does_nothing) {
    LockFile lock;
    auto file = lock.open();
    ZEXPECT(!file.unlock());
    ZEXPECT(file.try_lock() == true);
}

ZEST_CASE(range_locked_sees_a_lock_another_holds) {
    LockFile lock;
    RangeLock held(lock, RangeKind::Exclusive, 10, 10);
    // To probe takes no right to write.
    auto probe = File::open(lock.dir.file("lock"));
    ZASSERT(probe.has_value());
    ZEXPECT(probe->range_locked(0, 11) == true);
    ZEXPECT(probe->range_locked(19, 5) == true);
    ZEXPECT(probe->range_locked(12, 1) == true);
    ZEXPECT(probe->range_locked(0, every_byte) == true);
}

ZEST_CASE(range_locked_sees_a_shared_lock) {
    LockFile lock;
    RangeLock held(lock, RangeKind::Shared, 10, 10);
    auto probe = lock.open();
    ZEXPECT(probe.range_locked(15, 1) == true);
}

ZEST_CASE(range_locked_of_bytes_beside_a_lock_is_false) {
    LockFile lock;
    RangeLock held(lock, RangeKind::Exclusive, 10, 10);
    auto probe = lock.open();
    ZEXPECT(probe.range_locked(0, 10) == false);
    ZEXPECT(probe.range_locked(20, 10) == false);
}

ZEST_CASE(range_locked_of_a_file_no_one_locks_is_false) {
    LockFile lock;
    auto probe = lock.open();
    ZEXPECT(probe.range_locked(0, every_byte) == false);
}

ZEST_CASE(range_locked_leaves_the_range_free) {
    // Windows takes the range to probe it.
    LockFile lock;
    auto probe = lock.open();
    ZEXPECT(probe.range_locked(0, 10) == false);
    RangeLock held(lock, RangeKind::Exclusive, 0, 10);
    ZEXPECT(probe.range_locked(0, 10) == true);
}

ZEST_CASE(range_locked_sees_what_lock_takes_but_on_linux) {
    LockFile lock;
    auto holder = lock.open();
    auto probe = lock.open();
    ZEXPECT(!holder.lock());
#ifdef __linux__
    // flock()'s locks, a family apart from the record locks.
    ZEXPECT(probe.range_locked(0, every_byte) == false);
#else
    // macOS keeps flock()'s locks with the record locks, a lock of every
    // byte; on Windows a lock is of a byte past any file's end.
    ZEXPECT(probe.range_locked(0, every_byte) == true);
#endif
#ifdef _WIN32
    ZEXPECT(probe.range_locked(0, 4096) == false);
#endif
}

#ifndef _WIN32
ZEST_CASE(range_locked_sees_no_record_lock_of_this_process) {
    // A record lock belongs to the process, which never sees its own.
    LockFile lock;
    auto holder = lock.open();
    auto probe = lock.open();
    struct flock range{};
    range.l_type = F_WRLCK;
    range.l_whence = SEEK_SET;
    range.l_start = 0;
    range.l_len = 10;
    ZASSERT(::fcntl(holder.native_handle(), F_SETLK, &range) == 0);
    ZEXPECT(probe.range_locked(0, 10) == false);
}

ZEST_CASE(range_locked_past_the_largest_offset_fails) {
    LockFile lock;
    auto probe = lock.open();
    auto locked = probe.range_locked(std::uint64_t{1} << 63, 1);
    ZASSERT(!locked.has_value());
    ZEXPECT(locked.error() == std::errc::invalid_argument);
}
#endif

};  // ZEST_SUITE(fs_file_lock)

}  // namespace

}  // namespace kotatsu::fs
