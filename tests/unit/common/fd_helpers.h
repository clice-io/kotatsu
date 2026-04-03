#pragma once

#include <cstddef>
#include <string>

#ifdef _WIN32
#include <BaseTsd.h>
#include <fcntl.h>
#include <io.h>
using ssize_t = SSIZE_T;
#else
#include <unistd.h>
#endif

namespace eventide::test {

// IMPORTANT: Windows pipes have a tiny buffer (4KB). If you write more than
// that before the event loop starts reading, write_fd() will block and deadlock.
// Always write from a separate std::thread when the data may exceed ~4KB.

#ifdef _WIN32
inline int create_pipe(int fds[2]) {
    return _pipe(fds, 4096, _O_BINARY);
}

inline int close_fd(int fd) {
    return _close(fd);
}

inline ssize_t write_fd(int fd, const char* data, size_t len) {
    return _write(fd, data, static_cast<unsigned int>(len));
}
#else
inline int create_pipe(int fds[2]) {
    return ::pipe(fds);
}

inline int close_fd(int fd) {
    return ::close(fd);
}

inline ssize_t write_fd(int fd, const char* data, size_t len) {
    return ::write(fd, data, len);
}
#endif

}  // namespace eventide::test
