#pragma once

#include <cstdlib>

// Compiler/workaround feature macros shared by tests and runtime headers.

#if defined(_MSC_VER) && !defined(__clang__)
#define KOTA_COMPILER_MSVC 1
#define KOTA_COMPILER_MSVC_VERSION _MSC_VER
#else
#define KOTA_COMPILER_MSVC 0
#define KOTA_COMPILER_MSVC_VERSION 0
#endif

// Visual Studio issue:
// https://developercommunity.visualstudio.com/t/Unable-to-destroy-C20-coroutine-in-fin/10657377
//
// Reported fixed in VS 2026 toolset v145, still reproducible in v143.
// We treat _MSC_VER < 1950 as affected.
#if KOTA_COMPILER_MSVC && (KOTA_COMPILER_MSVC_VERSION < 1950) &&                                   \
    (defined(_CRT_USE_ADDRESS_SANITIZER) || defined(__SANITIZE_ADDRESS__))
#define KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF 1
#else
#define KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF 0
#endif

// [[msvc::no_unique_address]] corrupts coroutine frame layout under MSVC ASAN.
// Even without ASAN, MSVC miscompiles the layout of classes that use this
// attribute when those classes are stored inside a coroutine frame (local
// variables, not just promise types). Only safe on types that never live in
// a coroutine frame — e.g. outcome<>, which is embedded in the promise itself.
// See: https://developercommunity.visualstudio.com/t/msvc::no_unique_address-nonconforman/10504173
//      https://developercommunity.visualstudio.com/t/c20-coroutine-memory-corruption/1683791
#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(msvc::no_unique_address) && !KOTA_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
#define KOTA_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#elif __has_cpp_attribute(no_unique_address)
#define KOTA_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#define KOTA_NO_UNIQUE_ADDRESS
#endif
#else
#define KOTA_NO_UNIQUE_ADDRESS
#endif

// Windows ASAN (both MSVC and clang-cl) corrupts exception objects caught
// inside coroutine frames, making e.what() crash.
#if defined(_WIN32) && (defined(__SANITIZE_ADDRESS__) || defined(_CRT_USE_ADDRESS_SANITIZER))
#define KOTA_WORKAROUND_WINDOWS_ASAN_COROUTINE_EXCEPTION 1
#elif defined(_WIN32) && defined(__has_feature)
#if __has_feature(address_sanitizer)
#define KOTA_WORKAROUND_WINDOWS_ASAN_COROUTINE_EXCEPTION 1
#endif
#endif
#ifndef KOTA_WORKAROUND_WINDOWS_ASAN_COROUTINE_EXCEPTION
#define KOTA_WORKAROUND_WINDOWS_ASAN_COROUTINE_EXCEPTION 0
#endif

#if defined(KOTA_ENABLE_EXCEPTIONS)
#if KOTA_ENABLE_EXCEPTIONS && !defined(__cpp_exceptions)
#undef KOTA_ENABLE_EXCEPTIONS
#define KOTA_ENABLE_EXCEPTIONS 0
#endif
#elif defined(__cpp_exceptions)
#define KOTA_ENABLE_EXCEPTIONS 1
#else
#define KOTA_ENABLE_EXCEPTIONS 0
#endif

#if KOTA_ENABLE_EXCEPTIONS
#define KOTA_THROW(exception_expr) throw exception_expr
#define KOTA_TRY try
#define KOTA_CATCH_ALL() catch(...)
#define KOTA_RETHROW() throw
#else
#define KOTA_THROW(exception_expr)                                                                 \
    do {                                                                                           \
        static_cast<void>(sizeof(exception_expr));                                                 \
        std::abort();                                                                              \
    } while(false)
#define KOTA_TRY if(true)
#define KOTA_CATCH_ALL() else
#define KOTA_RETHROW() std::abort()
#endif

#if defined(NDEBUG)
#if defined(_MSC_VER)
#define KOTA_ALWAYS_INLINE [[msvc::forceinline]]
#elif defined(__GNUC__) || defined(__clang__)
#define KOTA_ALWAYS_INLINE [[gnu::always_inline]]
#else
#define KOTA_ALWAYS_INLINE
#endif
#else
#define KOTA_ALWAYS_INLINE
#endif
