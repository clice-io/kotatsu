#include <algorithm>
#include <string>
#include <system_error>

#include "sys/harness/env.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/sys/env.h"

namespace kota::sys {

namespace {

constexpr std::string_view name = "KOTA_SYS_TEST_VARIABLE";

ZEST_SUITE(sys_env_variables) {

ZEST_CASE(set_env_is_read_back) {
    test::ScopedEnv variable(std::string(name), "value");
    ZEXPECT(get_env(name) == "value");
    ZEXPECT(!set_env(name, "caf\xC3\xA9 \xF0\x9F\x98\x80"));
    ZEXPECT(get_env(name) == "caf\xC3\xA9 \xF0\x9F\x98\x80");
}

ZEST_CASE(set_env_to_empty_keeps_the_variable) {
    test::ScopedEnv variable(std::string(name), "");
    ZEXPECT(get_env(name) == "");
}

ZEST_CASE(unset_env_removes_the_variable) {
    test::ScopedEnv variable(std::string(name), "value");
    ZEXPECT(!unset_env(name));
    ZEXPECT(!get_env(name).has_value());
    // There is none any more.
    ZEXPECT(!unset_env(name));
}

ZEST_CASE(set_env_of_an_invalid_name_fails) {
    ZEXPECT(set_env("", "x") == std::errc::invalid_argument);
    ZEXPECT(set_env("A=B", "x") == std::errc::invalid_argument);
    ZEXPECT(set_env(std::string_view("A\0B", 3), "x") == std::errc::invalid_argument);
#ifndef _WIN32
    // Only Windows names start with '='.
    ZEXPECT(set_env("=A", "x") == std::errc::invalid_argument);
#endif
}

ZEST_CASE(set_env_of_a_value_holding_nul_fails) {
    ZEXPECT(set_env(name, std::string_view("a\0b", 3)) == std::errc::invalid_argument);
}

ZEST_CASE(unset_env_of_an_invalid_name_fails) {
    ZEXPECT(unset_env("A=B") == std::errc::invalid_argument);
}

ZEST_CASE(get_env_of_an_invalid_name_is_unset) {
    test::ScopedEnv variable(std::string(name), "value");
    ZEXPECT(!get_env("A=B").has_value());
    // Not the variable the name before the NUL names.
    ZEXPECT(!get_env(std::string(name) + std::string(1, '\0')).has_value());
}

ZEST_CASE(get_env_reads_a_long_value) {
    std::string value(5000, 'v');
    test::ScopedEnv variable(std::string(name), value);
    ZEXPECT(get_env(name) == value);
}

ZEST_CASE(environment_lists_every_variable) {
    test::ScopedEnv variable(std::string(name), "listed");
    auto entries = environment();
    ZEXPECT(zest::contains(entries, std::string(name) + "=listed"));
}

#ifdef _WIN32
ZEST_CASE(get_env_ignores_case_on_windows) {
    test::ScopedEnv variable(std::string(name), "value");
    ZEXPECT(get_env("kota_sys_test_variable") == "value");
}

// What cmd keeps the current directory of each drive in.
ZEST_CASE(set_env_takes_a_drive_variable_on_windows) {
    test::ScopedEnv variable("=Q:", R"(Q:\dir)");
    ZEXPECT(get_env("=Q:") == R"(Q:\dir)");
    ZEXPECT(zest::contains(environment(), std::string(R"(=Q:=Q:\dir)")));
}

// Windows keeps UTF-16, which text that is no UTF-8 has no spelling in.
ZEST_CASE(set_env_of_ill_formed_utf8_fails_on_windows) {
    ZEXPECT(set_env(name, "\xFF") == std::errc::illegal_byte_sequence);
}
#endif

};  // ZEST_SUITE(sys_env_variables)

}  // namespace

}  // namespace kota::sys
