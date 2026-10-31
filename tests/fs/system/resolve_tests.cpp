#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "fs/harness/files.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/support/small_string.h"
#include "kotatsu/fs/directory.h"
#include "kotatsu/fs/file.h"
#include "kotatsu/fs/path.h"
#include "kotatsu/fs/resolve.h"

namespace kotatsu::fs {

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

/// `p` with `/` for each separator, as a symlink's target and a path given
/// to make one may spell them otherwise on Windows.
std::string slashed(std::string p) {
    path::to_slash(p);
    return p;
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

#ifdef _WIN32
ZEST_CASE(canonical_names_a_volume_without_a_drive_letter_by_its_guid) {
    // KOTATSU_TEST_LETTERLESS_DIR is a volume's `\\?\Volume{...}\` that has
    // neither a drive letter nor a mount folder.
    const char* volume = std::getenv("KOTATSU_TEST_LETTERLESS_DIR");
    if(volume == nullptr || *volume == '\0') {
        // Only Windows CI mounts one.
        zest::skip();
        return;
    }
    test::TempDir dir{test::std_path(volume)};
    test::write_text(dir.file("file.txt"), "text");
    std::string real;
    ZEXPECT(!canonical(real, dir.file("file.txt")));
    ZEXPECT(real == dir.file("file.txt"));
    auto file = File::open(dir.file("file.txt"));
    ZASSERT(file.has_value());
    ZEXPECT(!file->canonical(real));
    ZEXPECT(real == dir.file("file.txt"));
    auto directory = Directory::open(dir.root());
    ZASSERT(directory.has_value());
    ZEXPECT(!directory->canonical(real));
    ZEXPECT(real == dir.root());
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

ZEST_CASE(weakly_canonical_keeps_dot_dot_when_asked) {
    test::TempDir dir;
    auto root = real_root(dir);
    std::filesystem::create_directory(dir.path / "a");
    std::string weak;
    weakly_canonical(weak, dir.file("a/missing/../new.h"), path::DotDot::Keep);
#ifdef _WIN32
    // Win32 folds `..` by text before it resolves anything.
    ZEXPECT(weak == under(root, "a/new.h"));
#else
    // The OS fails the name, as `missing` is not there to climb out of.
    std::string kept(root);
    path::push(kept, "a/missing/../new.h");
    ZEXPECT(weak == kept);
#endif
}

ZEST_CASE(follow_symlinks_of_no_link_is_the_path) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    std::string followed;
    ZEXPECT(!follow_symlinks(followed, dir.file("file.txt")));
    ZEXPECT(followed == dir.file("file.txt"));
    ZEXPECT(!follow_symlinks(followed, dir.file("missing/file.txt")));
    ZEXPECT(followed == dir.file("missing/file.txt"));
}

ZEST_CASE(follow_symlinks_follows_each_link_in_turn) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    if(!test::linked(create_symlink("file.txt", dir.file("second"))) ||
       !test::linked(create_symlink("second", dir.file("first")))) {
        return;
    }
    std::string followed;
    ZEXPECT(!follow_symlinks(followed, dir.file("first")));
    ZEXPECT(followed == dir.file("file.txt"));
}

ZEST_CASE(follow_symlinks_ends_where_a_dangling_link_leads) {
    // The file to write the links lead to, which need not exist yet.
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "a");
    if(!test::linked(create_symlink(dir.file("build/out.json"), dir.file("a/absolute"))) ||
       !test::linked(create_symlink("../build/out.json", dir.file("a/relative")))) {
        return;
    }
    std::string followed;
    ZEXPECT(!follow_symlinks(followed, dir.file("a/absolute")));
    ZEXPECT(slashed(followed) == slashed(dir.file("build/out.json")));
    ZEXPECT(!follow_symlinks(followed, dir.file("a/relative")));
#ifdef _WIN32
    // Windows resolves a link's `..` by text.
    ZEXPECT(slashed(followed) == slashed(dir.file("build/out.json")));
#else
    // `..` stays, as `a` could be a link too.
    ZEXPECT(slashed(followed) == slashed(dir.file("a/../build/out.json")));
#endif
}

ZEST_CASE(follow_symlinks_takes_each_target_in_its_own_link_directory) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "a");
    std::filesystem::create_directory(dir.path / "b");
    test::write_text(dir.file("b/file.txt"), "text");
    if(!test::linked(create_symlink("../b/second", dir.file("a/first"))) ||
       !test::linked(create_symlink("file.txt", dir.file("b/second")))) {
        return;
    }
    std::string followed;
    ZEXPECT(!follow_symlinks(followed, dir.file("a/first")));
