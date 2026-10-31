#pragma once

#include <cstdlib>

// Compiler/workaround feature macros shared by tests and runtime headers.

#if defined(_MSC_VER) && !defined(__clang__)
#define KOTATSU_COMPILER_MSVC 1
#define KOTATSU_COMPILER_MSVC_VERSION _MSC_VER
#else
#define KOTATSU_COMPILER_MSVC 0
#define KOTATSU_COMPILER_MSVC_VERSION 0
#endif

#if KOTATSU_COMPILER_MSVC
#define KOTATSU_NOINLINE __declspec(noinline)
#else
#define KOTATSU_NOINLINE [[gnu::noinline]]
#endif

// Visual Studio issue:
// https://developercommunity.visualstudio.com/t/Unable-to-destroy-C20-coroutine-in-fin/10657377
//
// Reported fixed in VS 2026 toolset v145, still reproducible in v143.
// We treat _MSC_VER < 1950 as affected.
#if KOTATSU_COMPILER_MSVC && (KOTATSU_COMPILER_MSVC_VERSION < 1950) &&                             \
    (defined(_CRT_USE_ADDRESS_SANITIZER) || defined(__SANITIZE_ADDRESS__))
#define KOTATSU_WORKAROUND_MSVC_COROUTINE_ASAN_UAF 1
#else
#define KOTATSU_WORKAROUND_MSVC_COROUTINE_ASAN_UAF 0
#endif

// [[msvc::no_unique_address]] corrupts coroutine frame layout under MSVC ASAN.
// Even without ASAN, MSVC miscompiles the layout of classes that use this
// attribute when those classes are stored inside a coroutine frame (local
// variables, not just promise types). Only safe on types that never live in
// a coroutine frame — e.g. outcome<>, which is embedded in the promise itself.
// See: https://developercommunity.visualstudio.com/t/msvc::no_unique_address-nonconforman/10504173
//      https://developercommunity.visualstudio.com/t/c20-coroutine-memory-corruption/1683791
#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(msvc::no_unique_address) && !KOTATSU_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
#define KOTATSU_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#elif __has_cpp_attribute(no_unique_address)
#define KOTATSU_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#define KOTATSU_NO_UNIQUE_ADDRESS
#endif
#else
#define KOTATSU_NO_UNIQUE_ADDRESS
#endif

#if defined(KOTATSU_ENABLE_EXCEPTIONS)
#if KOTATSU_ENABLE_EXCEPTIONS && !defined(__cpp_exceptions)
#undef KOTATSU_ENABLE_EXCEPTIONS
#define KOTATSU_ENABLE_EXCEPTIONS 0
#endif
#elif defined(__cpp_exceptions)
#define KOTATSU_ENABLE_EXCEPTIONS 1
#else
#define KOTATSU_ENABLE_EXCEPTIONS 0
#endif

#if KOTATSU_ENABLE_EXCEPTIONS
#define KOTATSU_THROW(exception_expr) throw exception_expr
#define KOTATSU_TRY try
#define KOTATSU_CATCH_ALL() catch(...)
#define KOTATSU_RETHROW() throw
#else
#define KOTATSU_THROW(exception_expr)                                                              \
    do {                                                                                           \
        static_cast<void>(sizeof(exception_expr));                                                 \
        std::abort();                                                                              \
    } while(false)
#define KOTATSU_TRY if(true)
#define KOTATSU_CATCH_ALL() else
#define KOTATSU_RETHROW() std::abort()
#endif
