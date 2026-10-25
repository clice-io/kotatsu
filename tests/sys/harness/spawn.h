#pragma once

// Reading what a child writes to a pipe sys::spawn made, without an event
// loop: read_some() and read_to_end() on the end Child::take_pipe gives.

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

#include "kota/fs/file.h"

namespace kota::test {

/// What the next read of pipe end `end` gives; empty at its end.
inline std::string read_some(fs::File::native_handle_type end) {
    char buffer[4096];
#ifdef _WIN32
    // The end is opened for overlapped I/O, which a read without an
    // OVERLAPPED would get wrong.
    OVERLAPPED overlapped{};
    overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD got = 0;
    if(!::ReadFile(end, buffer, sizeof(buffer), nullptr, &overlapped) &&
       ::GetLastError() != ERROR_IO_PENDING) {
        ::CloseHandle(overlapped.hEvent);
        return {};
    }
    const bool read = ::GetOverlappedResult(end, &overlapped, &got, TRUE);
    ::CloseHandle(overlapped.hEvent);
    return read ? std::string(buffer, got) : std::string();
#else
    ssize_t got = 0;
    do {
        got = ::read(end, buffer, sizeof(buffer));
    } while(got < 0 && errno == EINTR);
    return got > 0 ? std::string(buffer, static_cast<std::size_t>(got)) : std::string();
#endif
}

/// All that pipe end `end` gives until every writer of the other has gone.
inline std::string read_to_end(fs::File::native_handle_type end) {
    std::string text;
    while(true) {
        auto chunk = read_some(end);
        if(chunk.empty()) {
            return text;
        }
        text += chunk;
    }
}

/// Closes pipe end `end`.
inline void close_end(fs::File::native_handle_type end) {
#ifdef _WIN32
    ::CloseHandle(end);
#else
    ::close(end);
#endif
}

}  // namespace kota::test
