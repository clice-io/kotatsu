#pragma once

// Compiler/workaround feature macros shared by tests and runtime headers.

#if defined(_MSC_VER) && !defined(__clang__)
#define EVENTIDE_COMPILER_MSVC 1
#define EVENTIDE_COMPILER_MSVC_VERSION _MSC_VER
#else
#define EVENTIDE_COMPILER_MSVC 0
#define EVENTIDE_COMPILER_MSVC_VERSION 0
#endif

// Visual Studio issue:
// https://developercommunity.visualstudio.com/t/Unable-to-destroy-C20-coroutine-in-fin/10657377
//
// Reported fixed in VS 2026 toolset v145, still reproducible in v143.
// We treat _MSC_VER < 1950 as affected.
#if EVENTIDE_COMPILER_MSVC && (EVENTIDE_COMPILER_MSVC_VERSION < 1950) &&                           \
    (defined(_CRT_USE_ADDRESS_SANITIZER) || defined(__SANITIZE_ADDRESS__))
#define EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF 1
#else
#define EVENTIDE_WORKAROUND_MSVC_COROUTINE_ASAN_UAF 0
#endif

#if defined(EVENTIDE_ENABLE_EXCEPTIONS)
#if EVENTIDE_ENABLE_EXCEPTIONS && !defined(__cpp_exceptions)
#undef EVENTIDE_ENABLE_EXCEPTIONS
#define EVENTIDE_ENABLE_EXCEPTIONS 0
#endif
#elif defined(__cpp_exceptions)
#define EVENTIDE_ENABLE_EXCEPTIONS 1
#else
#define EVENTIDE_ENABLE_EXCEPTIONS 0
#endif
