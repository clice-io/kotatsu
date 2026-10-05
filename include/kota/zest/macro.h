#pragma once

// The zest test macros, and nothing else — this header includes nothing, by
// design. Modules cannot export macros, so a downstream consuming kotatsu as a
// module still has to pick these up textually, and anything included here would
// duplicate declarations the module already provides.
//
// The kota::zest entities an expansion refers to only have to be visible where
// the macro is used, not here. Include "kota/zest/zest.h" or import the module.

#define ZEST_CONCAT_IMPL(a, b) a##b
#define ZEST_CONCAT(a, b) ZEST_CONCAT_IMPL(a, b)

// A suite groups cases and the helpers they share, not state: each case runs
// on a fresh instance of the suite, so its members start as constructed and
// need no clearing. Cases run in no set order, in several processes at once,
// and none sees what another left; but one process runs several cases in a
// row, so a case that changes what the process shares (a global, the
// environment, the current directory) puts it back, as an RAII guard does.
//
// The variable before the suite keeps the suite and its cases registered
// whatever the linker drops, so a suite may live in any namespace and be built
// with any options (kota::zest::detail::keep_registered says how).
#define ZEST_SUITE(name, ...)                                                                      \
    struct name##TEST;                                                                             \
    [[maybe_unused]] static const bool _zest_registered_##name =                                   \
        ::kota::zest::detail::keep_registered<name##TEST>();                                       \
    struct name##TEST : __VA_OPT__(__VA_ARGS__, )::kota::zest::TestSuiteDef<name##TEST>

// clang-format off
#define ZEST_MAKE_ATTRS(...)                                                                       \
    [] constexpr {                                                                                 \
        ::kota::zest::TestAttrs _a{};                                                          \
        [[maybe_unused]] auto& [skip, focus, serial, crashes] = _a;                                \
        __VA_ARGS__;                                                                               \
        return _a;                                                                                 \
    }()
// clang-format on

#define ZEST_SUITE_ATTRS(...)                                                                      \
    constexpr static ::kota::zest::TestAttrs suite_attrs = ZEST_MAKE_ATTRS(__VA_ARGS__)

// A case registers from the initializer of TestSuiteDef's _register_test_case
// specialization that its hook names. The hook is virtual so that the suite's
// vtable, which ZEST_SUITE's variable keeps, refers to it, and it returns the
// specialization's address so that the reference survives optimization.
#define ZEST_CASE(name, ...)                                                                       \
    inline static constexpr char _zest_file_##name[] = __FILE__;                                   \
    virtual const void* _register_##name() {                                                       \
        constexpr auto _zest_attrs_ = ZEST_MAKE_ATTRS(__VA_OPT__(__VA_ARGS__));                    \
        return &_register_test_case<&Self::test_##name,                                            \
                                    _zest_file_##name,                                             \
                                    __LINE__,                                                      \
                                    _zest_attrs_>;                                                 \
    }                                                                                              \
    void test_##name()

// Registers a group of dynamically named test cases. The body receives
// `const ::kota::zest::CaseRegistrar& add_case` and is invoked once at static
// init; call `add_case(name, body)` for each case to register. Unlike
// ZEST_CASE, the registered bodies run without a suite instance. Its hook
// keeps the group registered as a case's does.
#define ZEST_CASE_GROUP(name, ...)                                                                 \
    inline static constexpr char _zest_file_##name[] = __FILE__;                                   \
    virtual const void* _register_##name() {                                                       \
        constexpr auto _zest_attrs_ = ZEST_MAKE_ATTRS(__VA_OPT__(__VA_ARGS__));                    \
        return &_register_case_group<&Self::group_##name,                                          \
                                     _zest_file_##name,                                            \
                                     __LINE__,                                                     \
                                     _zest_attrs_>;                                                \
    }                                                                                              \
    static void group_##name(const ::kota::zest::CaseRegistrar& add_case)

// A check reads `Decomposer{} << a == b` as `(Decomposer{} << a) == b` on
// purpose, which is what these warnings are about.
#if defined(__clang__)
#define ZEST_SPLIT_BEGIN                                                                           \
    _Pragma("clang diagnostic push")                                                               \
        _Pragma("clang diagnostic ignored \"-Woverloaded-shift-op-parentheses\"")
#define ZEST_SPLIT_END _Pragma("clang diagnostic pop")
#elif defined(__GNUC__)
#define ZEST_SPLIT_BEGIN                                                                           \
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wparentheses\"")
#define ZEST_SPLIT_END _Pragma("GCC diagnostic pop")
#else
#define ZEST_SPLIT_BEGIN
#define ZEST_SPLIT_END
#endif

// A check takes one expression. `Decomposer{} << expr` captures the operands of
// a top-level comparison — `<<` binds tighter than any comparison and looser
// than arithmetic, so `a + 1 == b` splits into `a + 1` and `b` — and the whole
// check runs in one full-expression, so temporaries in the operands live until
// it is reported.
#define ZEST_CHECK(checker, ...)                                                                   \
    do {                                                                                           \
        ZEST_SPLIT_BEGIN                                                                           \
        ::kota::zest::detail::checker(::kota::zest::detail::Decomposer{} << __VA_ARGS__,           \
                                      #__VA_ARGS__);                                               \
        ZEST_SPLIT_END                                                                             \
    } while(0)

#define ZEXPECT(...) ZEST_CHECK(check, __VA_ARGS__)

// A failed ZASSERT does not return: it ends the process, in a coroutine, a
// helper or any thread alike, without unwinding the stack. Cleanup that must
// happen anyway goes in a ::kota::zest::FatalHook.
#define ZASSERT(...) ZEST_CHECK(check_fatal, __VA_ARGS__)

// Evaluates the check at compile time and reports it at run time, so one wrong
// constant fails its test instead of the build. To show the operands, the
// report evaluates them again, at run time.
#define ZSTATIC_EXPECT(...)                                                                        \
    do {                                                                                           \
        ZEST_SPLIT_BEGIN                                                                           \
        constexpr bool _zest_held = (::kota::zest::detail::Decomposer{} << __VA_ARGS__).holds();   \
        if(!_zest_held) {                                                                          \
            (::kota::zest::detail::Decomposer{} << __VA_ARGS__)                                    \
                .fail(#__VA_ARGS__, std::source_location::current());                              \
        }                                                                                          \
        ZEST_SPLIT_END                                                                             \
    } while(0)

// Adds a line to the report of every check that fails while it is in scope,
// e.g. `ZEST_CONTEXT("called from {}:{}", loc.file_name(), loc.line())` in a
// helper. Takes std::format arguments. Contexts belong to the thread that
// entered them, so one held across a co_await also shows in the checks of
// whatever runs meanwhile on that thread, and one that ends on another thread
// stays in that thread's reports.
#define ZEST_CONTEXT(...)                                                                          \
    ::kota::zest::Context ZEST_CONCAT(_zest_context_, __COUNTER__) {                               \
        __VA_ARGS__                                                                                \
    }
