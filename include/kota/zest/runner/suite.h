#pragma once

#include "kota/zest/runner/registry.h"
#include "kota/zest/snapshot/snapshot.h"
#include "kota/meta/name.h"

namespace kota::zest {

/// Strip the "test_" prefix from a test case name, if present.
constexpr std::string_view strip_test_prefix(std::string_view name) {
    if(name.starts_with("test_")) {
        name.remove_prefix(5);
    }
    return name;
}

/// Merge suite-level and case-level test attributes.
/// Case-level flags override suite defaults when explicitly set to true.
constexpr TestAttrs merge_attrs(TestAttrs suite, TestAttrs test_case) {
    return {
        .skip = suite.skip || test_case.skip,
        .focus = suite.focus || test_case.focus,
        .serial = suite.serial || test_case.serial,
        .crashes = suite.crashes || test_case.crashes,
    };
}

/// Callback handed to ZEST_CASE_GROUP bodies: each invocation registers one
/// dynamically named test case inside the enclosing suite.
using CaseRegistrar = std::function<void(std::string, std::function<void()>)>;

/// The base ZEST_SUITE gives a suite. Each ZEST_CASE runs on an instance of its
/// own: the suite's constructor sets up for the case and its destructor tears
/// down after it, a throw included. A failed ZASSERT skips the destructor;
/// what must be cleaned up then goes in a FatalHook member.
template <typename Derived>
struct TestSuiteDef {
    using Self = Derived;

    constexpr static auto _suite_name() {
        auto name = meta::type_name<Derived>();
        if(name.ends_with("TEST")) {
            name = name.drop_back(4);
        }
        return name;
    }

    constexpr inline static auto& test_cases() {
        static std::vector<TestCase> instance;
        return instance;
    }

    constexpr inline static auto suites() {
        return std::move(test_cases());
    }

    template <typename T = void>
    inline static bool _register_suites = [] {
        auto sn = _suite_name();
        Runner::instance().add_suite(std::string_view(sn.data(), sn.size()), &suites);
        return true;
    }();

    template <TestAttrs attrs>
    static consteval TestAttrs effective_attrs() {
        if constexpr(requires { Derived::suite_attrs; }) {
            return merge_attrs(Derived::suite_attrs, attrs);
        } else {
            return attrs;
        }
    }

    template <auto test_body, const char* path, std::size_t line, TestAttrs attrs = {}>
    inline static bool _register_test_case = [] {
        constexpr auto case_name_ref = meta::member_name<test_body>();

        auto run_test = +[] {
            constexpr auto sn = _suite_name();
            constexpr auto cn = meta::member_name<test_body>();
            auto cn_sv = strip_test_prefix(std::string_view(cn.data(), cn.size()));
            detail::reset_snapshot_context(std::string_view(sn.data(), sn.size()), cn_sv);
            Derived test;
            (test.*test_body)();
        };

        auto cn = strip_test_prefix(std::string_view(case_name_ref.data(), case_name_ref.size()));
        test_cases().emplace_back(std::string(cn), path, line, effective_attrs<attrs>(), run_test);
        return true;
    }();

    template <auto group_body, const char* path, std::size_t line, TestAttrs attrs = {}>
    inline static bool _register_case_group = [] {
        CaseRegistrar registrar = [](std::string name, std::function<void()> body) {
            auto run = [name, body = std::move(body)] {
                constexpr auto sn = _suite_name();
                detail::reset_snapshot_context(std::string_view(sn.data(), sn.size()), name);
                body();
            };
            test_cases().emplace_back(std::move(name),
                                      path,
                                      line,
                                      effective_attrs<attrs>(),
                                      std::move(run));
        };
        group_body(registrar);
        return true;
    }();
};

namespace detail {

/// Keeps a suite and its cases registered, from the initializer of the
/// variable ZEST_SUITE defines before the suite.
///
/// They register from the initializers of TestSuiteDef's variables, which
/// nothing reads. C++ lets an implementation leave such a variable
/// uninitialized, and a linker that collects garbage drops it with its
/// initializer, as each is a COMDAT of its own. On Windows that loses tests:
/// MSVC's linker drops those of suites in an anonymous namespace under
/// /OPT:REF, the default of a link without /DEBUG, and lld drops those of the
/// other suites from clang's MinGW objects under --gc-sections, built with
/// -fdata-sections or LTO. No attribute stops either: clang turns
/// [[gnu::used]] into a directive to the linker only for MSVC targets, and cl
/// has nothing alike.
///
/// ZEST_SUITE's variable is neither inline nor a template's, so it has no
/// COMDAT of its own: its initializer runs from the start-up code of the
/// translation unit, which no linker drops. That initializer calls this.
///
/// This stores the address of the suite's registration in a volatile
/// variable, which the compiler must assume is read. It reaches the cases'
/// registrations through the suite's vtable: in a branch the compiler cannot
/// prove dead, it stores a suite made with new into the same variable, so the
/// compiler keeps the constructor's store of the vtable pointer, and with it
/// the vtable. A suite constructed into a local instead has no effect anyone
/// sees, and at -O2 compilers drop it with the vtable. The vtable holds every
/// case's hook (ZEST_CASE and ZEST_CASE_GROUP make it virtual), and each hook
/// returns the address of its registration. A linker keeps what a kept section
/// refers to, so every registration stays, and with it its initializer. The
/// variable is read back at the end, as clang reports one set but never read.
///
/// ZEST_SUITE's expansion ends at the suite's class head, so the variable
/// comes before the suite's definition and this is called where the suite is
/// incomplete. The standard lets this be instantiated right there, where it
/// would not compile, which makes the program ill-formed, no diagnostic
/// required; GCC, Clang and MSVC instantiate it at the end of the translation
/// unit, where the suite is complete.
template <typename Suite>
bool keep_registered() {
    const static void* volatile kept = nullptr;
    kept = &Suite::template _register_suites<>;
    volatile bool never = false;
    if(never) {
        kept = new Suite;
    }
    return kept != nullptr;
}

}  // namespace detail

}  // namespace kota::zest
