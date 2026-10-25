#include <expected>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef __linux__
#include <cerrno>
#include <cstddef>
#include <iterator>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <thread>
#endif

#include "fs/harness/files.h"
#include "sys/harness/env.h"
#include "sys/harness/program.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/fs/path.h"
#include "kota/sys/process.h"

#ifdef _WIN32
#include "kota/fs/cygwin.h"
#endif

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

/// The file find_program finds for `name`, or its error.
std::expected<std::string, std::error_code> found(std::string_view name,
                                                  const ProgramSearch& search = {}) {
    std::string out;
    if(auto error = find_program(out, name, search)) {
        return std::unexpected(error);
    }
    return out;
}

/// A search of `directories` alone.
ProgramSearch in(std::initializer_list<std::string> directories) {
    return {.directories = directories};
}

#if defined(__linux__) && !defined(__alpha__) && !defined(__mips__)
#ifdef SYS_faccessat2
constexpr unsigned faccessat2_number = SYS_faccessat2;
#else
// Its number on every architecture but Alpha's and MIPS's, for headers older
// than Linux 5.8.
constexpr unsigned faccessat2_number = 439;
#endif
#endif

ZEST_SUITE(sys_process_program) {

test::TempDir dir;

#ifdef _WIN32
test::ScopedEnv pathext{"PATHEXT", ".COM;.EXE;.BAT;.CMD"};
#endif

ZEST_CASE(find_program_takes_the_first_directory_that_has_it) {
    auto search = in({dir.file("first"), dir.file("second")});
    auto second = test::make_program(dir, "second", program_file("tool"));
    ZEXPECT(found("tool", search) == second);
    auto first = test::make_program(dir, "first", program_file("tool"));
    ZEXPECT(found("tool", search) == first);
}

ZEST_CASE(find_program_searches_path_by_default) {
    auto program = test::make_program(dir, "bin", program_file("tool"));
    test::ScopedEnv path("PATH", dir.file("bin"));
    ZEXPECT(found("tool") == program);
}

ZEST_CASE(find_program_of_a_missing_program_fails) {
    auto result = found("missing", in({dir.root()}));
    ZASSERT(!result.has_value());
    ZEXPECT(result.error() == std::errc::no_such_file_or_directory);
    auto empty = found("");
    ZASSERT(!empty.has_value());
    ZEXPECT(empty.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(find_program_skips_a_directory_of_its_name) {
    std::filesystem::create_directories(dir.path / "first" / program_file("tool"));
    auto program = test::make_program(dir, "second", program_file("tool"));
    ZEXPECT(found("tool", in({dir.file("first"), dir.file("second")})) == program);
}

// Something of that name is there, as execvp's EACCES says.
ZEST_CASE(find_program_finding_only_a_directory_fails) {
    std::filesystem::create_directories(dir.path / program_file("tool"));
    auto result = found("tool", in({dir.root()}));
    ZASSERT(!result.has_value());
    ZEXPECT(result.error() == std::errc::permission_denied);
}

#ifndef _WIN32
ZEST_CASE(find_program_skips_a_file_it_cannot_run) {
    test::make_program(dir, "first", "tool", false);
    auto program = test::make_program(dir, "second", "tool");
    ZEXPECT(found("tool", in({dir.file("first"), dir.file("second")})) == program);
}

ZEST_CASE(find_program_finding_only_files_it_cannot_run_fails) {
    auto program = test::make_program(dir, "first", "tool", false);
    auto result = found("tool", in({dir.file("first")}));
    ZASSERT(!result.has_value());
    ZEXPECT(result.error() == std::errc::permission_denied);
    auto by_path = found(program);
    ZASSERT(!by_path.has_value());
    ZEXPECT(by_path.error() == std::errc::permission_denied);
}

// The first, for a message to name.
ZEST_CASE(find_program_finding_only_files_it_cannot_run_names_the_first) {
    auto first = test::make_program(dir, "first", "tool", false);
    test::make_program(dir, "second", "tool", false);
    test::ScopedEnv path("PATH",
                         dir.file("first") + ":" + dir.file("second") + ":" + dir.file("third"));
    std::string out;
    ZEXPECT(find_program(out, "tool") == std::errc::permission_denied);
    ZEXPECT(out == first);
}

#if defined(__linux__) && !defined(__alpha__) && !defined(__mips__)
// A sandbox made before faccessat2 (Linux 5.8), such as Docker's before
// 20.10.6, refuses it with EPERM, which glibc's faccessat calls from 2.33 on:
// a program is found all the same.
ZEST_CASE(find_program_where_faccessat2_is_refused_finds_the_program) {
    auto program = test::make_program(dir, "", "tool");
    bool filtered = false;
    std::error_code error;
    std::string out;
    // A filter of this thread's alone, which goes with it.
    std::thread([&] {
        sock_filter filter[] = {
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, faccessat2_number, 0, 1),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        };
        sock_fprog bpf{.len = static_cast<unsigned short>(std::size(filter)), .filter = filter};
        filtered = ::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
                   ::prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &bpf) == 0;
        error = find_program(out, "tool", in({dir.root()}));
    }).join();
    // A kernel built without seccomp filters has none to set.
    if(!filtered) {
        zest::skip();
        return;
    }
    ZEXPECT(!error);
    ZEXPECT(out == program);
}
#endif

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
    auto result = found("tool", in({dir.file("locked")}));
    ZASSERT(!result.has_value());
    ZEXPECT(result.error() == std::errc::permission_denied);
}