#ifdef _WIN32
    ZEXPECT(slashed(followed) == slashed(dir.file("b/file.txt")));
#else
    ZEXPECT(slashed(followed) == slashed(dir.file("a/../b/file.txt")));
#endif
}

ZEST_CASE(follow_symlinks_normalizes_each_target) {
    // `.` goes; the cases before check what becomes of `..`.
    test::TempDir dir;
    if(!test::linked(
           create_symlink("./build/./compile_commands.json", dir.file("compile_commands.json")))) {
        return;
    }
    std::string followed;
    ZEXPECT(!follow_symlinks(followed, dir.file("compile_commands.json")));
    ZEXPECT(slashed(followed) == slashed(dir.file("build/compile_commands.json")));
}

ZEST_CASE(follow_symlinks_leaves_links_before_the_last_name) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "real");
    test::write_text(dir.file("real/file.txt"), "text");
    test::link_directory(dir.file("real"), dir.file("link"));
    std::string followed;
    ZEXPECT(!follow_symlinks(followed, dir.file("link/file.txt")));
    ZEXPECT(followed == dir.file("link/file.txt"));
}

ZEST_CASE(follow_symlinks_follows_a_link_to_a_directory) {
    // A junction on Windows, which names the directory whole.
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "real");
    test::link_directory(dir.file("real"), dir.file("link"));
    std::string followed;
    ZEXPECT(!follow_symlinks(followed, dir.file("link")));
    ZEXPECT(slashed(followed) == slashed(dir.file("real")));
}

#ifndef _WIN32
ZEST_CASE(follow_symlinks_reads_a_link_named_with_a_separator_after_it) {
    // `link/`, through which POSIX would follow `link`, as the path given and
    // as a target.
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "real");
    std::filesystem::create_directory_symlink("real", dir.path / "second");
    std::filesystem::create_directory_symlink("second/", dir.path / "first");
    std::string followed;
    ZEXPECT(!follow_symlinks(followed, dir.file("first/")));
    ZEXPECT(followed == dir.file("real"));
}
#endif

// The path may view the buffer the result goes to.
ZEST_CASE(follow_symlinks_resolves_in_place) {
    test::TempDir dir;
    test::write_text(dir.file("file.txt"), "text");
    if(!test::linked(create_symlink("file.txt", dir.file("link")))) {
        return;
    }
    auto p = dir.file("link");
    ZEXPECT(!follow_symlinks(p, p));
    ZEXPECT(p == dir.file("file.txt"));
}

ZEST_CASE(follow_symlinks_of_a_name_with_a_null_fails) {
    std::string followed;
    ZEXPECT(follow_symlinks(followed, std::string_view("a\0b", 3)) == std::errc::invalid_argument);
}

ZEST_CASE(follow_symlinks_of_a_loop_fails) {
    test::TempDir dir;
    if(!test::linked(create_symlink("b", dir.file("a"))) ||
       !test::linked(create_symlink("a", dir.file("b")))) {
        return;
    }
    std::string followed;
    ZEXPECT(follow_symlinks(followed, dir.file("a")) == std::errc::too_many_symbolic_link_levels);
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

// The path may view the buffer the result goes to.
ZEST_CASE(absolute_resolves_in_place) {
    std::string made;
    ZASSERT(!absolute(made, "some/name"));
    std::string p = "some/name";
    ZEXPECT(!absolute(p, p));
    ZEXPECT(p == made);
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

#ifdef _WIN32
ZEST_CASE(absolute_keeps_the_names_of_a_verbatim_path) {
    test::TempDir dir;
    // Win32 would fold the `..` and drop the dot; the file system keeps
    // both names apart.
    std::string dotted = R"(\\?\)" + dir.root();
    path::push(dotted, "name.");
    ZEXPECT(!write_file(dotted, "dotted"));
    ZEXPECT(!write_file(dir.file("name"), "plain"));
    std::string full;
    ZEXPECT(!absolute(full, dotted));
    ZEXPECT(full == dotted);
    std::string up = R"(\\?\)" + dir.root() + R"(\..\x)";
    ZEXPECT(!absolute(full, up));
    ZEXPECT(full == up);
    // Where the plain spelling names the same file, without the prefix.
    ZEXPECT(!absolute(full, R"(\\?\)" + dir.root()));
    ZEXPECT(full == dir.root());
    std::string real;
    weakly_canonical(real, dotted);
    std::string content;
    ZEXPECT(!read_file(content, real));
    ZEXPECT(content == "dotted");
    ZEXPECT(remove(dotted) == true);
}
#endif

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

}  // namespace kotatsu::fs
