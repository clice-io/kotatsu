#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#endif

#include "sys/harness/env.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/fs/path.h"
#include "kotatsu/sys/dirs.h"

namespace kotatsu::sys {

namespace {

#ifdef _WIN32
constexpr const char* home_variable = "USERPROFILE";
constexpr const char* home = R"(C:\Users\kotatsu)";
constexpr const char* root = R"(C:\)";
#else
constexpr const char* home_variable = "HOME";
constexpr const char* home = "/home/kotatsu";
constexpr const char* root = "/";
#endif

/// The directory `dir` writes, or its error.
std::expected<std::string, std::error_code> dir_of(std::error_code (*dir)(buffer_ref)) {
    std::string out;
    if(auto error = dir(out)) {
        return std::unexpected(error);
    }
    return out;
}

/// `path` with its `~` expanded, or the error.
std::expected<std::string, std::error_code> expanded(std::string_view path) {
    std::string out;
    if(auto error = expand_tilde(out, path)) {
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

// The rest goes after the home as it is, a separator of either kind on
// Windows.
ZEST_CASE(expand_tilde_takes_the_home_directory) {
    test::ScopedEnv variable(home_variable, home);
    ZEXPECT(expanded("~") == home);
    ZEXPECT(expanded("~/a/b") == std::string(home) + "/a/b");
#ifdef _WIN32
    ZEXPECT(expanded(R"(~\a)") == R"(C:\Users\kotatsu\a)");
#endif
}

ZEST_CASE(expand_tilde_in_a_root_home_keeps_one_separator) {
    test::ScopedEnv variable(home_variable, root);
    ZEXPECT(expanded("~") == root);
#ifdef _WIN32
    ZEXPECT(expanded("~/a") == "C:/a");
#else
    ZEXPECT(expanded("~/a") == "/a");
#endif
}

ZEST_CASE(expand_tilde_leaves_a_path_without_one) {
    ZEXPECT(expanded("a/~") == "a/~");
    ZEXPECT(expanded("/~") == "/~");
    ZEXPECT(expanded("") == "");
}

#ifdef _WIN32
ZEST_CASE(expand_tilde_leaves_a_user_name) {
    ZEXPECT(expanded(R"(~kotatsu\a)") == R"(~kotatsu\a)");
}
#else
// The user's home from the password database, whatever HOME says.
ZEST_CASE(expand_tilde_takes_a_users_home) {
    test::ScopedEnv variable(home_variable, home);
    const passwd* entry = ::getpwuid(::getuid());
    // A container may run as a user the password database does not list.
    if(entry == nullptr) {
        zest::skip();
        return;
    }
    std::string name = entry->pw_name;
    std::string user_home = entry->pw_dir;
    ZEXPECT(expanded("~" + name) == user_home);
    std::string inside = user_home;
    fs::path::push(inside, "a");
    ZEXPECT(expanded("~" + name + "/a") == inside);
}

ZEST_CASE(expand_tilde_leaves_a_user_that_does_not_exist) {
    ZEXPECT(expanded("~kotatsu-no-such-user/a") == "~kotatsu-no-such-user/a");
    ZEXPECT(expanded("~kotatsu-no-such-user") == "~kotatsu-no-such-user");
}
#endif

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
    ZEXPECT(dir_of(cache_dir) == "/home/kotatsu/Library/Caches");
    ZEXPECT(dir_of(config_dir) == "/home/kotatsu/Library/Application Support");
    ZEXPECT(dir_of(data_dir) == "/home/kotatsu/Library/Application Support");
}
#elif !defined(_WIN32)
ZEST_CASE(user_dirs_default_to_the_home_directory) {
    test::ScopedEnv variable(home_variable, home);
    test::ScopedEnv cache("XDG_CACHE_HOME", std::nullopt);
    test::ScopedEnv config("XDG_CONFIG_HOME", std::nullopt);
    test::ScopedEnv data("XDG_DATA_HOME", std::nullopt);
    ZEXPECT(dir_of(cache_dir) == "/home/kotatsu/.cache");
    ZEXPECT(dir_of(config_dir) == "/home/kotatsu/.config");
    ZEXPECT(dir_of(data_dir) == "/home/kotatsu/.local/share");
}

ZEST_CASE(user_dirs_take_absolute_xdg_variables) {
    test::ScopedEnv variable(home_variable, home);
    test::ScopedEnv cache("XDG_CACHE_HOME", "/xdg/cache");
    // A relative one is ignored, as the XDG specification says.
    test::ScopedEnv config("XDG_CONFIG_HOME", "relative");
    ZEXPECT(dir_of(cache_dir) == "/xdg/cache");
    ZEXPECT(dir_of(config_dir) == "/home/kotatsu/.config");
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

}  // namespace kotatsu::sys
