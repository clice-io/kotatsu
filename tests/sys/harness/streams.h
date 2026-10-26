#pragma once

// ScopedClosedStream, one of this process's standard streams closed for a
// while, as a program may be started without it.

#ifndef _WIN32

#include <fcntl.h>
#include <unistd.h>

#include "kota/zest/zest.h"

namespace kota::test {

/// Standard stream `fd` of this process closed until this goes, which puts
/// it back.
struct ScopedClosedStream {
    explicit ScopedClosedStream(int fd) : fd(fd), saved(::fcntl(fd, F_DUPFD_CLOEXEC, 3)) {
        ZASSERT(saved >= 0);
        ::close(fd);
    }

    ScopedClosedStream(const ScopedClosedStream&) = delete;
    ScopedClosedStream& operator=(const ScopedClosedStream&) = delete;

    ~ScopedClosedStream() {
        ::dup2(saved, fd);
        ::close(saved);
    }

    int fd;
    int saved;
};

}  // namespace kota::test

#endif
