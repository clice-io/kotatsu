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

#define ZEST_SUITE(name, ...)                                                                      \
    struct name##TEST : __VA_OPT__(__VA_ARGS__, )::kota::zest::TestSuiteDef<name##TEST>

// clang-format off
#define ZEST_MAKE_ATTRS(...)                                                                       \
    [] constexpr {                                                                                 \
        ::kota::zest::TestAttrs _a{};                                                          \
        [[maybe_unused]] auto& [skip, focus, serial] = _a;                                         \
        __VA_ARGS__;                                                                               \
        return _a;                                                                                 \
    }()
// clang-format on

#define ZEST_SUITE_ATTRS(...)                                                                      \
    constexpr static ::kota::zest::TestAttrs suite_attrs = ZEST_MAKE_ATTRS(__VA_ARGS__)

#define ZEST_CASE(name, ...)                                                                       \
    inline static constexpr char _zest_file_##name[] = __FILE__;                                   \
    void _register_##name() {                                                                      \
        (void)_register_suites<>;                                                                  \
        constexpr auto _zest_attrs_ = ZEST_MAKE_ATTRS(__VA_OPT__(__VA_ARGS__));                    \
        (void)_register_test_case<&Self::test_##name, _zest_file_##name, __LINE__, _zest_attrs_>;  \
    }                                                                                              \
    void test_##name()

