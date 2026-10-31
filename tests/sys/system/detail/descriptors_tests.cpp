#ifndef _WIN32

#include <cerrno>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sys/harness/streams.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/support/scope_exit.h"
#include "kotatsu/sys/detail/descriptors.h"

namespace kotatsu::sys::detail {

namespace {

/// /dev/null opened close-on-exec, on the lowest number free.
int open_null() {
    return ::open("/dev/null", O_RDONLY | O_CLOEXEC);
}

ZEST_SUITE(sys_detail_descriptors) {

ZEST_CASE(descriptor_on_a_standard_stream_moves_above_it) {
    test::ScopedClosedStream closed(0);
    const int fd = open_null();
    ZASSERT(fd == 0);
    struct stat opened = {};
    ZASSERT(::fstat(fd, &opened) == 0);
    const int moved = move_above_standard_streams(fd);
    ZASSERT(moved > 2);
    ZEXPECT(::fcntl(0, F_GETFD) == -1);
    ZEXPECT(::fcntl(moved, F_GETFD) == FD_CLOEXEC);
    struct stat kept = {};
    ZASSERT(::fstat(moved, &kept) == 0);
    ZEXPECT(kept.st_dev == opened.st_dev);
    ZEXPECT(kept.st_ino == opened.st_ino);
    ::close(moved);
}

ZEST_CASE(descriptor_above_the_standard_streams_stays) {
    const int fd = open_null();
    ZASSERT(fd > 2);
    ZEXPECT(move_above_standard_streams(fd) == fd);
    ZEXPECT(::fcntl(fd, F_GETFD) == FD_CLOEXEC);
    ::close(fd);
}

// The error of the call that made none stays.
ZEST_CASE(negative_descriptor_passes_through) {
    errno = EMFILE;
    ZEXPECT(move_above_standard_streams(-1) == -1);
    ZEXPECT(errno == EMFILE);
}

// No number above 2 allowed: the move fails, and the descriptor closes.
ZEST_CASE(move_past_the_descriptor_limit_fails) {
    test::ScopedClosedStream closed(0);
    const int fd = open_null();
    ZASSERT(fd == 0);

    // This process's soft limit of descriptors is 3 until the case ends.
    rlimit previous = {};
    ZASSERT(::getrlimit(RLIMIT_NOFILE, &previous) == 0);
    rlimit lowered = previous;
    lowered.rlim_cur = 3;
    ZASSERT(::setrlimit(RLIMIT_NOFILE, &lowered) == 0);
    ScopeExit restore([&previous] { ::setrlimit(RLIMIT_NOFILE, &previous); });

    errno = 0;
    ZEXPECT(move_above_standard_streams(fd) == -1);
    ZEXPECT(errno == EINVAL);
    ZEXPECT(::fcntl(0, F_GETFD) == -1);
}

};  // ZEST_SUITE(sys_detail_descriptors)

}  // namespace

}  // namespace kotatsu::sys::detail

#endif
