#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "sys/harness/env.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/small_string.h"
#include "kota/sys/env.h"

namespace kota::sys {

namespace {

constexpr std::string_view name = "KOTA_SYS_TEST_VARIABLE";

/// The value of `variable`, if it has one.
std::optional<std::string> value_of(std::string_view variable) {
    std::string value;
    if(!get_env(value, variable)) {
        return std::nullopt;
    }
    return value;
}

ZEST_SUITE(sys_env_variables) {

ZEST_CASE(set_env_is_read_back) {
    test::ScopedEnv variable(std::string(name), "value");
    ZEXPECT(value_of(name) == "value");
    ZEXPECT(!set_env(name, "caf\xC3\xA9 \xF0\x9F\x98\x80"));
    ZEXPECT(value_of(name) == "caf\xC3\xA9 \xF0\x9F\x98\x80");
}

ZEST_CASE(set_env_to_empty_keeps_the_variable) {
    test::ScopedEnv variable(std::string(name), "");
    ZEXPECT(value_of(name) == "");
}

ZEST_CASE(unset_env_removes_the_variable) {
    test::ScopedEnv variable(std::string(name), "value");
    ZEXPECT(!unset_env(name));
    ZEXPECT(!value_of(name).has_value());
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
    ZEXPECT(!value_of("A=B").has_value());
    // Not the variable the name before the NUL names.
    ZEXPECT(!value_of(std::string(name) + std::string(1, '\0')).has_value());
}

ZEST_CASE(get_env_of_an_unset_variable_leaves_the_buffer) {
    test::ScopedEnv variable(std::string(name), std::nullopt);
    std::string value = "kept";
    ZEXPECT(!get_env(value, name));
    ZEXPECT(value == "kept");
}

// Into any buffer, over what it held.
ZEST_CASE(get_env_writes_over_what_the_buffer_held) {
    test::ScopedEnv variable(std::string(name), "value");
    small_string<8> value("some old text");
    ZEXPECT(get_env(value, name));
    ZEXPECT(std::string_view(value) == "value");
}

ZEST_CASE(get_env_reads_a_long_value) {
    std::string value(5000, 'v');
    test::ScopedEnv variable(std::string(name), value);
    ZEXPECT(value_of(name) == value);
}

ZEST_CASE(environment_lists_every_variable) {
    test::ScopedEnv variable(std::string(name), "listed");
    auto entries = environment();
    ZEXPECT(zest::contains(entries, EnvVar{.name = std::string(name), .value = "listed"}));
}

#ifdef _WIN32
ZEST_CASE(get_env_ignores_case_on_windows) {
    test::ScopedEnv variable(std::string(name), "value");
    ZEXPECT(value_of("kota_sys_test_variable") == "value");
}

// What cmd keeps the current directory of each drive in.
ZEST_CASE(set_env_takes_a_drive_variable_on_windows) {
    test::ScopedEnv variable("=Q:", R"(Q:\dir)");
    ZEXPECT(value_of("=Q:") == R"(Q:\dir)");
    ZEXPECT(zest::contains(environment(), EnvVar{.name = "=Q:", .value = R"(Q:\dir)"}));
}

// Windows keeps UTF-16, which text that is no UTF-8 has no spelling in.
ZEST_CASE(set_env_of_ill_formed_utf8_fails_on_windows) {
    ZEXPECT(set_env(name, "\xFF") == std::errc::illegal_byte_sequence);
}
#endif

};  // ZEST_SUITE(sys_env_variables)

}  // namespace

}  // namespace kota::sys
