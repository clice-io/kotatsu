#include "kotatsu/sys/thread.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "os.h"

#ifndef _WIN32
#include <climits>
#include <pthread.h>

#include "kotatsu/support/scope_exit.h"
#endif

namespace kotatsu::sys {

namespace {

#ifdef _WIN32

DWORD WINAPI enter(void* entry) {
    auto* start = static_cast<detail::ThreadEntry*>(entry);
    start->run(start);
    return 0;
}

HANDLE native(std::uintptr_t handle) {
    return reinterpret_cast<HANDLE>(handle);
}

#else

static_assert(sizeof(pthread_t) <= sizeof(std::uintptr_t));

void* enter(void* entry) {
    auto* start = static_cast<detail::ThreadEntry*>(entry);
    start->run(start);
    return nullptr;
}

pthread_t native(std::uintptr_t handle) {
    pthread_t thread{};
    std::memcpy(&thread, &handle, sizeof(thread));
    return thread;
}

std::uintptr_t held(pthread_t thread) {
    std::uintptr_t handle = 0;
    std::memcpy(&handle, &thread, sizeof(thread));
    return handle;
}

#endif

}  // namespace

Thread& Thread::operator=(Thread&& other) noexcept {
    if(handle != 0) {
        join();
    }
    handle = std::exchange(other.handle, 0);
    return *this;
}

Thread::~Thread() {
    if(handle != 0) {
        join();
    }
}

std::expected<Thread, std::error_code> Thread::launch(std::size_t stack_size,
                                                      detail::ThreadEntry* entry) {
    Thread thread;
#ifdef _WIN32
    // Windows rounds the reserve up to its allocation granularity itself.
    HANDLE created = ::CreateThread(nullptr,
                                    stack_size,
                                    enter,
                                    entry,
                                    STACK_SIZE_PARAM_IS_A_RESERVATION,
                                    nullptr);
    if(created == nullptr) {
        return std::unexpected(fs::windows::last_error());
    }
    thread.handle = reinterpret_cast<std::uintptr_t>(created);
#else
    pthread_attr_t attributes;
    ::pthread_attr_init(&attributes);
    ScopeExit destroy([&] { ::pthread_attr_destroy(&attributes); });
    // macOS takes whole pages alone. A size within a page of the largest
    // rounds to 0, which pthread_attr_setstacksize refuses.
    auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    auto size = std::max(stack_size, static_cast<std::size_t>(PTHREAD_STACK_MIN));
    size = (size + page - 1) / page * page;
    if(int error = ::pthread_attr_setstacksize(&attributes, size)) {
        return std::unexpected(std::error_code(error, std::generic_category()));
    }
    pthread_t created;
    if(int error = ::pthread_create(&created, &attributes, enter, entry)) {
        return std::unexpected(std::error_code(error, std::generic_category()));
    }
    thread.handle = held(created);
#endif
    return thread;
}

void Thread::join() {
    assert(handle != 0 && "a thread to join");
#ifdef _WIN32
    ::WaitForSingleObject(native(handle), INFINITE);
    ::CloseHandle(native(handle));
#else
    ::pthread_join(native(handle), nullptr);
#endif
    handle = 0;
}

void Thread::detach() {
    assert(handle != 0 && "a thread to detach");
#ifdef _WIN32
    ::CloseHandle(native(handle));
#else
    ::pthread_detach(native(handle));
#endif
    handle = 0;
}

}  // namespace kotatsu::sys
