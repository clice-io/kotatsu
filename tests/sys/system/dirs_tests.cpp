#include <expected>
#include <filesystem>
#include <string>
#include <system_error>

#include "sys/harness/env.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/fs/path.h"
#include "kota/sys/dirs.h"

namespace kota::sys {

namespace {

#ifdef _WIN32
constexpr const char* home_variable = "USERPROFILE";
constexpr const char* home = R"(C:\Users\kota)";
#else
constexpr const char* home_variable = "HOME";
constexpr const char* home = "/home/kota";
#endif

/// The directory `dir` writes, or its error.
std::expected<std::string, std::error_code> dir_of(std::error_code (*dir)(buffer_ref)) {
    std::string out;
    if(auto error = dir(out)) {
        return std::unexpected(error);
    }
    return out;
}

ZEST_SUITE(sys_dirs) {

ZEST_CASE(home_dir_is_the_variable) {
    test::ScopedEnv variable(home_variable, home);
    ZEXPECT(dir_of(home_dir) == home);
}

ZEST_CASE(home_dir_without_the_variable_is_the_users) {
    test::ScopedEnv variable(home_variable, "");
    auto found = dir_of(home_dir);
    // A container may run as a user the password database does not list.
    if(!found && found.error() == std::errc::no_such_file_or_directory) {
        zest::skip();
        return;
    }
    ZASSERT(found.has_value());
    ZEXPECT(fs::path::is_absolute(*found));
}

ZEST_CASE(temp_dir_is_a_directory) {
    auto found = dir_of(temp_dir);
    ZASSERT(found.has_value());
    ZEXPECT(std::filesystem::is_directory(*found));
    ZEXPECT(fs::path::is_absolute(*found));
    ZEXPECT(!fs::path::is_separator(found->back()));
}

#ifndef _WIN32
ZEST_CASE(temp_dir_takes_the_first_variable_set) {
    test::ScopedEnv tmpdir("TMPDIR", "");
    test::ScopedEnv tmp("TMP", "/first/");
    test::ScopedEnv temp("TEMP", "/second");
    ZEXPECT(dir_of(temp_dir) == "/first");
}

ZEST_CASE(temp_dir_without_a_variable_is_tmp) {
    test::ScopedEnv tmpdir("TMPDIR", std::nullopt);
    test::ScopedEnv tmp("TMP", std::nullopt);
    test::ScopedEnv temp("TEMP", std::nullopt);
    test::ScopedEnv tempdir("TEMPDIR", std::nullopt);
    ZEXPECT(dir_of(temp_dir) == "/tmp");
}
#endif

#if defined(__APPLE__)
ZEST_CASE(user_dirs_are_in_library) {
    test::ScopedEnv variable(home_variable, home);
    ZEXPECT(dir_of(cache_dir) == "/home/kota/Library/Caches");
    ZEXPECT(dir_of(config_dir) == "/home/kota/Library/Application Support");
    ZEXPECT(dir_of(data_dir) == "/home/kota/Library/Application Support");
}
#elif !defined(_WIN32)
ZEST_CASE(user_dirs_default_to_the_home_directory) {
    test::ScopedEnv variable(home_variable, home);
    test::ScopedEnv cache("XDG_CACHE_HOME", std::nullopt);
    test::ScopedEnv config("XDG_CONFIG_HOME", std::nullopt);
    test::ScopedEnv data("XDG_DATA_HOME", std::nullopt);
    ZEXPECT(dir_of(cache_dir) == "/home/kota/.cache");
    ZEXPECT(dir_of(config_dir) == "/home/kota/.config");
    ZEXPECT(dir_of(data_dir) == "/home/kota/.local/share");
}

ZEST_CASE(user_dirs_take_absolute_xdg_variables) {
    test::ScopedEnv variable(home_variable, home);
    test::ScopedEnv cache("XDG_CACHE_HOME", "/xdg/cache");
    // A relative one is ignored, as the XDG specification says.
    test::ScopedEnv config("XDG_CONFIG_HOME", "relative");
    ZEXPECT(dir_of(cache_dir) == "/xdg/cache");
    ZEXPECT(dir_of(config_dir) == "/home/kota/.config");
}
#else
ZEST_CASE(user_dirs_are_known_folders) {
    auto cache = dir_of(cache_dir);
    auto config = dir_of(config_dir);
    auto data = dir_of(data_dir);
    ZASSERT(cache.has_value());
    ZASSERT(config.has_value());
    ZASSERT(data.has_value());
    ZEXPECT(std::filesystem::is_directory(*cache));
    ZEXPECT(fs::path::filename(*cache) == "Local");
    ZEXPECT(fs::path::filename(*config) == "Roaming");
    ZEXPECT(*data == *cache);
}
#endif

};  // ZEST_SUITE(sys_dirs)

}  // namespace

}  // namespace kota::sys
