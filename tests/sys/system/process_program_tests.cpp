#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "fs/harness/files.h"
#include "sys/harness/env.h"
#include "sys/harness/program.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/fs/path.h"
#include "kota/sys/process.h"

namespace kota::sys {

namespace {

using namespace std::literals;

/// The name of program `name`'s file on this host, its extension spelled as
/// the suite's PATHEXT spells it, which is how find_program spells it.
std::string program_file(std::string_view name) {
#ifdef _WIN32
    return std::string(name) + ".EXE";
#else
    return std::string(name);
#endif
}

/// A search of `directories` alone.
ProgramSearch in(std::initializer_list<std::string> directories) {
    return {.directories = directories};
}

/// The current directory, put back when this goes.
struct ScopedCurrentDir {
    explicit ScopedCurrentDir(const std::filesystem::path& path) {
        std::filesystem::current_path(path);
    }

    ScopedCurrentDir(const ScopedCurrentDir&) = delete;
    ScopedCurrentDir& operator=(const ScopedCurrentDir&) = delete;

    ~ScopedCurrentDir() {
        std::filesystem::current_path(original);
    }

    std::filesystem::path original = std::filesystem::current_path();
};

ZEST_SUITE(sys_process_program) {

test::TempDir dir;

#ifdef _WIN32
test::ScopedEnv pathext{"PATHEXT", ".COM;.EXE;.BAT;.CMD"};
#endif

ZEST_CASE(find_program_takes_the_first_directory_that_has_it) {
    auto search = in({dir.file("first"), dir.file("second")});
    auto second = test::make_program(dir, "second", program_file("tool"));
    ZEXPECT(find_program("tool", search) == second);
    auto first = test::make_program(dir, "first", program_file("tool"));
    ZEXPECT(find_program("tool", search) == first);
}

ZEST_CASE(find_program_searches_path_by_default) {
    auto program = test::make_program(dir, "bin", program_file("tool"));
    test::ScopedEnv path("PATH", dir.file("bin"));
    ZEXPECT(find_program("tool") == program);
}

ZEST_CASE(find_program_of_a_missing_program_fails) {
    auto found = find_program("missing", in({dir.root()}));
    ZASSERT(!found.has_value());
    ZEXPECT(found.error() == std::errc::no_such_file_or_directory);
    auto empty = find_program("");
    ZASSERT(!empty.has_value());
    ZEXPECT(empty.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(find_program_skips_a_directory_of_its_name) {
    std::filesystem::create_directories(dir.path / "first" / program_file("tool"));
    auto program = test::make_program(dir, "second", program_file("tool"));
    ZEXPECT(find_program("tool", in({dir.file("first"), dir.file("second")})) == program);
}

// Something of that name is there, as execvp's EACCES says.
ZEST_CASE(find_program_finding_only_a_directory_fails) {
    std::filesystem::create_directories(dir.path / program_file("tool"));
    auto found = find_program("tool", in({dir.root()}));
    ZASSERT(!found.has_value());
    ZEXPECT(found.error() == std::errc::permission_denied);
}

#ifndef _WIN32
ZEST_CASE(find_program_skips_a_file_it_cannot_run) {
    test::make_program(dir, "first", "tool", false);
    auto program = test::make_program(dir, "second", "tool");
    ZEXPECT(find_program("tool", in({dir.file("first"), dir.file("second")})) == program);
}

ZEST_CASE(find_program_finding_only_files_it_cannot_run_fails) {
    auto program = test::make_program(dir, "first", "tool", false);
    auto found = find_program("tool", in({dir.file("first")}));
    ZASSERT(!found.has_value());
    ZEXPECT(found.error() == std::errc::permission_denied);
    auto by_path = find_program(program);
    ZASSERT(!by_path.has_value());
    ZEXPECT(by_path.error() == std::errc::permission_denied);
}

/// The permissions of `path`, taken away until this goes.
struct ScopedNoPermissions {
    explicit ScopedNoPermissions(std::filesystem::path path) : path(std::move(path)) {
        std::filesystem::permissions(this->path, std::filesystem::perms::none);
    }

    ScopedNoPermissions(const ScopedNoPermissions&) = delete;
    ScopedNoPermissions& operator=(const ScopedNoPermissions&) = delete;

    ~ScopedNoPermissions() {
        std::filesystem::permissions(this->path, std::filesystem::perms::owner_all);
    }

    std::filesystem::path path;
};

// A directory that cannot be searched may hide a program, as execvp's
// EACCES says.
ZEST_CASE(find_program_behind_a_directory_it_cannot_search_fails) {
    // Root searches any directory.
    if(::geteuid() == 0) {
        zest::skip();
        return;
    }
    test::make_program(dir, "locked", "tool");
    ScopedNoPermissions locked(dir.path / "locked");
    auto found = find_program("tool", in({dir.file("locked")}));
    ZASSERT(!found.has_value());
    ZEXPECT(found.error() == std::errc::permission_denied);
}

ZEST_CASE(find_program_takes_a_name_with_a_slash_as_a_path) {
    auto program = test::make_program(dir, "", "tool");
    ZEXPECT(find_program(program, in({"/nowhere"})) == program);
    auto found = find_program("./not-there/tool");
    ZASSERT(!found.has_value());
    ZEXPECT(found.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(find_program_reads_an_empty_entry_as_the_current_directory) {
    test::make_program(dir, "", "tool");
    ScopedCurrentDir current(dir.path);
    ZEXPECT(find_program("tool", in({"/nowhere", ""})) == "./tool");
}

// confstr(_CS_PATH), where every system has a shell.
ZEST_CASE(find_program_without_path_searches_the_systems_directories) {
    test::ScopedEnv path("PATH", std::nullopt);
    auto found = find_program("sh");
    ZASSERT(found.has_value());
    ZEXPECT(found->ends_with("/sh"));
}
#else
ZEST_CASE(find_program_tries_each_extension_in_a_directory_first) {
    auto program = test::make_program(dir, "first", "tool.BAT");
    test::make_program(dir, "second", "tool.EXE");
    // Directory by directory, not extension by extension.
    ZEXPECT(find_program("tool", in({dir.file("first"), dir.file("second")})) == program);
}

ZEST_CASE(find_program_takes_a_listed_extension_as_it_is) {
    auto program = test::make_program(dir, "", "tool.exe");
    test::make_program(dir, "", "tool.exe.COM");
    ZEXPECT(find_program("tool.exe", in({dir.root()})) == program);
    // An extension not listed is no program's.
    auto python = test::make_program(dir, "", "python3.12.EXE");
    ZEXPECT(find_program("python3.12", in({dir.root()})) == python);
}

ZEST_CASE(find_program_with_an_empty_pathext_takes_the_usual_extensions) {
    test::ScopedEnv empty("PATHEXT", ";");
    auto program = test::make_program(dir, "", "tool.EXE");
    ZEXPECT(find_program("tool", in({dir.root()})) == program);
}

ZEST_CASE(find_program_tries_the_name_as_it_is_for_an_empty_extension) {
    auto program = test::make_program(dir, "", "tool");
    ProgramSearch search{
        .directories = {dir.root()},
        .extensions = {"", ".exe"}
    };
    ZEXPECT(find_program("tool", search) == program);
    search.extensions = {".exe"};
    auto found = find_program("tool", search);
    ZASSERT(!found.has_value());
    ZEXPECT(found.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(find_program_like_create_process_appends_exe_to_a_bare_name) {
    ProgramSearch search{.directories = {dir.root()}, .like_create_process = true};
    auto program = test::make_program(dir, "", "tool.exe");
    test::make_program(dir, "", "tool.bar.exe");
    ZEXPECT(find_program("tool", search) == program);
    // A name with an extension stays as it is.
    auto found = find_program("tool.bar", search);
    ZASSERT(!found.has_value());
    ZEXPECT(found.error() == std::errc::no_such_file_or_directory);
}

// The current directory is no entry of PATH's on Windows.
ZEST_CASE(find_program_reads_an_empty_entry_as_nothing) {
    test::make_program(dir, "", "tool.EXE");
    ScopedCurrentDir current(dir.path);
    auto found = find_program("tool", in({""}));
    ZASSERT(!found.has_value());
    ZEXPECT(found.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(create_process_search_starts_with_this_programs_directory) {
    auto search = windows::create_process_search();
    ZASSERT(search.has_value());
    ZASSERT(search->directories.size() >= 5U);
    auto executable = executable_path();
    ZASSERT(executable.has_value());
    ZEXPECT(search->directories[0] == fs::path::parent(*executable));
    ZEXPECT(search->like_create_process);
}

ZEST_CASE(find_program_takes_a_name_with_a_separator_as_a_path) {
    auto program = test::make_program(dir, "", "tool.EXE");
    ZEXPECT(find_program(dir.file("tool"), in({R"(C:\nowhere)"})) == program);
}

// What a Cygwin package installs for a link such as /usr/bin/cc, which
// Windows cannot start: find_program gives the program it names, `.exe`
// added.
ZEST_CASE(find_program_follows_a_cygwin_symlink) {
    auto program = test::make_program(dir, "", "gcc-13.exe");
    test::write_text(dir.file("cc"), "!<symlink>gcc-13\0"sv);
    ProgramSearch search{
        .directories = {dir.root()},
        .extensions = {"", ".EXE"}
    };
    ZEXPECT(find_program("cc", search) == program);
    // CreateProcess cannot start it.
    search.like_create_process = true;
    ZEXPECT(find_program("cc", search) == dir.file("cc"));
}

ZEST_CASE(find_program_of_a_cygwin_symlink_to_a_posix_path_fails) {
    test::write_text(dir.file("cc"), "!<symlink>/usr/bin/gcc-13\0"sv);
    ProgramSearch search{
        .directories = {dir.root()},
        .extensions = {"", ".EXE"}
    };
    auto found = find_program("cc", search);
    ZASSERT(!found.has_value());
    ZEXPECT(found.error() == std::errc::permission_denied);
}
#endif

};  // ZEST_SUITE(sys_process_program)

}  // namespace

}  // namespace kota::sys
