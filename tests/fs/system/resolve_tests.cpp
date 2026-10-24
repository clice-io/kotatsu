#include <filesystem>
#include <string>
#include <system_error>

#include "fs/harness/files.h"
#include "kota/fs/directory.h"
#include "kota/fs/path.h"
#include "kota/fs/resolve.h"
#include "kota/zest/zest.h"

namespace kota::fs {

namespace {

/// What the OS calls the test's directory: the temp directory itself may be
/// reached through a link (macOS's /var is /private/var).
std::string real_root(const test::TempDir& dir) {
    auto real = canonical(dir.root());
    return real ? *real : std::string();
}

ZEST_SUITE(fs_resolve) {

ZEST_CASE(canonical_folds_dots) {
    test::TempDir dir;
    auto root = real_root(dir);
    ZASSERT(!root.empty());
    std::filesystem::create_directory(dir.path / "a");
    test::write_text(dir.file("a/file.txt"), "text");
    ZEXPECT(canonical(dir.file("a/./../a/file.txt")) ==
            path::normalize(path::join(root, "a/file.txt")));
}

ZEST_CASE(canonical_follows_links) {
    test::TempDir dir;
    auto root = real_root(dir);
    ZASSERT(!root.empty());
    std::filesystem::create_directory(dir.path / "a");
    test::write_text(dir.file("a/file.txt"), "text");
    if(auto error = create_directory_symlink("a", dir.file("link"))) {
        // Windows lets only an administrator or developer mode make links.
        ZEXPECT(error == std::errc::operation_not_permitted);
        zest::skip();
        return;
    }
    ZEXPECT(canonical(dir.file("link/file.txt")) ==
            path::normalize(path::join(root, "a/file.txt")));
}

ZEST_CASE(canonical_of_nothing_fails) {
    test::TempDir dir;
    auto real = canonical(dir.file("missing"));
    ZASSERT(!real.has_value());
    ZEXPECT(real.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(weakly_canonical_appends_what_does_not_exist) {
    test::TempDir dir;
    auto root = real_root(dir);
    ZASSERT(!root.empty());
    std::filesystem::create_directory(dir.path / "a");
    ZEXPECT(weakly_canonical(dir.file("a/missing/../new.h")) ==
            path::normalize(path::join(root, "a/new.h")));
    ZEXPECT(weakly_canonical(dir.file("é中/x")) == path::normalize(path::join(root, "é中/x")));
    ZEXPECT(weakly_canonical(dir.root()) == root);
}

ZEST_CASE(weakly_canonical_resolves_through_a_link) {
    test::TempDir dir;
    auto root = real_root(dir);
    ZASSERT(!root.empty());
    std::filesystem::create_directory(dir.path / "a");
    if(auto error = create_directory_symlink("a", dir.file("link"))) {
        ZEXPECT(error == std::errc::operation_not_permitted);
        zest::skip();
        return;
    }
    ZEXPECT(weakly_canonical(dir.file("link/missing.h")) ==
            path::normalize(path::join(root, "a/missing.h")));
}

ZEST_CASE(absolute_takes_the_current_directory) {
    auto cwd = current_dir();
    ZASSERT(cwd.has_value());
    auto made = absolute("some/name");
    ZASSERT(made.has_value());
    ZEXPECT(path::is_absolute(*made));
    auto rest = path::strip_prefix(*made, *cwd);
    ZASSERT(rest.has_value());
    ZEXPECT(path::components(*rest) == path::components("some/name"));
    ZEXPECT(absolute(*cwd) == path::normalize(*cwd));
}

ZEST_CASE(absolute_of_nothing_fails) {
    auto made = absolute("");
    ZASSERT(!made.has_value());
    ZEXPECT(made.error() == std::errc::invalid_argument);
}

ZEST_CASE(current_dir_is_what_the_os_says) {
    auto cwd = current_dir();
    ZASSERT(cwd.has_value());
    auto u8 = std::filesystem::current_path().u8string();
    ZEXPECT(*cwd == std::string(u8.begin(), u8.end()));
}

};  // ZEST_SUITE(fs_resolve)

}  // namespace

}  // namespace kota::fs
