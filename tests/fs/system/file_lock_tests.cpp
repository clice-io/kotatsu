#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

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

ZEST_CASE(unlock_without_a_lock_does_nothing) {
    LockFile lock;
    auto file = lock.open();
    ZEXPECT(!file.unlock());
    ZEXPECT(file.try_lock() == true);
}

};  // ZEST_SUITE(fs_file_lock)

}  // namespace

}  // namespace kota::fs
