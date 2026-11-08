#pragma once

// The address a signal interrupted, as the context its handler gets says.

#ifndef _WIN32

#include <cstdint>

#ifdef __APPLE__
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif

namespace kotatsu::test {

/// The pc of `context`, the ucontext_t a SA_SIGINFO handler gets.
inline std::uintptr_t interrupted_at(const void* context) {
    const auto* interrupted = static_cast<const ucontext_t*>(context);
#if defined(__APPLE__) && defined(__aarch64__)
    return __darwin_arm_thread_state64_get_pc(interrupted->uc_mcontext->__ss);
#elif defined(__APPLE__)
    return interrupted->uc_mcontext->__ss.__rip;
#elif defined(__aarch64__)
    return interrupted->uc_mcontext.pc;
#else
    return static_cast<std::uintptr_t>(interrupted->uc_mcontext.gregs[REG_RIP]);
#endif
}

}  // namespace kotatsu::test

#endif
