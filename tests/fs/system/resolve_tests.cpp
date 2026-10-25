#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/support/small_string.h"
#include "kota/fs/directory.h"
#include "kota/fs/path.h"
#include "kota/fs/resolve.h"

namespace kota::fs {

namespace {

/// What the OS calls the test's directory: the temp directory itself may be
/// reached through a link (macOS's /var is /private/var).
std::string real_root(const test::TempDir& dir) {
    std::string real;
    ZASSERT(!canonical(real, dir.root()));
    return real;
}

/// `p` joined onto `base` and normalized, as a resolved path is spelled.
std::string under(std::string_view base, std::string_view p) {
    std::string joined(base);
    path::push(joined, p);
    path::normalize(joined);
    return joined;
}

ZEST_SUITE(fs_resolve) {

ZEST_CASE(canonical_folds_dots) {
    test::TempDir dir;
    auto root = real_root(dir);
    std::filesystem::create_directory(dir.path / "a");
    test::write_text(dir.file("a/file.txt"), "text");
    std::string real;
    ZEXPECT(!canonical(real, dir.file("a/./../a/file.txt")));
    ZEXPECT(real == under(root, "a/file.txt"));
}

ZEST_CASE(canonical_follows_links) {
    test::TempDir dir;
    auto root = real_root(dir);
    std::filesystem::create_directory(dir.path / "a");
    test::write_text(dir.file("a/file.txt"), "text");
    if(!test::linked(create_directory_symlink("a", dir.file("link")))) {
        return;
    }
    std::string real;
    ZEXPECT(!canonical(real, dir.file("link/file.txt")));
    ZEXPECT(real == under(root, "a/file.txt"));
}

// The path may view the buffer the result goes to.
ZEST_CASE(canonical_resolves_in_place) {
    test::TempDir dir;
    auto root = real_root(dir);
    std::filesystem::create_directory(dir.path / "a");
    auto p = dir.file("a/./../a");
    ZEXPECT(!canonical(p, p));
    ZEXPECT(p == under(root, "a"));
}

#ifdef _WIN32
ZEST_CASE(canonical_spells_the_name_on_disk) {
    test::TempDir dir;
    test::write_text(dir.file("MixedCase.txt"), "text");
    std::string real;
    ZASSERT(!canonical(real, dir.file("mixedcase.TXT")));
    ZEXPECT(path::filename(real) == "MixedCase.txt");
    ZEXPECT(!zest::starts_with(real, R"(\\?\)"));
}
#endif

ZEST_CASE(canonical_of_nothing_fails) {
    test::TempDir dir;
    std::string real;
    ZEXPECT(canonical(real, dir.file("missing")) == std::errc::no_such_file_or_directory);
}

ZEST_CASE(weakly_canonical_appends_what_does_not_exist) {
    test::TempDir dir;
    auto root = real_root(dir);
    std::filesystem::create_directory(dir.path / "a");
    std::string weak;
    weakly_canonical(weak, dir.file("a/missing/../new.h"));
    ZEXPECT(weak == under(root, "a/new.h"));
    weakly_canonical(weak, dir.file("é中/x"));
    ZEXPECT(weak == under(root, "é中/x"));
    weakly_canonical(weak, dir.root());
    ZEXPECT(weak == root);
}

ZEST_CASE(weakly_canonical_resolves_through_a_link) {
    test::TempDir dir;
    auto root = real_root(dir);
    std::filesystem::create_directory(dir.path / "a");
    if(!test::linked(create_directory_symlink("a", dir.file("link")))) {
        return;
    }
    std::string weak;
    weakly_canonical(weak, dir.file("link/missing.h"));
    ZEXPECT(weak == under(root, "a/missing.h"));
}

// The path may view the buffer the result goes to.
ZEST_CASE(weakly_canonical_resolves_in_place) {
    test::TempDir dir;
    auto root = real_root(dir);
    std::filesystem::create_directory(dir.path / "a");
    auto p = dir.file("a/missing/../new.h");
    weakly_canonical(p, p);
    ZEXPECT(p == under(root, "a/new.h"));
}

ZEST_CASE(weakly_canonical_takes_the_current_directory) {
    std::string cwd;
    ZASSERT(!current_dir(cwd));
    std::string real;
    ZASSERT(!canonical(real, cwd));
    std::string weak;
    weakly_canonical(weak, "missing/./name");
    ZEXPECT(weak == under(real, "missing/name"));
}

ZEST_CASE(absolute_takes_the_current_directory) {
    std::string cwd;
    ZASSERT(!current_dir(cwd));
    std::string made;
    ZASSERT(!absolute(made, "some/name"));
    ZEXPECT(path::is_absolute(made));
    auto rest = path::strip_prefix(made, cwd);
    ZASSERT(rest.has_value());
    ZEXPECT(std::ranges::equal(path::components(*rest), path::components("some/name")));
    ZEXPECT(!absolute(made, cwd));
    auto normalized = cwd;
    path::normalize(normalized);
    ZEXPECT(made == normalized);
}

ZEST_CASE(absolute_follows_the_platform_rules) {
    std::string made;
#ifdef _WIN32
    ZEXPECT(!absolute(made, R"(C:\a\.\b\..\c)"));
    ZEXPECT(made == R"(C:\a\c)");
    ZEXPECT(!absolute(made, "C:/a/b"));
    ZEXPECT(made == R"(C:\a\b)");
    // Win32 removes a name's trailing dots and spaces.
    ZEXPECT(!absolute(made, R"(C:\a\name. .)"));
    ZEXPECT(made == R"(C:\a\name)");
    std::string cwd;
    ZASSERT(!current_dir(cwd));
    // A rooted path is on the current directory's drive.
    ZEXPECT(!absolute(made, R"(\x)"));
    ZEXPECT(made == cwd.substr(0, 2) + R"(\x)");
#else
    // A symlink before `..` would make it name another directory.
    ZEXPECT(!absolute(made, "/a/./b//../c/"));
    ZEXPECT(made == "/a/b/../c");
#endif
}

ZEST_CASE(absolute_of_nothing_fails) {
    std::string made;
    ZEXPECT(absolute(made, "") == std::errc::invalid_argument);
}

ZEST_CASE(current_dir_is_what_the_os_says) {
    small_string<16> cwd;
    ZEXPECT(!current_dir(cwd));
    auto u8 = std::filesystem::current_path().u8string();
    ZEXPECT(std::string_view(cwd) == std::string(u8.begin(), u8.end()));
}

};  // ZEST_SUITE(fs_resolve)

}  // namespace

}  // namespace kota::fs