ZEST_CASE(find_program_takes_a_name_with_a_slash_as_a_path) {
    auto program = test::make_program(dir, "", "tool");
    ZEXPECT(found(program, in({"/nowhere"})) == program);
    auto result = found("./not-there/tool");
    ZASSERT(!result.has_value());
    ZEXPECT(result.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(find_program_reads_an_empty_entry_as_the_current_directory) {
    test::make_program(dir, "", "tool");
    test::ScopedCurrentDir current(dir.path);
    ZEXPECT(found("tool", in({"/nowhere", ""})) == "./tool");
}

// confstr(_CS_PATH), where every system has a shell.
ZEST_CASE(find_program_without_path_searches_the_systems_directories) {
    test::ScopedEnv path("PATH", std::nullopt);
    auto result = found("sh");
    ZASSERT(result.has_value());
    ZEXPECT(result->ends_with("/sh"));
}
#else
ZEST_CASE(find_program_tries_each_extension_in_a_directory_first) {
    auto program = test::make_program(dir, "first", "tool.BAT");
    test::make_program(dir, "second", "tool.EXE");
    // Directory by directory, not extension by extension.
    ZEXPECT(found("tool", in({dir.file("first"), dir.file("second")})) == program);
}

ZEST_CASE(find_program_takes_a_listed_extension_as_it_is) {
    auto program = test::make_program(dir, "", "tool.exe");
    test::make_program(dir, "", "tool.exe.COM");
    ZEXPECT(found("tool.exe", in({dir.root()})) == program);
    // An extension not listed is no program's.
    auto python = test::make_program(dir, "", "python3.12.EXE");
    ZEXPECT(found("python3.12", in({dir.root()})) == python);
}

ZEST_CASE(find_program_with_an_empty_pathext_takes_the_usual_extensions) {
    test::ScopedEnv empty("PATHEXT", ";");
    auto program = test::make_program(dir, "", "tool.EXE");
    ZEXPECT(found("tool", in({dir.root()})) == program);
}

ZEST_CASE(find_program_tries_the_name_as_it_is_for_an_empty_extension) {
    auto program = test::make_program(dir, "", "tool");
    ProgramSearch search{
        .directories = {dir.root()},
        .extensions = {"", ".exe"}
    };
    ZEXPECT(found("tool", search) == program);
    search.extensions = {".exe"};
    auto result = found("tool", search);
    ZASSERT(!result.has_value());
    ZEXPECT(result.error() == std::errc::no_such_file_or_directory);
}

// The current directory is no entry of PATH's on Windows.
ZEST_CASE(find_program_reads_an_empty_entry_as_nothing) {
    test::make_program(dir, "", "tool.EXE");
    test::ScopedCurrentDir current(dir.path);
    auto result = found("tool", in({""}));
    ZASSERT(!result.has_value());
    ZEXPECT(result.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(find_program_takes_a_name_with_a_separator_as_a_path) {
    auto program = test::make_program(dir, "", "tool.EXE");
    ZEXPECT(found(dir.file("tool"), in({R"(C:\nowhere)"})) == program);
}

// In the current directory of its drive, not in a directory on that drive.
ZEST_CASE(find_program_takes_a_drive_relative_name_as_a_path) {
    test::make_program(dir, "", "tool.EXE");
    test::make_program(dir, "elsewhere", "tool.EXE");
    test::ScopedCurrentDir current(dir.path);
    auto name = dir.root().substr(0, 2) + "tool";
    ZEXPECT(found(name, in({dir.file("elsewhere")})) == name + ".EXE");
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
    ZEXPECT(found("cc", search) == program);
}

// The link, which says where it leads, for a message to name.
ZEST_CASE(find_program_of_a_cygwin_symlink_to_a_posix_path_fails) {
    test::write_text(dir.file("cc"), "!<symlink>/usr/bin/gcc-13\0"sv);
    ProgramSearch search{
        .directories = {dir.root()},
        .extensions = {"", ".EXE"}
    };
    std::string out;
    ZEXPECT(find_program(out, "cc", search) == std::errc::permission_denied);
    ZEXPECT(out == dir.file("cc"));
    std::string target;
    ZEXPECT(fs::cygwin::read_symlink(target, out));
    ZEXPECT(target == "/usr/bin/gcc-13");
}

// The last link of a chain, the one that names the POSIX path.
ZEST_CASE(find_program_of_a_cygwin_symlink_chain_to_a_posix_path_names_its_last_link) {
    test::write_text(dir.file("cc"), "!<symlink>gcc\0"sv);
    test::write_text(dir.file("gcc"), "!<symlink>/usr/bin/gcc-13\0"sv);
    ProgramSearch search{
        .directories = {dir.root()},
        .extensions = {"", ".EXE"}
    };
    std::string out;
    ZEXPECT(find_program(out, "cc", search) == std::errc::permission_denied);
    ZEXPECT(out == dir.file("gcc"));
}

// The link, whose target is no file; the last of a chain.
ZEST_CASE(find_program_of_a_cygwin_symlink_to_nothing_names_the_link) {
    test::write_text(dir.file("cc"), "!<symlink>gcc-13\0"sv);
    ProgramSearch search{
        .directories = {dir.root()},
        .extensions = {"", ".EXE"}
    };
    std::string out;
    ZEXPECT(find_program(out, "cc", search) == std::errc::permission_denied);
    ZEXPECT(out == dir.file("cc"));
    test::write_text(dir.file("c++"), "!<symlink>cc\0"sv);
    ZEXPECT(find_program(out, "c++", search) == std::errc::permission_denied);
    ZEXPECT(out == dir.file("cc"));
}

// The link found, links that loop having no last.
ZEST_CASE(find_program_of_cygwin_symlinks_that_loop_names_the_first) {
    test::write_text(dir.file("cc"), "!<symlink>gcc\0"sv);
    test::write_text(dir.file("gcc"), "!<symlink>cc\0"sv);
    ProgramSearch search{
        .directories = {dir.root()},
        .extensions = {"", ".EXE"}
    };
    std::string out;
    ZEXPECT(find_program(out, "cc", search) == std::errc::permission_denied);
    ZEXPECT(out == dir.file("cc"));
    ZEXPECT(find_program(out, "gcc", search) == std::errc::permission_denied);
    ZEXPECT(out == dir.file("gcc"));
}
#endif

};  // ZEST_SUITE(sys_process_program)

}  // namespace

}  // namespace kota::sys
