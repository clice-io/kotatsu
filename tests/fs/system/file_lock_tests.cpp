#include <array>
#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/fs/file.h"

namespace kota::fs {

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
}

ZEST_CASE(unlock_without_a_lock_does_nothing) {
    LockFile lock;
    auto file = lock.open();
    ZEXPECT(!file.unlock());
    ZEXPECT(file.try_lock() == true);
}

};  // ZEST_SUITE(fs_file_lock)

}  // namespace

}  // namespace kota::fs