// Registers a group of dynamically named test cases. The body receives
// `const ::kota::zest::CaseRegistrar& add_case` and is invoked once at static
// init; call `add_case(name, body)` for each case to register. Unlike
// ZEST_CASE, the registered bodies run without a suite instance, so
// setup()/teardown() do not apply to them.
#define ZEST_CASE_GROUP(name, ...)                                                                 \
    inline static constexpr char _zest_file_##name[] = __FILE__;                                   \
    void _register_##name() {                                                                      \
        (void)_register_suites<>;                                                                  \
        constexpr auto _zest_attrs_ = ZEST_MAKE_ATTRS(__VA_OPT__(__VA_ARGS__));                    \
        (void)                                                                                     \
            _register_case_group<&Self::group_##name, _zest_file_##name, __LINE__, _zest_attrs_>;  \
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
#define ZEST_CHECK(return_action, ...)                                                             \
    do {                                                                                           \
        ZEST_SPLIT_BEGIN                                                                           \
        if(!::kota::zest::check(::kota::zest::Decomposer{} << __VA_ARGS__, #__VA_ARGS__))          \
            [[unlikely]] {                                                                         \
            return_action;                                                                         \
        }                                                                                          \
        ZEST_SPLIT_END                                                                             \
    } while(0)

#define EXPECT(...) ZEST_CHECK((void)0, __VA_ARGS__)
#define ASSERT(...) ZEST_CHECK(return, __VA_ARGS__)
#define CO_ASSERT(...) ZEST_CHECK(co_return, __VA_ARGS__)

// Evaluates the check at compile time and reports it at run time, so one wrong
// constant fails its test instead of the build.
#define STATIC_EXPECT(...)                                                                         \
    do {                                                                                           \
        ZEST_SPLIT_BEGIN                                                                           \
        constexpr bool _zest_held =                                                                \
            ::kota::zest::holds(::kota::zest::Decomposer{} << __VA_ARGS__);                        \
        if(!_zest_held) {                                                                          \
            ::kota::zest::check(::kota::zest::Decomposer{} << __VA_ARGS__, #__VA_ARGS__);          \
        }                                                                                          \
        ZEST_SPLIT_END                                                                             \
    } while(0)

// Adds a line to the report of every check that fails while it is in scope,
// e.g. `ZEST_CONTEXT("called from {}:{}", loc.file_name(), loc.line())` in a
// helper. Takes std::format arguments.
#define ZEST_CONTEXT(...)                                                                          \
    ::kota::zest::Context ZEST_CONCAT(_zest_context_, __LINE__) {                                  \
        std::format(__VA_ARGS__)                                                                   \
    }

// clang-format off
#define ZEST_SNAPSHOT_STR_IMPL(return_action, value, ...)                                          \
    do {                                                                                           \
        if(::kota::zest::check_snapshot(value __VA_OPT__(, __VA_ARGS__))) [[unlikely]] {           \
            ::kota::zest::print_trace(std::source_location::current());                            \
            ::kota::zest::failure();                                                                \
            return_action;                                                                          \
        }                                                                                           \
    } while(0)

#define EXPECT_SNAPSHOT(value, ...) ZEST_SNAPSHOT_STR_IMPL((void)0, value __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_SNAPSHOT(value, ...) ZEST_SNAPSHOT_STR_IMPL(return, value __VA_OPT__(,) __VA_ARGS__)
#define CO_ASSERT_SNAPSHOT(value, ...) ZEST_SNAPSHOT_STR_IMPL(co_return, value __VA_OPT__(,) __VA_ARGS__)

#define ZEST_SNAPSHOT_GLOB_IMPL(return_action, base_dir, pattern, transform)                        \
    do {                                                                                           \
        if(::kota::zest::check_snapshot_glob(base_dir, pattern, transform)) [[unlikely]] {         \
            ::kota::zest::print_trace(std::source_location::current());                            \
            ::kota::zest::failure();                                                                \
            return_action;                                                                          \
        }                                                                                           \
    } while(0)

#define EXPECT_SNAPSHOT_GLOB(base_dir, pattern, transform) ZEST_SNAPSHOT_GLOB_IMPL((void)0, base_dir, pattern, transform)
#define ASSERT_SNAPSHOT_GLOB(base_dir, pattern, transform) ZEST_SNAPSHOT_GLOB_IMPL(return, base_dir, pattern, transform)
#define CO_ASSERT_SNAPSHOT_GLOB(base_dir, pattern, transform) ZEST_SNAPSHOT_GLOB_IMPL(co_return, base_dir, pattern, transform)
// clang-format on

#ifdef __cpp_exceptions

#define EXPECT_THROWS(...) ::kota::zest::check_throws([&] { (__VA_ARGS__); }, #__VA_ARGS__, true)
#define EXPECT_NOTHROWS(...) ::kota::zest::check_throws([&] { (__VA_ARGS__); }, #__VA_ARGS__, false)

#endif

// Defined unconditionally, like every other macro here: these name
// ::kota::codec::json, which — as always — only has to be visible where the
// macro is used. Reaching for the JSON snapshot macros without the JSON codec
// available is a name-lookup error at that use site, which is the honest
// failure. (This used to be gated on __has_include of kotatsu's own json.h,
// which answered the wrong question: that file is on disk whether or not the
// codec was built, so the gate opened even when the backend was unavailable.)

// clang-format off
#define ZEST_SNAPSHOT_JSON_IMPL(return_action, value, ...)                                         \
    do {                                                                                           \
        auto _zest_snap_json = ::kota::codec::json::to_string(value);                              \
        if(!_zest_snap_json.has_value()) {                                                         \
            std::println("[snapshot] json serialization failed");                                   \
            ::kota::zest::print_trace(std::source_location::current());                            \
            ::kota::zest::failure();                                                                \
            return_action;                                                                          \
        } else {                                                                                    \
            auto _zest_snap_pretty = ::kota::codec::json::prettify(*_zest_snap_json);              \
            if(!_zest_snap_pretty.has_value()) {                                                   \
                std::println("[snapshot] json prettify failed");                                    \
                ::kota::zest::print_trace(std::source_location::current());                        \
                ::kota::zest::failure();                                                            \
                return_action;                                                                      \
            } else if(::kota::zest::check_snapshot_expr(                                            \
                          *_zest_snap_pretty, #value __VA_OPT__(, __VA_ARGS__))) [[unlikely]] {     \
                ::kota::zest::print_trace(std::source_location::current());                        \
                ::kota::zest::failure();                                                            \
                return_action;                                                                      \
            }                                                                                       \
        }                                                                                           \
    } while(0)

#define EXPECT_SNAPSHOT_JSON(value, ...) ZEST_SNAPSHOT_JSON_IMPL((void)0, value __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_SNAPSHOT_JSON(value, ...) ZEST_SNAPSHOT_JSON_IMPL(return, value __VA_OPT__(,) __VA_ARGS__)
#define CO_ASSERT_SNAPSHOT_JSON(value, ...) ZEST_SNAPSHOT_JSON_IMPL(co_return, value __VA_OPT__(,) __VA_ARGS__)
// clang-format on
